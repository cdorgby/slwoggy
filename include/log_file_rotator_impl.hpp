#pragma once

#include "log_file_rotator.hpp"
#include "log.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <string.h>

// macOS doesn't have fdatasync, use fsync instead
#ifdef __APPLE__
#define fdatasync fsync
#endif

namespace slwoggy {

// rotation_metrics implementation
inline void rotation_metrics::dump_metrics() const {
    LOG(info) << "Rotation Metrics:";
    LOG(info) << "  Dropped: " << dropped_records_total.load() << " records, " 
              << dropped_bytes_total.load() << " bytes";
    LOG(info) << "  Rotations: " << rotations_total.load() << " total";
    if (rotation_duration_us_count > 0) {
        auto avg_us = rotation_duration_us_sum / rotation_duration_us_count;
        LOG(info) << "  Avg rotation time: " << avg_us << " us";
    }
    LOG(info) << "  ENOSPC deletions: pending=" << enospc_deletions_pending.load()
              << " gz=" << enospc_deletions_gz.load() 
              << " raw=" << enospc_deletions_raw.load()
              << " bytes=" << enospc_deleted_bytes.load();
    LOG(info) << "  Zero-gap fallbacks: " << zero_gap_fallback_total.load();
    LOG(info) << "  Failures: compression=" << compression_failures.load()
              << " prepare_fd=" << prepare_fd_failures.load()
              << " fsync=" << fsync_failures.load();
}

// file_rotation_service implementation
inline file_rotation_service::file_rotation_service() : running_(true) {
    rotator_thread_ = std::thread(&file_rotation_service::rotator_thread_func, this);
}

inline file_rotation_service::~file_rotation_service() {
    // Signal shutdown
    running_.store(false);
    
    // Send sentinel message to wake thread
    queue_.enqueue({rotation_message::SHUTDOWN, nullptr, -1, ""});
    
    // Wait for thread to finish
    if (rotator_thread_.joinable()) {
        rotator_thread_.join();
    }
    
    // Final fdatasync for all open handles and cleanup temp files
    std::lock_guard<std::mutex> lock(handles_mutex_);
    for (auto& weak_handle : handles_) {
        if (auto handle = weak_handle.lock()) {
            int fd = handle->current_fd_.load();
            if (fd >= 0) {
                fdatasync(fd);
            }
            
            // Clean up any prepared temp file
            int next_fd = handle->next_fd_.load();
            if (next_fd >= 0) {
                close(next_fd);
                if (!handle->next_temp_filename_.empty()) {
                    unlink(handle->next_temp_filename_.c_str());
                }
            }
        }
    }
}

inline void file_rotation_service::rotator_thread_func() {
    // This thread CANNOT initiate rotations!
    // Rotation happens in two phases:
    // 1. DETECTION & FD SWAP (in writer's write() method)
    // 2. CLEANUP & PREPARATION (in this thread)
    
    while (running_.load()) {
        rotation_message msg;
        
        // Block indefinitely waiting for rotation messages from writers
        queue_.wait_dequeue(msg);
        
        // Process the message
        switch (msg.msg_type) {
        case rotation_message::ROTATE:
            handle_rotation(msg);
            break;
        case rotation_message::CLOSE:
            handle_close(msg);
            break;
        case rotation_message::SHUTDOWN:
            drain_queue();
            return;
        }
    }
}

inline void file_rotation_service::handle_rotation(const rotation_message& msg) {
    // This function handles PHASE 2 of rotation
    // The writer has already swapped to the next fd
    
    auto rotation_start = std::chrono::steady_clock::now();
    
    // Durability - fdatasync before rotation if configured
    if (msg.handle->policy_.sync_on_rotate && msg.old_fd >= 0) {
        if (fdatasync(msg.old_fd) != 0) {
            rotation_metrics::instance().fsync_failures.fetch_add(1);
        }
    }
    
    // Generate timestamped filename for rotated file
    std::string rotated_name = generate_rotated_filename(msg.handle->base_filename_);
    namespace fs = std::filesystem;
    fs::path base_path(msg.handle->base_filename_);
    fs::path rotated_path(rotated_name);
    
    if (!msg.temp_filename.empty()) {
        // Normal case: writer has swapped to temp file
        close(msg.old_fd);
        
        // Zero-gap rotation via link()+rename()
        int link_attempts = 0;
        while (link_attempts < 3) {
            if (link(base_path.c_str(), rotated_path.c_str()) == 0) {
                // Success: hard link created
                // Atomically replace current with temp (NO GAP!)
                if (rename(msg.temp_filename.c_str(), base_path.c_str()) != 0) {
                    LOG(error) << "Failed to rename temp to base: " << strerror(errno);
                }
                break;
            } else if (errno == EEXIST) {
                // Race: another rotation created this name
                LOG(info) << "Rotated filename exists, regenerating";
                rotated_name = generate_rotated_filename(msg.handle->base_filename_);
                rotated_path = rotated_name;
                link_attempts++;
            } else {
                // Other failure (cross-device, permission, etc.)
                rotation_metrics::instance().zero_gap_fallback_total.fetch_add(1);
                
                LOG(warn) << "Hard link failed, using fallback rotation with gap: " 
                          << strerror(errno);
                
                // Use two-rename sequence (HAS BRIEF GAP)
                if (rename(base_path.c_str(), rotated_path.c_str()) == 0) {
                    rename(msg.temp_filename.c_str(), base_path.c_str());
                } else if (errno == EEXIST) {
                    // Rotated name exists, retry with new name
                    rotated_name = generate_rotated_filename(msg.handle->base_filename_);
                    rotated_path = rotated_name;
                    rename(base_path.c_str(), rotated_path.c_str());
                    rename(msg.temp_filename.c_str(), base_path.c_str());
                }
                break;
            }
        }
    } else {
        // This should never happen - writer must swap fd first
        LOG(fatal) << "Rotation message without temp file - programming error!";
        abort();
    }
    
    // Directory durability: BATCH fsync (not per-mutation)
    std::string dir_path = base_path.parent_path().string();
    if (dir_path.empty()) dir_path = ".";
    
#ifdef O_DIRECTORY
    int dir_fd = ::open(dir_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
#else
    int dir_fd = ::open(dir_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (dir_fd >= 0) {
        struct stat st;
        if (fstat(dir_fd, &st) == 0 && !S_ISDIR(st.st_mode)) {
            close(dir_fd);
            dir_fd = -1;
            LOG(error) << "Parent path is not a directory";
        }
    }
#endif
    if (dir_fd >= 0) {
        fsync(dir_fd);  // Single fsync after batch
        close(dir_fd);
    } else {
        LOG(error) << "Failed to fsync directory - renames not durable!";
    }
    
    // Add rotated file to cache
    try {
        size_t file_size = fs::file_size(rotated_path);
        add_to_cache(msg.handle.get(), rotated_name, file_size);
    } catch (const fs::filesystem_error& e) {
        LOG(error) << "Failed to get file size for cache: " << e.what();
    }
    
    // Apply retention policies
    apply_retention_timestamped(msg.handle.get());
    
    // Compress if needed
    if (msg.handle->policy_.compress) {
        compress_file_async(rotated_name);
    }
    
    // Prepare next fd with retry logic
    prepare_next_fd_with_retry(msg.handle.get());
    
    // Track rotation progress
    auto rotation_end = std::chrono::steady_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
        rotation_end - rotation_start).count();
    rotation_metrics::instance().rotations_total.fetch_add(1);
    rotation_metrics::instance().rotation_duration_us_sum.fetch_add(duration_us);
    rotation_metrics::instance().rotation_duration_us_count.fetch_add(1);
}

inline std::string file_rotation_service::generate_rotated_filename(const std::string& base) {
    // Generate timestamp in UTC
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_utc;
    gmtime_r(&time_t, &tm_utc);
    
    // Format: base-YYYYMMDD-HHMMSS-NNN.log
    std::ostringstream timestamp;
    timestamp << std::setfill('0')
              << "-" << std::setw(4) << (tm_utc.tm_year + 1900)
              << std::setw(2) << (tm_utc.tm_mon + 1)
              << std::setw(2) << tm_utc.tm_mday
              << "-" << std::setw(2) << tm_utc.tm_hour
              << std::setw(2) << tm_utc.tm_min
              << std::setw(2) << tm_utc.tm_sec;
    
    // Remove .log extension if present
    std::string base_no_ext = base;
    if (base_no_ext.size() > 4 && base_no_ext.substr(base_no_ext.size() - 4) == ".log") {
        base_no_ext = base_no_ext.substr(0, base_no_ext.size() - 4);
    }
    
    // Retry on filename collision
    for (int seq = 1; seq < 10000; ++seq) {
        std::ostringstream final_name;
        final_name << base_no_ext << timestamp.str() 
                   << "-" << std::setfill('0') << std::setw(3) << seq << ".log";
        
        struct stat st;
        if (stat(final_name.str().c_str(), &st) != 0 && errno == ENOENT) {
            // File doesn't exist - use this name
            return final_name.str();
        }
    }
    
    // Fallback: add microseconds for uniqueness
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count() % 1000000;
    std::ostringstream final_name;
    final_name << base_no_ext << timestamp.str() 
               << "-" << std::setfill('0') << std::setw(6) << us << ".log";
    return final_name.str();
}

inline std::string file_rotation_service::generate_temp_filename(const std::string& base) {
    // Temp files MUST be created in the same directory as app.log
    namespace fs = std::filesystem;
    fs::path base_path(base);
    fs::path dir = base_path.parent_path();
    if (dir.empty()) {
        dir = ".";
    }
    
    // Generate unique temp name in same directory
    static std::atomic<uint64_t> temp_counter{0};
    uint64_t id = temp_counter.fetch_add(1);
    auto pid = getpid();
    auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
    
    // Format: .app.log.tmp.PID.TID.COUNTER
    std::ostringstream oss;
    oss << dir.string() << "/." << base_path.filename().string() 
        << ".tmp." << pid << "." << tid << "." << id;
    return oss.str();
}

inline void file_rotation_service::prepare_next_fd_with_retry(rotation_handle* handle) {
    static constexpr int MAX_RETRIES = 10;
    static constexpr auto INITIAL_BACKOFF = std::chrono::milliseconds(1);
    static constexpr auto MAX_BACKOFF = std::chrono::seconds(1);
    
    // Guard against overwriting an unconsumed fd
    if (handle->next_fd_.load(std::memory_order_acquire) != -1) {
        return;  // Previous fd not consumed yet
    }
    
    auto backoff = INITIAL_BACKOFF;
    int retry_count = 0;
    bool tried_cleanup = false;
    
    while (retry_count < MAX_RETRIES) {
        // Generate temp filename
        std::string temp_name = generate_temp_filename(handle->base_filename_);
        
        // Try to open new fd
        int new_fd = ::open(temp_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0644);
        if (new_fd >= 0) {
            // Success - store the new fd
            handle->next_temp_filename_ = temp_name;
            handle->next_fd_.store(new_fd, std::memory_order_release);
            handle->next_fd_ready_.store(true, std::memory_order_release);
            handle->consecutive_failures_.store(0);
            
            // Signal semaphore in case sink is waiting
            handle->next_fd_semaphore_.signal();
            return;
        }
        
        // Check if it's ENOSPC
        if (errno == ENOSPC && !tried_cleanup) {
            LOG(warn) << "ENOSPC encountered, attempting to free space";
            
            if (emergency_cleanup(handle)) {
                tried_cleanup = true;
                continue;  // Retry immediately
            }
        }
        
        // Failed - log and retry with backoff
        LOG(error) << "Failed to open rotation file " << temp_name 
                   << ": " << strerror(errno) 
                   << " (retry " << retry_count + 1 << "/" << MAX_RETRIES << ")";
        
        retry_count++;
        handle->consecutive_failures_.fetch_add(1);
        
        std::this_thread::sleep_for(backoff);
        backoff = (backoff * 2 > MAX_BACKOFF) ? MAX_BACKOFF : backoff * 2;
    }
    
    // Max retries exceeded - enter error state
    LOG(error) << "Failed to prepare next fd after " << MAX_RETRIES 
               << " attempts for " << handle->base_filename_ 
               << " - entering error state";
    
    rotation_metrics::instance().prepare_fd_failures.fetch_add(1);
    handle->in_error_state_.store(true, std::memory_order_release);
    
    // Signal semaphore to unblock any waiting writers
    handle->next_fd_semaphore_.signal();
}

inline void file_rotation_service::apply_retention_timestamped(rotation_handle* handle) {
    std::lock_guard<std::mutex> lock(handle->cache_mutex_);
    
    auto& files = handle->rotated_files_cache_;
    
    // Sort by timestamp (oldest first)
    std::sort(files.begin(), files.end(), 
              [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
    
    size_t total_bytes = 0;
    int to_keep = handle->policy_.keep_files;
    auto now = std::chrono::system_clock::now();
    
    std::vector<size_t> to_delete;
    
    for (size_t i = 0; i < files.size(); ++i) {
        bool should_delete = false;
        
        // Retention precedence: 1. keep_files, 2. max_total_bytes, 3. max_age
        if (to_keep > 0 && static_cast<int>(i) < static_cast<int>(files.size()) - to_keep) {
            should_delete = true;
        }
        else if (handle->policy_.max_total_bytes > 0 && 
                 total_bytes + files[i].size > handle->policy_.max_total_bytes) {
            should_delete = true;
        }
        else if (handle->policy_.max_age.count() > 0) {
            auto age = now - files[i].timestamp;
            if (age > handle->policy_.max_age) {
                should_delete = true;
            }
        }
        
        if (should_delete) {
            to_delete.push_back(i);
            unlink(files[i].filename.c_str());
            LOG(debug) << "Deleted old log file: " << files[i].filename;
        } else {
            total_bytes += files[i].size;
        }
    }
    
    // Remove deleted entries from cache (reverse order)
    for (auto it = to_delete.rbegin(); it != to_delete.rend(); ++it) {
        files.erase(files.begin() + *it);
    }
}

inline bool file_rotation_service::emergency_cleanup(rotation_handle* handle) {
    // ENOSPC last-resort cleanup - VIOLATES retention policies!
    std::lock_guard<std::mutex> lock(handle->cache_mutex_);
    auto& files = handle->rotated_files_cache_;
    auto& metrics = rotation_metrics::instance();
    
    if (files.empty()) {
        return false;
    }
    
    // Try deleting .pending files first
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i].filename.find(".pending") != std::string::npos) {
            LOG(info) << "ENOSPC: Deleting pending file: " << files[i].filename;
            metrics.enospc_deletions_pending.fetch_add(1);
            metrics.enospc_deleted_bytes.fetch_add(files[i].size);
            unlink(files[i].filename.c_str());
            files.erase(files.begin() + i);
            return true;
        }
    }
    
    // Then try .gz files
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i].filename.find(".gz") != std::string::npos) {
            LOG(info) << "ENOSPC: Deleting compressed file: " << files[i].filename;
            metrics.enospc_deletions_gz.fetch_add(1);
            metrics.enospc_deleted_bytes.fetch_add(files[i].size);
            unlink(files[i].filename.c_str());
            files.erase(files.begin() + i);
            return true;
        }
    }
    
    // Finally delete oldest raw log
    if (!files.empty()) {
        LOG(warn) << "ENOSPC: Deleting log (violates retention): " << files[0].filename;
        metrics.enospc_deletions_raw.fetch_add(1);
        metrics.enospc_deleted_bytes.fetch_add(files[0].size);
        unlink(files[0].filename.c_str());
        files.erase(files.begin());
        return true;
    }
    
    return false;
}

inline void file_rotation_service::add_to_cache(rotation_handle* handle, const std::string& filename, size_t size) {
    std::lock_guard<std::mutex> lock(handle->cache_mutex_);
    
    namespace fs = std::filesystem;
    auto ftime = fs::last_write_time(filename);
    
    // Convert file_time to system_clock time_point
    // Note: This is platform-specific and may need adjustment
    auto now_sys = std::chrono::system_clock::now();
    auto now_file = fs::file_time_type::clock::now();
    auto diff = now_sys.time_since_epoch() - std::chrono::duration_cast<std::chrono::system_clock::duration>(now_file.time_since_epoch());
    auto sctp = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(ftime.time_since_epoch()) + diff);
    
    handle->rotated_files_cache_.push_back({
        filename,
        sctp,
        size
    });
}

inline void file_rotation_service::initialize_cache(rotation_handle* handle) {
    std::lock_guard<std::mutex> lock(handle->cache_mutex_);
    
    namespace fs = std::filesystem;
    fs::path base_path(handle->base_filename_);
    fs::path dir = base_path.parent_path();
    if (dir.empty()) dir = ".";
    std::string base_stem = base_path.stem().string();
    
    // Format: base_stem-YYYYMMDD-HHMMSS-NNN.log[.gz|.pending]
    std::string prefix = base_stem + "-";
    
    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                
                // Check if it matches our pattern
                if (filename.starts_with(prefix) && filename.size() > prefix.size() + 19) {
                    size_t date_pos = prefix.size();
                    size_t time_pos = date_pos + 9;
                    
                    if (filename.size() > time_pos + 6 && 
                        filename[date_pos + 8] == '-' && 
                        filename[time_pos + 6] == '-' &&
                        (filename.ends_with(".log") || 
                         filename.ends_with(".log.gz") || 
                         filename.ends_with(".log.pending"))) {
                        
                        auto ftime = fs::last_write_time(entry);
                        auto now_sys = std::chrono::system_clock::now();
                        auto now_file = fs::file_time_type::clock::now();
                        auto diff = now_sys.time_since_epoch() - std::chrono::duration_cast<std::chrono::system_clock::duration>(now_file.time_since_epoch());
                        auto sctp = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(ftime.time_since_epoch()) + diff);
                        
                        handle->rotated_files_cache_.push_back({
                            entry.path().string(),
                            sctp,
                            static_cast<size_t>(entry.file_size())
                        });
                    }
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        LOG(warn) << "Failed to scan directory for cache: " << e.what();
    }
    
    // Sort by timestamp
    std::sort(handle->rotated_files_cache_.begin(), 
              handle->rotated_files_cache_.end(),
              [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
}

inline void file_rotation_service::handle_close(const rotation_message& msg) {
    // Sync data to disk before closing
    if (msg.old_fd >= 0) {
        fdatasync(msg.old_fd);
        close(msg.old_fd);
    }
    
    // Remove from active handles
    std::lock_guard<std::mutex> lock(handles_mutex_);
    handles_.erase(
        std::remove_if(handles_.begin(), handles_.end(),
            [&msg](const std::weak_ptr<rotation_handle>& wp) {
                if (auto sp = wp.lock()) {
                    return sp == msg.handle;
                }
                return true;  // Remove expired handles too
            }),
        handles_.end()
    );
}

inline void file_rotation_service::drain_queue() {
    rotation_message msg;
    while (queue_.try_dequeue(msg)) {
        if (msg.msg_type == rotation_message::CLOSE && msg.old_fd >= 0) {
            fdatasync(msg.old_fd);
            close(msg.old_fd);
        }
    }
}

inline void file_rotation_service::compress_file_async(const std::string& filename) {
    // TODO: Implement compression with .pending files
    // For now, just log
    LOG(debug) << "Compression requested for " << filename << " (not implemented)";
}

inline void file_rotation_service::update_cache_entry(const std::string& old_name, const std::string& new_name) {
    // TODO: Implement cache update during compression
}

inline void file_rotation_service::cleanup_expired_handles() {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    handles_.erase(
        std::remove_if(handles_.begin(), handles_.end(),
            [](const std::weak_ptr<rotation_handle>& wp) {
                return wp.expired();
            }),
        handles_.end()
    );
}

inline std::shared_ptr<rotation_handle> file_rotation_service::open(const std::string& filename, const rotate_policy& policy) {
    auto handle = std::make_shared<rotation_handle>();
    handle->base_filename_ = filename;
    handle->policy_ = policy;
    handle->service_ = this;
    handle->bytes_written_.store(0);
    handle->next_fd_ready_.store(false);
    handle->current_fd_.store(-1);
    handle->next_fd_.store(-1);
    
    // Open initial fd
    int fd = ::open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) {
        throw std::runtime_error("Failed to open: " + filename + " - " + strerror(errno));
    }
    handle->current_fd_.store(fd);
    
    // Initialize file cache with one-time directory scan
    initialize_cache(handle.get());
    
    // Compute initial rotation time for time-based policies
    if (policy.mode == rotate_policy::kind::time || 
        policy.mode == rotate_policy::kind::size_or_time) {
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