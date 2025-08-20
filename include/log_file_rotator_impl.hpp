#pragma once
/**
 * @file log_file_rotator_impl.hpp
 * @brief Implementation of file rotation service with compression support
 * 
 * This file contains the implementation of the file rotation service that handles:
 * - Zero-gap atomic file rotation
 * - Time and size-based rotation policies
 * - Retention management (by count, age, size)
 * - Automatic ENOSPC handling with cleanup priorities
 * - Optional background compression with batching
 * - Platform-specific sync operations
 * 
 * The service runs a background thread for rotation operations and optionally
 * a compression thread for asynchronous gzip compression of rotated files.
 */

#include "log_file_rotator.hpp"
#include "log_gzip.hpp"
#include "log.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <string.h>

// Platform-specific sync implementation
inline int log_fdatasync(int fd) {
#ifdef _WIN32
    // Windows: use _commit to flush file buffers
    return ::_commit(fd);
#elif defined(__APPLE__)
    // On macOS, fsync doesn't guarantee durability to disk
    // F_FULLFSYNC forces all buffered data to permanent storage
    return ::fcntl(fd, F_FULLFSYNC);
#else
    // On Linux and other POSIX systems, use fdatasync for better performance
    // fdatasync omits metadata sync (like timestamps) which we don't need
    return ::fdatasync(fd);
#endif
}

namespace slwoggy
{

#ifdef _WIN32
// Windows doesn't support file rotation yet
#warning "File rotation is not supported on Windows"
#endif

// Internal constants (not user-tunable)
static constexpr size_t MIN_TIMESTAMP_LENGTH = 19; // YYYYMMDD-HHMMSS-NNN minimum length for parsing

// Thread-safe error string helper
inline std::string get_error_string(int err) {
    char errbuf[256];
    
#ifdef _GNU_SOURCE
    // GNU version returns char* which may or may not use the buffer
    const char* msg = strerror_r(err, errbuf, sizeof(errbuf));
    return std::string(msg);
#else
    // POSIX version returns int and always uses the buffer
    int ret = strerror_r(err, errbuf, sizeof(errbuf));
    if (ret != 0) {
        // strerror_r failed, return a generic message
        return "Unknown error " + std::to_string(err);
    }
    return std::string(errbuf);
#endif
}

// Helper function to convert filesystem time to system clock time
// This is more robust than trying to calculate epoch differences
inline std::chrono::system_clock::time_point 
filesystem_time_to_system_time(const std::filesystem::file_time_type& ftime)
{
    // C++20 would allow std::chrono::clock_cast, but for C++17 compatibility
    // we use a more portable approach
    
    // Get current time in both clocks to calculate offset
    auto sys_now = std::chrono::system_clock::now();
    auto file_now = std::filesystem::file_time_type::clock::now();
    
    // Calculate the difference and apply to the target time
    // This assumes the clock offset is relatively stable
    auto file_epoch = file_now.time_since_epoch();
    auto sys_epoch = sys_now.time_since_epoch();
    auto ftime_epoch = ftime.time_since_epoch();
    
    // Convert to common duration type (nanoseconds for precision)
    auto file_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(file_epoch);
    auto sys_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(sys_epoch);
    auto ftime_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(ftime_epoch);
    
    // Calculate offset and apply
    auto offset_ns = sys_ns - file_ns;
    auto result_ns = ftime_ns + offset_ns;
    
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(result_ns));
}

// rotation_metrics implementation
inline void rotation_metrics::dump_metrics() const
{
    LOG(info) << "Rotation Metrics:";
    LOG(info) << "  Dropped: " << dropped_records_total.load() << " records, " << dropped_bytes_total.load() << " bytes";
    LOG(info) << "  Rotations: " << rotations_total.load() << " total";
    
    auto count = rotation_duration_us_count.load();
    if (count > 0)
    {
        auto sum = rotation_duration_us_sum.load();
        auto avg_us = sum / count;
        LOG(info) << "  Avg rotation time: " << avg_us << " us";
    }
    LOG(info) << "  ENOSPC deletions: pending=" << enospc_deletions_pending.load() << " gz=" << enospc_deletions_gz.load()
              << " raw=" << enospc_deletions_raw.load() << " bytes=" << enospc_deleted_bytes.load();
    LOG(info) << "  Zero-gap fallbacks: " << zero_gap_fallback_total.load();
    LOG(info) << "  Failures: compression=" << compression_failures.load()
              << " prepare_fd=" << prepare_fd_failures.load() << " fsync=" << fsync_failures.load();
}

// file_rotation_service implementation
inline file_rotation_service::file_rotation_service() : running_(true)
{
    rotator_thread_ = std::thread(&file_rotation_service::rotator_thread_func, this);
}

inline file_rotation_service::~file_rotation_service()
{
    // Signal shutdown
    running_.store(false);
    
    // Stop compression thread if running
    if (compression_running_.load()) {
        stop_compression_thread();
    }

    // Send sentinel message to wake thread
    queue_.enqueue({rotation_message::SHUTDOWN, nullptr, -1, ""});

    // Wait for thread to finish
    if (rotator_thread_.joinable()) { rotator_thread_.join(); }

    // Final fdatasync for all open handles and cleanup temp files
    std::lock_guard<std::mutex> lock(handles_mutex_);
    auto &metrics = rotation_metrics::instance();
    for (auto &weak_handle : handles_)
    {
        if (auto handle = weak_handle.lock())
        {
            // Aggregate dropped metrics before shutdown
            metrics.dropped_records_total.fetch_add(handle->dropped_records_.load());
            metrics.dropped_bytes_total.fetch_add(handle->dropped_bytes_.load());

            int fd = handle->current_fd_.load();
            if (fd >= 0) { log_fdatasync(fd); }

            // Clean up any prepared temp file
            int next_fd = handle->next_fd_.load();
            if (next_fd >= 0)
            {
                close(next_fd);
                if (!handle->next_temp_filename_.empty()) { unlink(handle->next_temp_filename_.c_str()); }
            }
        }
    }
}

inline void file_rotation_service::rotator_thread_func()
{
    // This thread CANNOT initiate rotations!
    // Rotation happens in two phases:
    // 1. DETECTION & FD SWAP (in writer's write() method)
    // 2. CLEANUP & PREPARATION (in this thread)

    while (running_.load())
    {
        rotation_message msg;

        // Block indefinitely waiting for rotation messages from writers
        queue_.wait_dequeue(msg);

        // Process the message
        switch (msg.msg_type)
        {
        case rotation_message::ROTATE: handle_rotation(msg); break;
        case rotation_message::CLOSE: handle_close(msg); break;
        case rotation_message::SHUTDOWN: drain_queue(); return;
        }
    }
}

// Helper: Perform atomic zero-gap rotation
inline bool file_rotation_service::perform_zero_gap_rotation(
    const std::filesystem::path& base_path,
    const std::filesystem::path& rotated_path,
    const std::string& temp_filename,
    rotation_handle* handle)
{
    int link_attempts = 0;
    while (link_attempts < ROTATION_LINK_ATTEMPTS)
    {
        if (link(base_path.c_str(), rotated_path.c_str()) == 0)
        {
            // Success: hard link created
            // Atomically replace current with temp (NO GAP!)
            if (rename(temp_filename.c_str(), base_path.c_str()) != 0)
            {
                LOG(error) << "Failed to rename temp to base: " << get_error_string(errno);
            }
            return true;
        }
        else if (errno == EEXIST)
        {
            // Race: another rotation created this name - regenerate
            link_attempts++;
            if (link_attempts < ROTATION_LINK_ATTEMPTS)
            {
                LOG(info) << "Rotated filename exists, will retry with new name";
            }
            return false; // Caller will regenerate filename
        }
        else
        {
            // Other failure - use fallback
            break;
        }
    }
    
    // Fall back to two-rename sequence
    rotation_metrics::instance().zero_gap_fallback_total.fetch_add(1);
    LOG(warn) << "Hard link failed, using fallback rotation with gap: " << get_error_string(errno);
    
    if (rename(base_path.c_str(), rotated_path.c_str()) == 0)
    {
        rename(temp_filename.c_str(), base_path.c_str());
    }
    else if (errno == EEXIST)
    {
        // Rotated name exists, caller should retry with new name
        return false;
    }
    return true;
}

// Helper: Ensure directory metadata is synced
inline void file_rotation_service::sync_directory(const std::filesystem::path& base_path)
{
    std::string dir_path = base_path.parent_path().string();
    if (dir_path.empty()) dir_path = ".";

#ifdef O_DIRECTORY
    int dir_fd = ::open(dir_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
#else
    int dir_fd = ::open(dir_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (dir_fd >= 0)
    {
        struct stat st;
        if (fstat(dir_fd, &st) == 0 && !S_ISDIR(st.st_mode))
        {
            close(dir_fd);
            dir_fd = -1;
            LOG(error) << "Parent path is not a directory";
        }
    }
#endif
    if (dir_fd >= 0)
    {
        fsync(dir_fd); // Single fsync after batch
        close(dir_fd);
    }
    else 
    { 
        LOG(error) << "Failed to fsync directory - renames not durable!"; 
    }
}

inline void file_rotation_service::handle_rotation(const rotation_message &msg)
{
    // This function handles PHASE 2 of rotation
    // The writer has already swapped to the next fd
    auto rotation_start = std::chrono::steady_clock::now();
    namespace fs = std::filesystem;

    // Step 1: Durability - fdatasync before rotation if configured
    if (msg.handle->policy_.sync_on_rotate && msg.old_fd >= 0)
    {
        if (log_fdatasync(msg.old_fd) != 0) 
        { 
            rotation_metrics::instance().fsync_failures.fetch_add(1); 
        }
    }

    // Step 2: Perform rotation
    if (msg.temp_filename.empty())
    {
        // This should never happen - writer must swap fd first
        LOG(fatal) << "Rotation message without temp file - programming error!";
        abort();
    }
    
    close(msg.old_fd);
    
    // Generate timestamped filename and perform rotation
    fs::path base_path(msg.handle->base_filename_);
    std::string rotated_name;
    fs::path rotated_path;
    
    // Retry rotation with new filenames if collision occurs
    for (int retry = 0; retry < ROTATION_LINK_ATTEMPTS; ++retry)
    {
        rotated_name = generate_rotated_filename(msg.handle->base_filename_);
        rotated_path = rotated_name;
        
        if (perform_zero_gap_rotation(base_path, rotated_path, msg.temp_filename, msg.handle.get()))
        {
            break; // Success
        }
        // perform_zero_gap_rotation returns false if filename exists, retry with new name
    }

    // Step 3: Ensure directory durability
    sync_directory(base_path);

    // Step 4: Update cache with rotated file
    std::shared_ptr<rotation_handle::rotated_file_entry> entry;
    struct stat st;
    if (::stat(rotated_name.c_str(), &st) == 0)
    {
        entry = add_to_cache(msg.handle.get(), rotated_name, st.st_size);
    }
    else
    {
        LOG(error) << "Failed to get file size for cache: " << get_error_string(errno);
    }

    // Step 5: Apply retention policies
    apply_retention_timestamped(msg.handle.get());

    // Step 6: Compress if needed
    if (msg.handle->policy_.compress && entry) 
    { 
        // Check if compression thread is running
        if (compression_running_.load()) {
            // Queue for async compression
            if (!enqueue_for_compression(entry)) {
                // Queue was full, compression skipped
                rotation_metrics::instance().compression_queue_overflows.fetch_add(1, std::memory_order_relaxed);
                LOG(warn) << "Compression queue full, skipping compression for: " << entry->filename;
            }
        } else {
            // Compress synchronously in rotation thread
            compress_file_sync(entry);
        }
    }

    // Step 7: Update next rotation time for time-based policies
    if (msg.handle->policy_.mode == rotate_policy::kind::time || 
        msg.handle->policy_.mode == rotate_policy::kind::size_or_time)
    {
        msg.handle->compute_next_rotation_time();
    }

    // Step 8: Prepare next fd for future rotation
    prepare_next_fd_with_retry(msg.handle.get());

    // Step 9: Track metrics
    auto rotation_end = std::chrono::steady_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
        rotation_end - rotation_start).count();
    rotation_metrics::instance().rotations_total.fetch_add(1);
    rotation_metrics::instance().rotation_duration_us_sum.fetch_add(duration_us);
    rotation_metrics::instance().rotation_duration_us_count.fetch_add(1);
}

inline std::string file_rotation_service::generate_rotated_filename(const std::string &base)
{
    // Generate timestamp in UTC
    auto now    = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_utc;
    gmtime_r(&time_t, &tm_utc);

    // Format: base-YYYYMMDD-HHMMSS-NNN.ext
    std::ostringstream timestamp;
    timestamp << std::setfill('0') << "-" << std::setw(4) << (tm_utc.tm_year + 1900) << std::setw(2)
              << (tm_utc.tm_mon + 1) << std::setw(2) << tm_utc.tm_mday << "-" << std::setw(2) << tm_utc.tm_hour
              << std::setw(2) << tm_utc.tm_min << std::setw(2) << tm_utc.tm_sec;

    // Split base into directory, stem and extension
    namespace fs = std::filesystem;
    fs::path base_path(base);
    fs::path dir = base_path.parent_path();
    std::string stem = base_path.stem().string();
    std::string ext = base_path.extension().string();
    
    // If no extension or invalid extension format, use .log
    if (ext.empty() || ext.size() < MIN_EXTENSION_SIZE || !ext.starts_with(".")) {
        ext = ".log";
    }

    // Retry on filename collision
    for (int seq = 1; seq < 10000; ++seq)
    {
        std::ostringstream final_name;
        if (!dir.empty()) {
            final_name << dir.string() << "/" << stem << timestamp.str() << "-" << std::setfill('0') << std::setw(3) << seq << ext;
        } else {
            final_name << stem << timestamp.str() << "-" << std::setfill('0') << std::setw(3) << seq << ext;
        }

        struct stat st;
        // Check if either the uncompressed or compressed version exists
        std::string gz_name = final_name.str() + ".gz";
        if ((stat(final_name.str().c_str(), &st) != 0 && errno == ENOENT) &&
            (stat(gz_name.c_str(), &st) != 0 && errno == ENOENT))
        {
            // Neither file exists - use this name
            return final_name.str();
        }
    }

    // Fallback: add microseconds for uniqueness
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000;
    std::ostringstream final_name;
    if (!dir.empty()) {
        final_name << dir.string() << "/" << stem << timestamp.str() << "-" << std::setfill('0') << std::setw(6) << us << ext;
    } else {
        final_name << stem << timestamp.str() << "-" << std::setfill('0') << std::setw(6) << us << ext;
    }
    return final_name.str();
}

inline std::string file_rotation_service::generate_temp_filename(const std::string &base)
{
    // Temp files MUST be created in the same directory as app.log
    namespace fs = std::filesystem;
    fs::path base_path(base);
    fs::path dir = base_path.parent_path();
    if (dir.empty()) { dir = "."; }

    // Generate unique temp name in same directory
    static std::atomic<uint64_t> temp_counter{0};
    uint64_t id = temp_counter.fetch_add(1);
    auto pid    = getpid();
    auto tid    = std::hash<std::thread::id>{}(std::this_thread::get_id());

    // Format: .app.log.tmp.PID.TID.COUNTER
    std::ostringstream oss;
    oss << dir.string() << "/." << base_path.filename().string() << ".tmp." << pid << "." << tid << "." << id;
    return oss.str();
}

inline void file_rotation_service::prepare_next_fd_with_retry(rotation_handle *handle)
{
    // Use constants defined at namespace level
    auto backoff       = ROTATION_INITIAL_BACKOFF;
    int retry_count    = 0;
    bool tried_cleanup = false;

    while (retry_count < ROTATION_MAX_RETRIES)
    {
        // Generate temp filename
        std::string temp_name = generate_temp_filename(handle->base_filename_);

        // Try to open new fd
        int new_fd = ::open(temp_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0644);
        if (new_fd >= 0)
        {
            // Use compare-and-swap to atomically check and set the FD
            // This prevents race conditions where multiple threads might prepare FDs
            // Use weak version in case of spurious failures on some architectures
            int expected = -1;
            bool exchanged = false;
            while (expected == -1 && !(exchanged = handle->next_fd_.compare_exchange_weak(expected, new_fd, std::memory_order_acq_rel))) {
                // Retry on spurious failure (expected still -1)
                // If expected changed, another thread set it, exit loop
            }
            if (exchanged)
            {
                // Successfully claimed the slot - store metadata
                // IMPORTANT: Set temp filename BEFORE setting ready flag
                // The release-acquire synchronization with next_fd_ready_ ensures
                // that readers will see both next_fd_ and next_temp_filename_
                handle->next_temp_filename_ = temp_name;
                handle->next_fd_ready_.store(true, std::memory_order_release);
                handle->consecutive_failures_.store(0);

                // Signal semaphore in case sink is waiting
                handle->next_fd_semaphore_.signal();
                return;
            }
            else
            {
                // Another thread already set next_fd - clean up our FD
                ::close(new_fd);
                ::unlink(temp_name.c_str());
                return; // Previous fd not consumed yet
            }
        }

        // Check if it's ENOSPC
        if (errno == ENOSPC && !tried_cleanup)
        {
            LOG(warn) << "ENOSPC encountered, attempting to free space";

            if (emergency_cleanup(handle))
            {
                tried_cleanup = true;
                continue; // Retry immediately
            }
        }

        // Failed - log and retry with backoff
        LOG(error) << "Failed to open rotation file " << temp_name << ": " << get_error_string(errno) << " (retry "
                   << retry_count + 1 << "/" << ROTATION_MAX_RETRIES << ")";

        retry_count++;
        handle->consecutive_failures_.fetch_add(1);

        std::this_thread::sleep_for(backoff);
        backoff = (backoff * 2 > ROTATION_MAX_BACKOFF) ? ROTATION_MAX_BACKOFF : backoff * 2;
    }

    // Max retries exceeded - enter error state
    LOG(error) << "Failed to prepare next fd after " << ROTATION_MAX_RETRIES << " attempts for " << handle->base_filename_
               << " - entering error state";

    rotation_metrics::instance().prepare_fd_failures.fetch_add(1);
    handle->in_error_state_.store(true, std::memory_order_release);

    // Signal semaphore to unblock any waiting writers
    handle->next_fd_semaphore_.signal();
}

inline void file_rotation_service::apply_retention_timestamped(rotation_handle *handle)
{
    std::lock_guard<std::mutex> lock(handle->cache_mutex_);

    auto &files = handle->rotated_files_cache_;

    // Sort by timestamp (oldest first)
    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a->timestamp < b->timestamp; });

    size_t total_bytes = 0;
    int to_keep        = handle->policy_.keep_files;
    auto now           = std::chrono::system_clock::now();

    std::vector<size_t> to_delete;

    for (size_t i = 0; i < files.size(); ++i)
    {
        bool should_delete = false;

        // Retention precedence: 1. keep_files, 2. max_total_bytes, 3. max_age
        if (to_keep > 0 && static_cast<int>(i) < static_cast<int>(files.size()) - to_keep) { should_delete = true; }
        else if (handle->policy_.max_total_bytes > 0 && total_bytes + files[i]->size > handle->policy_.max_total_bytes)
        {
            should_delete = true;
        }
        else if (handle->policy_.max_age.count() > 0)
        {
            auto age = now - files[i]->timestamp;
            if (age > handle->policy_.max_age) { should_delete = true; }
        }

        if (should_delete)
        {
            // Mark as cancelled if compression is queued or in progress
            compression_state expected = compression_state::queued;
            if (files[i]->comp_state.compare_exchange_strong(expected, compression_state::cancelled)) {
                compression_files_cancelled_.fetch_add(1);
            } else {
                expected = compression_state::compressing;
                if (files[i]->comp_state.compare_exchange_strong(expected, compression_state::cancelled)) {
                    compression_files_cancelled_.fetch_add(1);
                }
            }

            to_delete.push_back(i);
            unlink(files[i]->filename.c_str());
            LOG(debug) << "Deleted old log file: " << files[i]->filename;
        }
        else { total_bytes += files[i]->size; }
    }

    // Remove deleted entries from cache (reverse order)
    for (auto it = to_delete.rbegin(); it != to_delete.rend(); ++it) { files.erase(files.begin() + *it); }
}

inline bool file_rotation_service::emergency_cleanup(rotation_handle *handle)
{
    // ENOSPC last-resort cleanup - VIOLATES retention policies!
    std::lock_guard<std::mutex> lock(handle->cache_mutex_);
    auto &files   = handle->rotated_files_cache_;
    auto &metrics = rotation_metrics::instance();

    if (files.empty()) { return false; }

    // Try deleting .pending files first
    for (size_t i = 0; i < files.size(); ++i)
    {
        // Check for proper suffix to avoid false positives
        const auto& fname = files[i]->filename;
        if (fname.size() >= 8 && fname.substr(fname.size() - 8) == ".pending")
        {
            LOG(info) << "ENOSPC: Deleting pending file: " << files[i]->filename;
            metrics.enospc_deletions_pending.fetch_add(1);
            metrics.enospc_deleted_bytes.fetch_add(files[i]->size);
            unlink(files[i]->filename.c_str());
            files.erase(files.begin() + i);
            return true;
        }
    }

    // Then try .gz files
    for (size_t i = 0; i < files.size(); ++i)
    {
        // Check for proper suffix to avoid false positives
        const auto& fname = files[i]->filename;
        if (fname.size() >= 3 && fname.substr(fname.size() - 3) == ".gz")
        {
            LOG(info) << "ENOSPC: Deleting compressed file: " << files[i]->filename;
            metrics.enospc_deletions_gz.fetch_add(1);
            metrics.enospc_deleted_bytes.fetch_add(files[i]->size);
            unlink(files[i]->filename.c_str());
            files.erase(files.begin() + i);
            return true;
        }
    }

    // Finally delete oldest raw log
    if (!files.empty())
    {
        LOG(warn) << "ENOSPC: Deleting log (violates retention): " << files[0]->filename;
        metrics.enospc_deletions_raw.fetch_add(1);
        metrics.enospc_deleted_bytes.fetch_add(files[0]->size);
        unlink(files[0]->filename.c_str());
        files.erase(files.begin());
        return true;
    }

    return false;
}

inline std::shared_ptr<rotation_handle::rotated_file_entry> file_rotation_service::add_to_cache(rotation_handle *handle, const std::string &filename, size_t size)
{
    std::lock_guard<std::mutex> lock(handle->cache_mutex_);

    namespace fs = std::filesystem;
    auto ftime   = fs::last_write_time(filename);
    
    // Use robust conversion helper
    auto sctp = filesystem_time_to_system_time(ftime);

    auto entry = std::make_shared<rotation_handle::rotated_file_entry>();
    entry->filename = filename;
    entry->timestamp = sctp;
    entry->size = size;
    entry->comp_state = compression_state::idle;
    entry->handle = handle->shared_from_this();
    
    handle->rotated_files_cache_.push_back(entry);
    return entry;
}

inline void file_rotation_service::initialize_cache(rotation_handle *handle)
{
    std::lock_guard<std::mutex> lock(handle->cache_mutex_);

    namespace fs = std::filesystem;
    fs::path base_path(handle->base_filename_);
    fs::path dir = base_path.parent_path();
    if (dir.empty()) dir = ".";
    std::string base_stem = base_path.stem().string();
    std::string ext = base_path.extension().string();

    // Format: base_stem-YYYYMMDD-HHMMSS-NNN.ext[.gz|.pending]
    std::string prefix = base_stem + "-";

    try
    {
        for (const auto &entry : fs::directory_iterator(dir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();

                // Check if it matches our pattern
                if (filename.starts_with(prefix) && filename.size() > prefix.size() + MIN_TIMESTAMP_LENGTH)
                {
                    size_t date_pos = prefix.size();
                    size_t time_pos = date_pos + 9;

                    // Check for the expected extensions
                    bool matches = false;
                    if (!ext.empty()) {
                        // If original file had extension, look for that extension
                        matches = filename.ends_with(ext) || 
                                 filename.ends_with(ext + ".gz") || 
                                 filename.ends_with(ext + ".pending");
                    } else {
                        // If no extension, default to .log
                        matches = filename.ends_with(".log") || 
                                 filename.ends_with(".log.gz") || 
                                 filename.ends_with(".log.pending");
                    }

                    if (filename.size() > time_pos + 6 && filename[date_pos + 8] == '-' && filename[time_pos + 6] == '-' && matches)
                    {

                        auto ftime = fs::last_write_time(entry);
                        auto sctp = filesystem_time_to_system_time(ftime);

                        // Use stat to get file size to avoid exceptions
                        struct stat st;
                        if (::stat(entry.path().c_str(), &st) == 0)
                        {
                            auto file_entry = std::make_shared<rotation_handle::rotated_file_entry>();
                            file_entry->filename = entry.path().string();
                            file_entry->timestamp = sctp;
                            file_entry->size = static_cast<size_t>(st.st_size);
                            file_entry->comp_state = compression_state::idle;
                            file_entry->handle = handle->shared_from_this();
                            
                            handle->rotated_files_cache_.push_back(file_entry);
                        }
                        else
                        {
                            LOG(debug) << "Failed to stat file for cache initialization: " << get_error_string(errno);
                        }
                    }
                }
            }
        }
    }
    catch (const fs::filesystem_error &e)
    {
        LOG(warn) << "Failed to scan directory for cache: " << e.what();
    }

    // Sort by timestamp
    std::sort(handle->rotated_files_cache_.begin(),
              handle->rotated_files_cache_.end(),
              [](const auto &a, const auto &b) { return a->timestamp < b->timestamp; });
}

inline void file_rotation_service::handle_close(const rotation_message &msg)
{
    // Sync data to disk before closing
    if (msg.old_fd >= 0)
    {
        log_fdatasync(msg.old_fd);
        close(msg.old_fd);
    }

    // Aggregate handle metrics to global metrics before removing
    if (msg.handle)
    {
        auto &metrics = rotation_metrics::instance();
        metrics.dropped_records_total.fetch_add(msg.handle->dropped_records_.load());
        metrics.dropped_bytes_total.fetch_add(msg.handle->dropped_bytes_.load());
    }

    // Remove from active handles
    std::lock_guard<std::mutex> lock(handles_mutex_);
    handles_.erase(std::remove_if(handles_.begin(),
                                  handles_.end(),
                                  [&msg](const std::weak_ptr<rotation_handle> &wp)
                                  {
                                      if (auto sp = wp.lock()) { return sp == msg.handle; }
                                      return true; // Remove expired handles too
                                  }),
                   handles_.end());
}

inline void file_rotation_service::drain_queue()
{
    rotation_message msg;
    while (queue_.try_dequeue(msg))
    {
        if (msg.msg_type == rotation_message::CLOSE && msg.old_fd >= 0)
        {
            log_fdatasync(msg.old_fd);
            close(msg.old_fd);
        }
    }
}

inline bool file_rotation_service::compress_file_sync(std::shared_ptr<rotation_handle::rotated_file_entry> entry)
{
    const std::string& filename = entry->filename;
    std::string gz_pending = filename + ".gz.pending";
    std::string gz_final = filename + ".gz";
    
    // Clean up any leftover .pending file from previous failed attempt
    ::unlink(gz_pending.c_str());

    // Check if cancelled before starting
    if (entry->comp_state == compression_state::cancelled) {
        compression_files_cancelled_.fetch_add(1);
        return false;
    }

    // Check if source file exists before compression
    struct stat st;
    if (::stat(filename.c_str(), &st) != 0) {
        // File gone, mark as done
        entry->comp_state = compression_state::done;
        return false;
    }
    
    // Check cancellation periodically during compression
    auto cancel_checker = [entry]() -> bool {
        return entry->comp_state == compression_state::cancelled;
    };
    
    // Compress the file with cancellation support
    bool ok = slwoggy::gzip::file_to_gzip_with_cancel(filename, gz_pending, 
                                                       MZ_DEFAULT_COMPRESSION, cancel_checker);
    
    if (!ok || entry->comp_state == compression_state::cancelled) {
        if (entry->comp_state == compression_state::cancelled) {
            compression_files_cancelled_.fetch_add(1);
        } else {
            rotation_metrics::instance().compression_failures.fetch_add(1, std::memory_order_relaxed);
        }
        ::unlink(gz_pending.c_str());
        return false;
    }
    
    // Atomic rename from .pending to final .gz
    if (::rename(gz_pending.c_str(), gz_final.c_str()) != 0) {
        // If rename failed, try to clean up existing .gz and retry
        ::unlink(gz_final.c_str());
        if (::rename(gz_pending.c_str(), gz_final.c_str()) != 0) {
            // Still failed - clean up pending file and report error
            ::unlink(gz_pending.c_str());
            rotation_metrics::instance().compression_failures.fetch_add(1, std::memory_order_relaxed);
            entry->comp_state = compression_state::done;
            return false;
        }
    }
    
    // Sync directory for durability after rename
    sync_directory(filename);
    
    // Successfully compressed - delete the original uncompressed file
    ::unlink(filename.c_str());
    
    // Sync directory again after unlink to ensure deletion is durable
    sync_directory(filename);
    
    // Update entry to reflect compression
    if (auto handle = entry->handle.lock()) {
        std::lock_guard<std::mutex> lock(handle->cache_mutex_);
        entry->filename = gz_final;
        if (::stat(gz_final.c_str(), &st) == 0) {
            entry->size = st.st_size;
        }
    }
    
    entry->comp_state = compression_state::done;
    compression_files_compressed_.fetch_add(1);
    return true;
}

// Compression thread functions
inline void file_rotation_service::compression_thread_func()
{
    std::vector<std::shared_ptr<rotation_handle::rotated_file_entry>> batch;
    
    while (compression_running_.load()) {
        batch.clear();
        
        // Wait for first item
        std::shared_ptr<rotation_handle::rotated_file_entry> entry;
        compression_queue_.wait_dequeue(entry);
        
        if (!entry) continue;  // Null entry used to wake thread
        if (!compression_running_.load()) break;
        
        batch.push_back(entry);
        compression_queue_size_.fetch_sub(1);
        
        // Wait compression_delay_ while collecting more items for batching
        auto deadline = log_fast_timestamp() + compression_delay_;
        do {
            auto remaining = deadline - log_fast_timestamp();
            if (remaining <= std::chrono::milliseconds::zero()) break;
            
            // Use short timeout to check for shutdown periodically
            auto timeout = std::min(std::chrono::duration_cast<std::chrono::milliseconds>(remaining), 
                                   std::chrono::milliseconds{100});
            if (compression_queue_.wait_dequeue_timed(entry, timeout)) {
                if (entry) {
                    batch.push_back(entry);
                    compression_queue_size_.fetch_sub(1);
                    if (batch.size() >= COMPRESS_THREAD_MAX_BATCH) break;
                }
            }
        } while (compression_running_.load() && log_fast_timestamp() < deadline);
        
        // Process batch
        for (auto& e : batch) {
            compression_state expected = compression_state::queued;
            if (e->comp_state.compare_exchange_strong(expected, compression_state::compressing)) {
                compress_file_sync(e);
            }
            // If not queued anymore, it was cancelled - skip
        }
    }
    
    // Drain queue on shutdown
    drain_compression_queue();
}

inline bool file_rotation_service::enqueue_for_compression(std::shared_ptr<rotation_handle::rotated_file_entry> entry)
{
    // Check queue size
    if (compression_queue_size_.load() >= compression_queue_max_) {
        // Queue full, skip compression
        return false;
    }
    
    // Try to transition state
    compression_state expected = compression_state::idle;
    if (!entry->comp_state.compare_exchange_strong(expected, compression_state::queued)) {
        // Already queued or in progress
        return false;
    }
    
    compression_queue_.enqueue(entry);
    size_t new_size = compression_queue_size_.fetch_add(1) + 1;
    compression_files_queued_.fetch_add(1);
    
    // Update high water mark
    size_t prev_high = compression_queue_high_water_.load();
    while (new_size > prev_high && 
           !compression_queue_high_water_.compare_exchange_weak(prev_high, new_size)) {
        // Loop until we successfully update or someone else sets a higher value
    }
    
    return true;
}

inline void file_rotation_service::drain_compression_queue()
{
    std::shared_ptr<rotation_handle::rotated_file_entry> entry;
    while (compression_queue_.try_dequeue(entry)) {
        compression_queue_size_.fetch_sub(1);
        // Mark as cancelled so it won't be processed if picked up
        if (entry) {
            entry->comp_state = compression_state::cancelled;
            compression_files_cancelled_.fetch_add(1);
        }
    }
}

inline void file_rotation_service::start_compression_thread(
    std::chrono::milliseconds delay,
    size_t max_queue_size)
{
    bool expected = false;
    if (compression_running_.compare_exchange_strong(expected, true)) {
        compression_delay_ = delay;
        compression_queue_max_ = max_queue_size;
        compression_thread_ = std::thread(&file_rotation_service::compression_thread_func, this);
    }
}

inline void file_rotation_service::stop_compression_thread()
{
    compression_running_ = false;
    compression_queue_.enqueue(nullptr);  // Wake thread
    if (compression_thread_.joinable()) {
        compression_thread_.join();
    }
}

// directly in compress_file_sync via the shared_ptr<rotated_file_entry>

inline void file_rotation_service::cleanup_expired_handles()
{
    std::lock_guard<std::mutex> lock(handles_mutex_);
    handles_.erase(std::remove_if(handles_.begin(),
                                  handles_.end(),
                                  [](const std::weak_ptr<rotation_handle> &wp) { return wp.expired(); }),
                   handles_.end());
}

inline std::shared_ptr<rotation_handle> file_rotation_service::open(const std::string &filename, const rotate_policy &policy)
{
    auto handle            = std::make_shared<rotation_handle>();
    handle->base_filename_ = filename;
    handle->policy_        = policy;
    handle->service_       = this;
    handle->bytes_written_.store(0);
    handle->next_fd_ready_.store(false);
    handle->current_fd_.store(-1);
    handle->next_fd_.store(-1);

    // Open initial fd
    int fd = ::open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) { 
        throw std::runtime_error("Failed to open: " + filename + " - " + get_error_string(errno)); 
    }
    handle->current_fd_.store(fd);
    
    // Check if file is a regular file when rotation/compression is requested
    if (policy.mode != rotate_policy::kind::none || policy.compress) {
        struct stat st;
        if (fstat(fd, &st) == 0 && !S_ISREG(st.st_mode)) {
            // This is a special file (device, pipe, socket, etc.)
            // Disable rotation and compression for special files
            LOG(warn) << "File '" << filename << "' is not a regular file, disabling rotation and compression";
            handle->policy_.mode = rotate_policy::kind::none;
            handle->policy_.compress = false;
        }
    }
    
    // Get the current file size to properly handle existing files
    struct stat st;
    if (fstat(fd, &st) == 0) {
        handle->bytes_written_.store(st.st_size);
    }

    // Initialize file cache with one-time directory scan
    initialize_cache(handle.get());
    
    // Apply retention policy to pre-existing files immediately
    apply_retention_timestamped(handle.get());

    // Compute initial rotation time for time-based policies
    if (policy.mode == rotate_policy::kind::time || policy.mode == rotate_policy::kind::size_or_time)
    {
        handle->compute_next_rotation_time();
    }

    // Prepare next fd immediately (will be ready for entire rotation cycle)
    prepare_next_fd_with_retry(handle.get());

    // Track handle
    std::lock_guard<std::mutex> lock(handles_mutex_);
    handles_.push_back(handle);

    return handle;
}

} // namespace slwoggy