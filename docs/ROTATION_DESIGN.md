# File Rotation Service Design

## Overview

A centralized file rotation service that manages rotation for all file writers. Writers get a handle/token that provides file descriptors and manages rotation transparently while maintaining 2M msg/sec throughput.

### Key Design Decisions

- **Timestamped rotated files**: Instead of cascading renames (.1 → .2 → .3), use timestamps (app-20250117-143022-001.log)
- **O(1) rotation**: No rename chains, just rename current to timestamped name
- **Two-phase rotation process**:
  - **Phase 1 (Writer in write())**: Detects rotation needed, swaps to next fd, sends message to rotator
  - **Phase 2 (Rotator thread)**: Renames old file, applies retention, prepares next fd
- **No timer-initiated rotation**: Rotator thread CANNOT initiate - only processes writer messages
- **No quiet-period rotation**: Time rotation only happens on write activity (architectural constraint)
- **UTC-only**: Simplify time handling, avoid DST/timezone complexity
- **ENOSPC handling**: Last-resort deletion that MAY VIOLATE retention policies (dangerous!)
- **Proper clock usage**:
  - `system_clock` for calendar-based rotation checks
  - `steady_clock` for retry backoff timing only

## Implementation Requirements

- **C++20 (partial)**: 
  - `std::filesystem` (for path manipulation only)
  - `std::chrono::clock_cast` (NOT universally available yet)
  - UTC/calendar support (toolchain-dependent)
- **POSIX syscalls for atomicity**: 
  - `rename(2)` for atomic replacement (NOT std::filesystem::rename)
  - `link(2)` for hard links
  - `fsync`, `fdatasync`, `O_CLOEXEC` (POSIX)
  - `O_DIRECTORY` (Linux-specific)
- **Atomic operations + semaphore**: Fast path uses atomics, slow path blocks on semaphore (NOT lock-free)
- **BlockingConcurrentQueue**: For proper wait semantics (not ConcurrentQueue)

## Critical Implementation Notes

### Clock Domain Correctness
- **UTC calculations**: Use `system_clock` for all time calculations
- **file_time conversion**: Use C++20 `clock_cast` when available, fallback to manual conversion
- **Calendar vs intervals**: Use `system_clock` for calendar events, `steady_clock` only for retry backoff

### File Descriptor Management  
- **O_CLOEXEC required**: All `open()` calls must include `O_CLOEXEC` to prevent fd leaks to child processes
- **Directory durability**: After renames, must `fsync()` (not `fdatasync`) the directory fd for metadata durability
  - Linux: Can use `O_DIRECTORY` flag
  - POSIX portable: Open as regular file, check with `fstat()` for `S_ISDIR`
- **Partial writes**: Must handle `EINTR` and partial writes in a loop

### Data Integrity Trade-offs
- **SILENT DATA LOSS WARNING**: In error state, `write()` returns success but drops data. This violates POSIX semantics. **MUST monitor metrics for data loss!**
- **Cache coherency issues**: 
  - Cache tracks filename + mtime + size
  - Compression renames change filename (.log → .pending → .gz)
  - Cache entries become stale, retention may try to delete non-existent files
  - Cache entries must be updated during compression transitions
- **Proactive fd preparation**: Next fd prepared immediately after rotation completes, ready for entire cycle

## Core Components

### 1. Rotation Policy Structure

```cpp
struct rotate_policy {
    enum class kind { none, size, time, size_or_time };
    kind mode = kind::none;

    // size policy
    uint64_t max_bytes = 0;        // e.g. 256_MB

    // time policy
    std::chrono::minutes every = std::chrono::minutes{0}; // 24h for daily
    std::chrono::minutes at = std::chrono::minutes{0};    // cutover time for daily

    // retention (NOT GUARANTEED - violated on ENOSPC!)
    // Precedence when multiple constraints apply:
    // 1. keep_files (delete oldest beyond count)
    // 2. max_total_bytes (delete oldest to stay under limit)  
    // 3. max_age (delete anything older)
    // BUT: ENOSPC overrides ALL constraints!
    int keep_files = 5;            // 0 => no limit (but ENOSPC deletes anyway!)
    std::chrono::hours max_age{0}; // 0 => ignore (but ENOSPC deletes anyway!)
    uint64_t max_total_bytes = 0;  // 0 => ignore (but ENOSPC deletes anyway!)

    // post-rotate
    bool compress = false;
    bool sync_on_rotate = false;   // fdatasync before rotation for data integrity
    
    // error handling
    int max_retries = 10;          // Max attempts before entering error state
};
```

### 2. Updated file_writer (log_writers.hpp)

```cpp
class file_writer {
private:
    // Existing members...
    rotate_policy policy_;
    std::shared_ptr<rotation_handle> rotation_handle_;
    mutable int fd_;  // Mutable for rotation
    
public:
    // Updated constructor with optional policy
    file_writer(const std::string &filename, rotate_policy policy = rotate_policy{})
        : filename_(filename), policy_(policy), fd_(-1), close_fd_(true)
    {
        if (policy_.mode != rotate_policy::kind::none) {
            // Lazy init of rotation service on first use
            auto& rotator = file_rotation_service::instance();
            rotation_handle_ = rotator.open(filename, policy);
            fd_ = rotation_handle_->get_current_fd();
        } else {
            // Normal non-rotating file
            fd_ = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
            if (fd_ < 0) throw std::runtime_error("Failed to open: " + filename);
        }
    }
    
    ssize_t write(const char *data, size_t len) const {
        // Check for rotation
        if (rotation_handle_ && rotation_handle_->should_rotate(len)) {
            // This either returns immediately (next fd ready)
            // or blocks if rotator thread is behind
            int new_fd = rotation_handle_->get_next_fd();
            if (new_fd == -1) {
                // Handle is in error state - discard the write
                // WARNING: This returns success but drops data! 
                // MUST: Track dropped writes for observability
                rotation_handle_->dropped_records_.fetch_add(1, std::memory_order_relaxed);
                rotation_handle_->dropped_bytes_.fetch_add(len, std::memory_order_relaxed);
                return len;  // Pretend we wrote it (POSIX violation!)
            }
            fd_ = new_fd;
        }
        
        // Handle partial writes and EINTR
        size_t total_written = 0;
        while (total_written < len) {
            ssize_t written = ::write(fd_, data + total_written, len - total_written);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;  // Retry on interrupt
                }
                // Return error to caller (no I/O in hot path)
                return -1;
            }
            total_written += written;
        }
        
        if (rotation_handle_ && total_written > 0) {
            rotation_handle_->add_bytes_written(total_written);
        }
        return total_written;
    }
    
    ~file_writer() {
        if (rotation_handle_) {
            rotation_handle_->close();  // Tells rotator to fdatasync
        } else if (close_fd_ && fd_ >= 0) {
            close(fd_);
        }
    }
};
```

### 3. Rotation Handle (log_file_rotator.hpp)

```cpp
// Handle/token that sinks use
// MUST: Inherit from enable_shared_from_this for lifetime management
class rotation_handle : public std::enable_shared_from_this<rotation_handle> {
private:
    friend class file_rotation_service;
    
    // Current state (accessed by sink)
    std::atomic<int> current_fd_;
    std::atomic<size_t> bytes_written_;
    std::atomic<bool> next_fd_ready_;
    
    // Next fd (prepared by rotator)
    std::atomic<int> next_fd_;
    std::string next_temp_filename_;
    moodycamel::LightweightSemaphore next_fd_semaphore_;
    
    // Error state
    std::atomic<bool> in_error_state_{false};
    std::atomic<int> consecutive_failures_{0};
    
    // MUST: Metrics for observability
    std::atomic<uint64_t> dropped_records_{0};
    std::atomic<uint64_t> dropped_bytes_{0};
    
    // Policy and metadata
    rotate_policy policy_;
    std::string base_filename_;
    std::chrono::system_clock::time_point next_rotation_time_;  // For calendar-based rotation
    std::chrono::steady_clock::time_point last_backoff_;        // For retry timing only
    
    // Cached list of rotated files (avoids O(N) directory scan per rotation)
    struct rotated_file_entry {
        std::string filename;
        std::chrono::system_clock::time_point timestamp;
        size_t size;
    };
    std::vector<rotated_file_entry> rotated_files_cache_;
    std::mutex cache_mutex_;  // Protects cache updates
    
    // Back-reference to service
    file_rotation_service* service_;
    
public:
    int get_current_fd() const { 
        return current_fd_.load(std::memory_order_acquire); 
    }
    
    bool should_rotate(size_t next_write_size) const {
        // Don't attempt rotation if in error state
        if (in_error_state_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Writers check BOTH size and time policies during writes
        // This works because the writer owns the fd and can swap it
        switch (policy_.mode) {
        case rotate_policy::kind::size:
            return (bytes_written_.load() + next_write_size) > policy_.max_bytes;
        case rotate_policy::kind::time:
            return std::chrono::system_clock::now() >= next_rotation_time_;
        case rotate_policy::kind::size_or_time:
            return should_rotate_size(next_write_size) || 
                   (std::chrono::system_clock::now() >= next_rotation_time_);
        default:
            return false;
        }
    }
    
    void compute_next_rotation_time() {
        // MUST: Apply policy.at offset; if unset, treat as 00:00 UTC
        auto now = std::chrono::system_clock::now();
        
        if (policy_.mode == rotate_policy::kind::time || 
            policy_.mode == rotate_policy::kind::size_or_time) {
            
            // Example: Daily rotation at specified time (or 00:00 UTC if not set)
            if (policy_.every == std::chrono::hours{24}) {
                // Use C++20 calendar support (portable)
                auto dp = std::chrono::floor<std::chrono::days>(now);
                auto cutover = dp + policy_.at;  // MUST apply offset (defaults to 00:00)
                
                // If we're past today's cutover, move to tomorrow
                if (now >= cutover) {
                    next_rotation_time_ = cutover + std::chrono::days{1};
                } else {
                    next_rotation_time_ = cutover;
                }
            } else {
                // For non-daily intervals, just add the interval
                next_rotation_time_ = now + policy_.every;
            }
        }
    }
    
    int get_next_fd() {
        // Check error state first
        if (in_error_state_.load(std::memory_order_acquire)) {
            return -1;  // Signal writer to discard
        }
        
        // Loop instead of recursion to avoid stack issues
        while (true) {
            // Fast path - next fd is ready
            if (next_fd_ready_.load(std::memory_order_acquire)) {
                int new_fd = next_fd_.exchange(-1, std::memory_order_acq_rel);
                if (new_fd != -1) {
                    int old_fd = current_fd_.exchange(new_fd);
                    bytes_written_.store(0);
                    next_fd_ready_.store(false);
                    
                    // Notify rotator thread (must pass shared_ptr for lifetime)
                    service_->enqueue_rotation(shared_from_this(), old_fd, next_temp_filename_);
                    return new_fd;
                }
            }
            
            // Slow path - wait for rotator to prepare next fd
            // This blocks, giving the system a breather
            next_fd_semaphore_.wait();
            
            // Check if we entered error state while waiting
            if (in_error_state_.load(std::memory_order_acquire)) {
                return -1;
            }
        }
    }
    
    void add_bytes_written(size_t bytes) {
        bytes_written_.fetch_add(bytes, std::memory_order_relaxed);
    }
    
    void close() {
        // MUST: Pass shared_ptr to ensure lifetime through message processing
        service_->enqueue_close(shared_from_this(), current_fd_.load());
    }
};
```

### 4. Rotation Metrics (Critical for Observability)

```cpp
// MUST: Track all rotation metrics for observability
struct rotation_metrics {
    // Dropped writes (POSIX violation detection)
    std::atomic<uint64_t> dropped_records_total{0};
    std::atomic<uint64_t> dropped_bytes_total{0};
    
    // Rotation progress
    std::atomic<uint64_t> rotations_total{0};
    std::atomic<uint64_t> rotation_duration_us_sum{0};  // For EWMA calculation
    std::atomic<uint64_t> rotation_duration_us_count{0};
    
    // ENOSPC auto-deletions (retention violations)
    std::atomic<uint64_t> enospc_deletions_pending{0};
    std::atomic<uint64_t> enospc_deletions_gz{0};
    std::atomic<uint64_t> enospc_deletions_raw{0};
    std::atomic<uint64_t> enospc_deleted_bytes{0};
    
    // Zero-gap fallback frequency
    std::atomic<uint64_t> zero_gap_fallback_total{0};
    
    // Additional useful metrics
    std::atomic<uint64_t> compression_failures{0};
    std::atomic<uint64_t> prepare_fd_failures{0};
    std::atomic<uint64_t> fsync_failures{0};
    
    static rotation_metrics& instance() {
        static rotation_metrics metrics;
        return metrics;
    }
    
    void dump_metrics() const {
        LOG(info) << "Rotation Metrics:";
        LOG(info) << "  Dropped: " << dropped_records_total << " records, " 
                  << dropped_bytes_total << " bytes";
        LOG(info) << "  Rotations: " << rotations_total << " total";
        if (rotation_duration_us_count > 0) {
            auto avg_us = rotation_duration_us_sum / rotation_duration_us_count;
            LOG(info) << "  Avg rotation time: " << avg_us << " us";
        }
        LOG(info) << "  ENOSPC deletions: pending=" << enospc_deletions_pending
                  << " gz=" << enospc_deletions_gz 
                  << " raw=" << enospc_deletions_raw
                  << " bytes=" << enospc_deleted_bytes;
        LOG(info) << "  Zero-gap fallbacks: " << zero_gap_fallback_total;
        LOG(info) << "  Failures: compression=" << compression_failures
                  << " prepare_fd=" << prepare_fd_failures
                  << " fsync=" << fsync_failures;
    }
};
```

### 5. File Rotation Service (Singleton)

```cpp
// Singleton rotation service
class file_rotation_service {
private:
    struct rotation_message {
        enum type { ROTATE, CLOSE, SHUTDOWN };
        type msg_type;
        std::shared_ptr<rotation_handle> handle;  // MUST hold shared lifetime token
        int old_fd;
        std::string temp_filename;
    };
    
    // Message queue for rotator thread (blocking for proper wait semantics)
    moodycamel::BlockingConcurrentQueue<rotation_message> queue_;
    
    // Active handles
    std::mutex handles_mutex_;
    std::vector<std::weak_ptr<rotation_handle>> handles_;
    
    // Rotator thread
    std::thread rotator_thread_;
    std::atomic<bool> running_;
    
    file_rotation_service() : running_(true) {
        rotator_thread_ = std::thread(&file_rotation_service::rotator_thread_func, this);
    }
    
    void rotator_thread_func() {
        // CRITICAL: This thread CANNOT initiate rotations!
        // 
        // Rotation happens in two phases:
        // 1. DETECTION & FD SWAP (in writer's write() method):
        //    - Writer checks should_rotate()
        //    - Writer swaps to next fd immediately
        //    - Writer sends message to this thread
        // 
        // 2. CLEANUP & PREPARATION (in this thread):
        //    - Rename old file to timestamped name
        //    - Apply retention policies
        //    - Prepare next fd for future rotation
        //
        // There are NO timers, NO deadlines, NO timed waits in this thread.
        
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
    
    // process_time_rotations() function deleted - timer-initiated rotation is
    // architecturally impossible since the writer owns the fd
    
    void handle_rotation(const rotation_message& msg) {
        // This function handles PHASE 2 of rotation (cleanup & preparation).
        // By the time we get here:
        // - The writer has already detected rotation is needed (Phase 1)
        // - The writer has already swapped to the next fd
        // - The writer is continuing to write to the new fd
        // - We're responsible for cleaning up the old file
        //
        // MUST: Messages hold shared_ptr to prevent use-after-free
        
        // Track rotation timing
        auto rotation_start = std::chrono::steady_clock::now();
        
        // MUST: Durability - fdatasync before rotation if configured
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
            // Normal case: writer has swapped to temp file, we clean up old fd
            close(msg.old_fd);
            
            // Zero-gap rotation via link()+rename()
            // PRECONDITION: Same directory & filesystem required for hard links
            
            // Step 1: Try to create hard link to current file (preserves it)
            // Retry on EEXIST with new name
            int link_attempts = 0;
            while (link_attempts < 3) {
                if (link(base_path.c_str(), rotated_path.c_str()) == 0) {
                    // Success: hard link created
                    // Step 2: Atomically replace current with temp (NO GAP!)
                    // MUST use POSIX rename(2) for atomic replacement
                    if (rename(msg.temp_filename.c_str(), base_path.c_str()) != 0) {
                        LOG(error) << "Failed to rename temp to base: " << strerror(errno);
                    }
                    break;
                } else if (errno == EEXIST) {
                    // Race: another rotation created this name, retry with new name
                    LOG(info) << "Rotated filename exists, regenerating";
                    rotated_name = generate_rotated_filename(msg.handle->base_filename_);
                    rotated_path = rotated_name;
                    link_attempts++;
                } else {
                    // Other failure (cross-device, permission, etc.)
                    // MUST: Track zero-gap fallback frequency
                    rotation_metrics::instance().zero_gap_fallback_total.fetch_add(1);
                    
                    // Use two-rename sequence (HAS BRIEF GAP)
                    LOG(warn) << "Hard link failed, using fallback rotation with gap: " 
                              << strerror(errno);
                    
                    // MUST use POSIX rename(2) for atomic operations
                    // Step 1: Rename current to rotated (GAP STARTS)
                    if (rename(base_path.c_str(), rotated_path.c_str()) == 0) {
                        // Step 2: Rename temp to current (GAP ENDS)
                        rename(msg.temp_filename.c_str(), base_path.c_str());
                    } else if (errno == EEXIST) {
                        // Rotated name exists, retry with new name
                        rotated_name = generate_rotated_filename(msg.handle->base_filename_);
                        rotated_path = rotated_name;
                        // Retry the two-rename with new name
                        rename(base_path.c_str(), rotated_path.c_str());
                        rename(msg.temp_filename.c_str(), base_path.c_str());
                    }
                    break;
                }
            }
        } else {
            // THIS CODE PATH SHOULD NEVER BE REACHED!
            // We should always have a temp filename because the writer
            // must have swapped to a new fd before sending this message.
            
            LOG(fatal) << "Rotation message without temp file - programming error!";
            LOG(fatal) << "Writer must swap fd before sending rotation message.";
            abort();  // This is a programming error, not a runtime condition
        }
        
        // Directory durability: BATCH fsync (not per-mutation)
        // This implementation does a SINGLE fsync after all renames.
        // Promise: Final state is durable after function returns.
        // Risk: Crash between mutations leaves temporary inconsistency.
        //
        // Alternative (not implemented): Per-mutation durability would
        // require fsync(dir_fd) after EACH link/rename/unlink operation.
        //
        // Must use fsync() not fdatasync() for directory metadata
        #ifdef O_DIRECTORY  // Linux-specific
        int dir_fd = open(base_path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        #else  // POSIX portable path
        int dir_fd = open(base_path.parent_path().c_str(), O_RDONLY | O_CLOEXEC);
        if (dir_fd >= 0) {
            // MUST: Verify it's actually a directory
            struct stat st;
            if (fstat(dir_fd, &st) == 0 && !S_ISDIR(st.st_mode)) {
                close(dir_fd);
                dir_fd = -1;
                LOG(error) << "Parent path is not a directory";
            }
        }
        #endif
        if (dir_fd >= 0) {
            fsync(dir_fd);  // Single fsync after batch (not per-mutation)
            close(dir_fd);
        } else {
            LOG(error) << "Failed to fsync directory - renames not durable!";
        }
        
        // Add rotated file to cache (O(1) update)
        try {
            size_t file_size = fs::file_size(rotated_path);
            add_to_cache(msg.handle, rotated_name, file_size);
        } catch (const fs::filesystem_error& e) {
            LOG(error) << "Failed to get file size for cache: " << e.what();
        }
        
        // Apply retention policies (uses cache, no directory scan)
        apply_retention_timestamped(msg.handle);
        
        // Compress if needed (async with .pending files)
        if (msg.handle->policy_.compress) {
            compress_file_async(rotated_name);
        }
        
        // Prepare next fd with retry logic
        prepare_next_fd_with_retry(msg.handle);
        
        // MUST: Track rotation progress
        auto rotation_end = std::chrono::steady_clock::now();
        auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
            rotation_end - rotation_start).count();
        rotation_metrics::instance().rotations_total.fetch_add(1);
        rotation_metrics::instance().rotation_duration_us_sum.fetch_add(duration_us);
        rotation_metrics::instance().rotation_duration_us_count.fetch_add(1);
    }
    
    std::string generate_rotated_filename(const std::string& base) {
        // Generate timestamp in UTC
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_utc;
        gmtime_r(&time_t, &tm_utc);
        
        // Format: base-YYYYMMDD-HHMMSS-NNN.log
        char timestamp[32];
        snprintf(timestamp, sizeof(timestamp), "-%04d%02d%02d-%02d%02d%02d",
                 tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                 tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
        
        // Remove .log extension if present, will re-add
        std::string base_no_ext = base;
        if (base_no_ext.size() > 4 && base_no_ext.substr(base_no_ext.size() - 4) == ".log") {
            base_no_ext = base_no_ext.substr(0, base_no_ext.size() - 4);
        }
        
        // MUST: Retry on filename collision
        // Start with sequence 001 and increment until we find an unused name
        for (int seq = 1; seq < 10000; ++seq) {
            char final_name[256];
            snprintf(final_name, sizeof(final_name), "%s%s-%03d.log", 
                     base_no_ext.c_str(), timestamp, seq);
            
            // Check if file exists using stat (faster than open)
            struct stat st;
            if (stat(final_name, &st) != 0 && errno == ENOENT) {
                // File doesn't exist - we can use this name
                return final_name;
            }
            // File exists or other error - try next sequence number
        }
        
        // If we exhausted all sequence numbers (extremely unlikely),
        // fall back to adding microseconds for uniqueness
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count() % 1000000;
        char final_name[256];
        snprintf(final_name, sizeof(final_name), "%s%s-%06ld.log", 
                 base_no_ext.c_str(), timestamp, us);
        return final_name;
    }
    
    std::string generate_temp_filename(const std::string& base) {
        // MUST: Temp files MUST be created in the same directory as app.log
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
        auto tid = std::this_thread::get_id();
        
        // Format: .app.log.tmp.PID.TID.COUNTER
        std::ostringstream oss;
        oss << dir / ("." + base_path.filename().string() + ".tmp."
                     + std::to_string(pid) + "."
                     + std::to_string(std::hash<std::thread::id>{}(tid)) + "."
                     + std::to_string(id));
        return oss.str();
    }
    
    void apply_retention_timestamped(rotation_handle* handle) {
        std::lock_guard<std::mutex> lock(handle->cache_mutex_);
        
        // Use cached list instead of directory scan (O(1) vs O(N))
        auto& files = handle->rotated_files_cache_;
        
        // Already sorted by timestamp from incremental updates
        // But ensure it's sorted (defensive programming)
        std::sort(files.begin(), files.end(), 
                  [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
        
        // Apply retention policies
        size_t total_bytes = 0;
        int to_keep = handle->policy_.keep_files;
        auto now = std::chrono::system_clock::now();
        
        // Mark files for deletion
        std::vector<size_t> to_delete;
        
        // IMPORTANT: Files are sorted oldest-first, so we delete from the beginning
        // to keep the newest files (at the end of the vector)
        for (size_t i = 0; i < files.size(); ++i) {
            bool should_delete = false;
            
            // MUST: Retention precedence when multiple constraints conflict
            // Order: 1. keep_files, 2. max_total_bytes, 3. max_age
            // Precedence 1: File count (keep newest N files)
            if (to_keep > 0 && i < files.size() - to_keep) {
                should_delete = true;
            }
            // Precedence 2: Total size (only if not already deleted by count)
            else if (handle->policy_.max_total_bytes > 0 && 
                     total_bytes + files[i].size > handle->policy_.max_total_bytes) {
                should_delete = true;
            }
            // Precedence 3: Age (only if not already deleted by count or size)
            else if (handle->policy_.max_age.count() > 0) {
                auto age = now - files[i].timestamp;
                if (age > handle->policy_.max_age) {
                    should_delete = true;
                }
            }
            
            if (should_delete) {
                to_delete.push_back(i);
                std::filesystem::remove(files[i].filename);
                LOG(debug) << "Deleted old log file: " << files[i].filename;
            } else {
                total_bytes += files[i].size;
            }
        }
        
        // Remove deleted entries from cache (reverse order to preserve indices)
        for (auto it = to_delete.rbegin(); it != to_delete.rend(); ++it) {
            files.erase(files.begin() + *it);
        }
    }
    
    void add_to_cache(rotation_handle* handle, const std::string& filename, size_t size) {
        std::lock_guard<std::mutex> lock(handle->cache_mutex_);
        
        // Use file's actual modification time, not current time
        namespace fs = std::filesystem;
        auto ftime = fs::last_write_time(filename);
        // Portable C++20 conversion using clock_cast
        auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        
        handle->rotated_files_cache_.push_back({
            filename,
            sctp,
            size
        });
    }
    
    void initialize_cache(rotation_handle* handle) {
        // One-time directory scan on handle creation (C++20 filesystem)
        std::lock_guard<std::mutex> lock(handle->cache_mutex_);
        
        namespace fs = std::filesystem;
        fs::path base_path(handle->base_filename_);
        fs::path dir = base_path.parent_path();
        std::string base_stem = base_path.stem().string();
        
        // Fast string-based matching instead of regex
        // Format: base_stem-YYYYMMDD-HHMMSS-NNN.log[.gz|.pending]
        std::string prefix = base_stem + "-";
        
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                
                // Check prefix and suffix (much faster than regex)
                if (filename.starts_with(prefix) && filename.size() > prefix.size() + 19) {
                    // Verify it's our format: prefix + 8 digits + "-" + 6 digits + "-" + 3 digits + ".log"
                    size_t date_pos = prefix.size();
                    size_t time_pos = date_pos + 9;  // 8 digits + dash
                    size_t seq_pos = time_pos + 7;   // 6 digits + dash
                    
                    if (filename[date_pos + 8] == '-' && 
                        filename[time_pos + 6] == '-' &&
                        (filename.ends_with(".log") || 
                         filename.ends_with(".log.gz") || 
                         filename.ends_with(".log.pending"))) {
                    // Portable C++20 conversion using clock_cast
                    auto ftime = fs::last_write_time(entry);
                    auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                    
                    handle->rotated_files_cache_.push_back({
                        entry.path().string(),
                        sctp,
                        static_cast<size_t>(entry.file_size())
                    });
                }
            }
        }
        
        // Sort by timestamp
        std::sort(handle->rotated_files_cache_.begin(), 
                  handle->rotated_files_cache_.end(),
                  [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
    }
    
    void handle_close(const rotation_message& msg) {
        // Sync data to disk before closing
        fdatasync(msg.old_fd);
        close(msg.old_fd);
        
        // Remove from active handles
        cleanup_handle(msg.handle);
    }
    
    void prepare_next_fd_with_retry(rotation_handle* handle) {
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
            
            // Try to open new fd (with O_CLOEXEC to prevent fd leaks)
            int new_fd = open(temp_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0644);
            if (new_fd >= 0) {
                // Success - store the new fd
                handle->next_temp_filename_ = temp_name;
                handle->next_fd_.store(new_fd, std::memory_order_release);
                handle->next_fd_ready_.store(true, std::memory_order_release);
                handle->consecutive_failures_.store(0);  // Reset failure count
                
                // Signal semaphore in case sink is waiting
                handle->next_fd_semaphore_.signal();
                return;
            }
            
            // Check if it's ENOSPC and we haven't tried cleanup yet
            if (errno == ENOSPC && !tried_cleanup) {
                LOG(warn) << "ENOSPC encountered, attempting to free space by deleting old logs";
                
                // Try to free space by deleting old files
                if (emergency_cleanup(handle)) {
                    tried_cleanup = true;
                    // Retry immediately without incrementing retry count
                    continue;
                }
            }
            
            // Failed - log and retry with backoff
            LOG(error) << "Failed to open rotation file " << temp_name 
                       << ": " << strerror(errno) 
                       << " (retry " << retry_count + 1 << "/" << MAX_RETRIES << ")";
            
            retry_count++;
            handle->consecutive_failures_.fetch_add(1);
            
            // Sleep with exponential backoff (using steady_clock internally via sleep_for)
            std::this_thread::sleep_for(backoff);
            backoff = std::min(backoff * 2, MAX_BACKOFF);
        }
        
        // Max retries exceeded - enter error state
        LOG(error) << "Failed to prepare next fd after " << MAX_RETRIES 
                   << " attempts for " << handle->base_filename_ 
                   << " - entering error state";
        
        // MUST: Track prepare failures
        rotation_metrics::instance().prepare_fd_failures.fetch_add(1);
        handle->in_error_state_.store(true, std::memory_order_release);
        
        // Signal semaphore to unblock any waiting writers
        // They will check error state and return -1
        handle->next_fd_semaphore_.signal();
    }
    
    bool emergency_cleanup(rotation_handle* handle) {
        // ENOSPC last-resort cleanup
        // DANGER: This VIOLATES retention policies and should be disabled in 
        // environments with compliance/forensic requirements!
        // Consider failing hard instead of deleting protected logs
        
        std::lock_guard<std::mutex> lock(handle->cache_mutex_);
        auto& files = handle->rotated_files_cache_;
        
        if (files.empty()) {
            return false;
        }
        
        // Already sorted by timestamp (oldest first)
        
        // MUST: Track ENOSPC auto-deletions by type
        auto& metrics = rotation_metrics::instance();
        
        // Try deleting .pending files first (compression leftovers)
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i].filename.find(".pending") != std::string::npos) {
                LOG(info) << "ENOSPC: Deleting pending file: " << files[i].filename;
                metrics.enospc_deletions_pending.fetch_add(1);
                metrics.enospc_deleted_bytes.fetch_add(files[i].size);
                std::filesystem::remove(files[i].filename);
                files.erase(files.begin() + i);
                return true;
            }
        }
        
        // Then try .gz files (already compressed)
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i].filename.find(".gz") != std::string::npos) {
                LOG(info) << "ENOSPC: Deleting compressed file: " << files[i].filename;
                metrics.enospc_deletions_gz.fetch_add(1);
                metrics.enospc_deleted_bytes.fetch_add(files[i].size);
                std::filesystem::remove(files[i].filename);
                files.erase(files.begin() + i);
                return true;
            }
        }
        
        // Finally delete oldest raw log (may violate keep_files policy!)
        if (!files.empty()) {
            LOG(warn) << "ENOSPC: Deleting log (violates retention): " << files[0].filename;
            metrics.enospc_deletions_raw.fetch_add(1);
            metrics.enospc_deleted_bytes.fetch_add(files[0].size);
            std::filesystem::remove(files[0].filename);
            files.erase(files.begin());
            return true;
        }
        
        return false;
    }
    
public:
    static file_rotation_service& instance() {
        static file_rotation_service service;
        return service;
    }
    
    std::shared_ptr<rotation_handle> open(const std::string& filename, const rotate_policy& policy) {
        auto handle = std::make_shared<rotation_handle>();
        handle->base_filename_ = filename;
        handle->policy_ = policy;
        handle->service_ = this;
        handle->bytes_written_.store(0);
        handle->next_fd_ready_.store(false);
        
        // Open initial fd (with O_CLOEXEC)
        int fd = ::open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (fd < 0) throw std::runtime_error("Failed to open: " + filename);
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
    
    void enqueue_rotation(std::shared_ptr<rotation_handle> handle, int old_fd, const std::string& temp_filename) {
        // MUST: Take shared_ptr to ensure lifetime extends through message processing
        queue_.enqueue({rotation_message::ROTATE, handle, old_fd, temp_filename});
    }
    
    void enqueue_close(std::shared_ptr<rotation_handle> handle, int fd) {
        // MUST: Take shared_ptr to ensure lifetime extends through message processing
        queue_.enqueue({rotation_message::CLOSE, handle, fd, ""});
    }
};
```

## Usage Examples

```cpp
// Non-rotating (existing behavior)
auto sink1 = make_raw_file_sink("app.log");

// Size-based rotation
rotate_policy size_policy;
size_policy.mode = rotate_policy::kind::size;
size_policy.max_bytes = 256*1024*1024; // 256MB
size_policy.keep_files = 5;
size_policy.sync_on_rotate = true;  // Optional data integrity

auto sink2 = std::make_shared<log_sink>(
    raw_formatter{false, true},
    file_writer{"app.log", size_policy}
);
// Produces: app.log (active)
//          app-20250117-143022-001.log
//          app-20250117-142515-001.log
//          app-20250117-141932-001.log
//          ...

// Daily rotation with compression
rotate_policy daily_policy;
daily_policy.mode = rotate_policy::kind::time;
daily_policy.every = std::chrono::hours{24};
daily_policy.compress = true;
daily_policy.keep_files = 7;

auto sink3 = std::make_shared<log_sink>(
    raw_formatter{false, true},
    file_writer{"app.log", daily_policy}
);
// Produces: app.log (active)
//          app-20250117-000000-001.log.gz
//          app-20250116-000000-001.log.gz
//          ...
```

## Design Constraints & Trade-offs

1. **NO TIMER THREAD ROTATION** - Timer thread CANNOT rotate files. ALL rotations happen ONLY in writer's write() method
   - **Architectural constraint**: Writer owns fd and must trigger its own rotation
   - **Implementation clarity**: No calculate_next_time_rotation(), no process_time_rotations(), no timed waits
2. **Zero-gap when hard links work** - Uses link() + rename() for zero gap; falls back to brief gap on failure
3. **Retention is NOT guaranteed** - ENOSPC deletes protected files
4. **Proactive preparation already implemented** - Next fd prepared immediately after rotation (at 0%)

## Partial Benefits (What Actually Works)

1. **O(1) Rotation** - Timestamped files eliminate cascading renames
2. **Blocking queue** - Uses BlockingConcurrentQueue for proper wait semantics
3. **UTC-Only Simplicity** - No timezone/DST complexity  
4. **Centralized Management** - One thread handles all file rotations
5. **Lazy Initialization** - Service only starts when first rotating file is created
6. **Graceful Degradation** - If rotator falls behind, sinks block briefly (backpressure)
7. **Proper Cleanup** - fdatasync on close ensures data is on disk
8. **Fast path is atomic** - When next fd ready, just atomic swap (but slow path blocks on semaphore)
9. **Maintains 2M msg/sec** - For size-based rotation with pre-opened fds

## Retry Logic and Error Handling

### Rotator Thread Behavior

The rotation service uses a **single-focused thread** that completes one operation fully before moving to the next. This design choice prioritizes simplicity and predictability over complex async state management.

**Key characteristics:**
- **Blocking with retry**: When an operation fails (e.g., can't open a file), the thread retries with exponential backoff
- **Backoff schedule**: 1ms → 2ms → 4ms → 8ms → ... → 1 second (max)
- **Max retries**: 10 attempts by default (configurable)
- **No concurrent operations**: Thread blocks on failures rather than moving to other tasks

This means if rotation for one file is stuck retrying, ALL rotation operations wait. This is intentional - it provides natural backpressure to the system.

### Writer Behavior During Failures

Writers interact with rotation through three distinct phases:

1. **Normal operation**: Writer checks `should_rotate()`, gets pre-opened fd instantly
2. **Waiting phase**: If next fd not ready, writer blocks on semaphore (backpressure)
3. **Error state**: After max retries, handle enters error state, writers discard logs

The transition to discard mode is explicit:
```cpp
int new_fd = rotation_handle_->get_next_fd();
if (new_fd == -1) {
    // Handle is in error state - discard the write
    return len;  // Pretend we wrote it
}
```

### Error Recovery

Once a handle enters error state:
- All writes to that sink are discarded
- The rotation service continues attempting recovery in background
- Manual intervention may be required (fix permissions, free disk space, etc.)
- Creating a new sink with same filename will get a fresh handle

## Performance Characteristics

### Fast Path (Common Case)
- Next fd is pre-opened and ready
- Atomic swap of file descriptors
- Lock-free queue enqueue for background work
- No syscalls, no blocking
- **Cost**: ~100ns (same as non-rotating write)

### Slow Path (Rotator Behind)
- Sink blocks on semaphore
- Gives system time to catch up
- Once fd ready, resumes at full speed
- **Cost**: Depends on rotation speed (typically <1ms)

### Error Path (Rotation Failed)
- After max retries, writers enter discard mode
- No blocking, writes return immediately
- **Cost**: Single atomic load to check error state

### Background Thread Work
- **O(1) rotation**: Single rename to timestamped file (no cascading)
- **Directory scan**: O(N) for retention but no rename chain
- **Smart cleanup**: ENOSPC triggers immediate space recovery
- **Compression**: Async with .pending files for safety
- **Pre-opening next fd**: Ready before needed
- All expensive operations isolated from hot path

### Rotation Performance

**Old cascade approach (app.log → .1 → .2 → .3):**
- O(N) renames for N retained files
- Directory lock held for entire chain
- Risk of blocking on large N

**New timestamped approach (app-YYYYMMDD-HHMMSS-NNN.log):**
- O(1) rename operation
- Minimal directory lock time
- Predictable latency regardless of retention count

### Retention Performance with File Cache

**Without cache (naive approach):**
- O(N) directory scan on every rotation
- readdir() + stat() for each file
- Can be expensive with high rotation frequency (2M msg/sec → many rotations)

**With cache (optimized):**
- **One-time scan**: O(N) directory scan only on handle creation
- **Incremental updates**: O(1) to add new rotated file
- **Retention check**: O(K) where K = cached files (typically 5-10)
- **No syscalls**: Works entirely from memory during rotation

Cache benefits at 2M msg/sec with 100MB files:
- ~20 rotations per second
- Without cache: 20 × readdir() × N files = significant overhead
- With cache: 20 × memory scan of 5-10 entries = negligible

## Implementation Considerations

### 1. Service Shutdown

The singleton needs clean shutdown at program exit:

```cpp
class file_rotation_service {
    ~file_rotation_service() {
        // Signal shutdown
        running_.store(false);
        
        // Send sentinel message to wake thread
        queue_.enqueue({rotation_message::SHUTDOWN, nullptr, -1, ""});
        
        // Process remaining messages
        if (rotator_thread_.joinable()) {
            rotator_thread_.join();
        }
        
        // Final fdatasync for all open handles
        for (auto& weak_handle : handles_) {
            if (auto handle = weak_handle.lock()) {
                fdatasync(handle->current_fd_.load());
            }
        }
    }
    
    void rotator_thread_func() {
        while (running_.load()) {
            rotation_message msg;
            if (queue_.try_dequeue(msg)) {
                if (msg.msg_type == rotation_message::SHUTDOWN) {
                    // Process any remaining messages before exit
                    drain_queue();
                    break;
                }
                // ... handle other message types
            }
        }
    }
};
```

### 2. Data Integrity During Rotation

Ensure data is on disk before file moves:

```cpp
void handle_rotation(const rotation_message& msg) {
    // Sync data before closing/renaming
    fdatasync(msg.old_fd);
    close(msg.old_fd);
    
    // Now safe to rename - all data is persisted
    rotate_files(msg.handle->base_filename_, msg.temp_filename);
    // ...
}
```

### 3. Error Handling

Robust handling of file operation failures:

```cpp
void prepare_next_fd(rotation_handle* handle) {
    std::string temp_name = generate_temp_filename(handle->base_filename_);
    
    int new_fd = open(temp_name.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (new_fd >= 0) {
        // Success path
        handle->next_temp_filename_ = temp_name;
        handle->next_fd_.store(new_fd, std::memory_order_release);
        handle->next_fd_ready_.store(true, std::memory_order_release);
        handle->next_fd_semaphore_.signal();
    } else {
        // Error path - log and retry
        LOG(error) << "Failed to prepare next fd for " << handle->base_filename_ 
                   << ": " << strerror(errno);
        
        // Schedule retry after delay
        schedule_retry(handle, std::chrono::seconds(1));
        
        // For now, writers continue using current fd
        // The semaphore is not signaled, so get_next_fd() won't complete
    }
}

void schedule_retry(rotation_handle* handle, std::chrono::milliseconds delay) {
    // Add to retry queue with timestamp
    retry_queue_.enqueue({
        log_fast_timestamp() + delay,
        handle
    });
}
```

### 4. Handle Lifecycle Management

Clean up expired handles periodically:

```cpp
void rotator_thread_func() {
    auto last_cleanup = log_fast_timestamp();
    
    while (running_.load()) {
        // ... normal message processing
        
        // Periodic cleanup of dead handles
        auto now = log_fast_timestamp();
        if (now - last_cleanup > std::chrono::seconds(10)) {
            cleanup_expired_handles();
            last_cleanup = now;
        }
    }
}

void cleanup_expired_handles() {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    
    // Remove expired weak_ptrs
    handles_.erase(
        std::remove_if(handles_.begin(), handles_.end(),
            [](const std::weak_ptr<rotation_handle>& wp) {
                return wp.expired();
            }),
        handles_.end()
    );
}
```

### 5. Compression with .pending Files

To prevent data loss during compression failures, we use a multi-step process:

```cpp
void compress_file_async(const std::string& filename) {
    // MUST: Update cache atomically during compression transitions
    // Step 1: Rename to .pending to mark for compression
    std::string pending_name = filename + ".pending";
    if (rename(filename.c_str(), pending_name.c_str()) != 0) {
        LOG(error) << "Failed to rename for compression: " << strerror(errno);
        return;  // Leave original file intact
    }
    
    // Update cache entry atomically
    update_cache_entry(filename, pending_name);
    
    // Step 2: Compress to temporary file
    std::string temp_gz = filename + ".gz.tmp";
    if (!compress_to_file(pending_name, temp_gz)) {
        LOG(error) << "Compression failed, keeping .pending file";
        rotation_metrics::instance().compression_failures.fetch_add(1);
        // .pending file remains for manual recovery
        return;
    }
    
    // Step 3: Atomic rename to final .gz
    std::string final_gz = filename + ".gz";
    if (rename(temp_gz.c_str(), final_gz.c_str()) == 0) {
        // Success - remove .pending file
        unlink(pending_name.c_str());
        // Update cache entry atomically
        update_cache_entry(pending_name, final_gz);
    } else {
        // Failed - clean up temp, keep .pending
        unlink(temp_gz.c_str());
        LOG(error) << "Failed to finalize compression";
    }
}

void update_cache_entry(const std::string& old_name, const std::string& new_name) {
    // MUST: Atomically update cache entries during compression/rename
    // This prevents cache from becoming stale
    // Implementation would update the rotated_files_cache_ entries
    // to reflect the new filename while preserving timestamp and size
}
```

**Benefits of this approach:**
- Original data never lost (either .pending or .gz exists)
- Compression failures don't block rotation
- Easy manual recovery (just rename .pending back)
- No partial .gz files (atomic rename)

### 6. Additional Safety Measures

```cpp
class rotation_handle {
    // Add state tracking
    enum class state { ACTIVE, ROTATING, CLOSING, CLOSED };
    std::atomic<state> state_{state::ACTIVE};
    
    int get_next_fd() {
        // Check if handle is still valid
        if (state_.load() >= state::CLOSING) {
            return current_fd_.load();  // Don't rotate if closing
        }
        // ... rest of implementation
    }
};
```

## Metrics Export for Monitoring

To properly monitor the rotation service, expose these metrics to your monitoring system:

```cpp
// Example: Prometheus exporter integration
void export_rotation_metrics(prometheus::Registry& registry) {
    auto& metrics = rotation_metrics::instance();
    
    // CRITICAL: Dropped writes indicate POSIX violation
    auto& dropped_counter = prometheus::BuildCounter()
        .Name("log_rotation_dropped_total")
        .Help("Total log records dropped due to rotation failures")
        .Register(registry);
    dropped_counter.Add(metrics.dropped_records_total.load());
    
    auto& dropped_bytes = prometheus::BuildCounter()
        .Name("log_rotation_dropped_bytes_total")
        .Help("Total bytes dropped due to rotation failures")
        .Register(registry);
    dropped_bytes.Add(metrics.dropped_bytes_total.load());
    
    // Rotation health
    auto& rotation_counter = prometheus::BuildCounter()
        .Name("log_rotations_total")
        .Help("Total successful rotations")
        .Register(registry);
    rotation_counter.Add(metrics.rotations_total.load());
    
    // ENOSPC violations (audit trail)
    auto& enospc_family = prometheus::BuildCounter()
        .Name("log_enospc_deletions_total")
        .Help("Files deleted due to ENOSPC")
        .Register(registry);
    enospc_family.Add({{"type", "pending"}}, metrics.enospc_deletions_pending.load());
    enospc_family.Add({{"type", "gz"}}, metrics.enospc_deletions_gz.load());
    enospc_family.Add({{"type", "raw"}}, metrics.enospc_deletions_raw.load());
    
    // Zero-gap promise violations
    auto& gap_counter = prometheus::BuildCounter()
        .Name("log_rotation_gap_fallbacks_total")
        .Help("Times we had to use gapful rotation")
        .Register(registry);
    gap_counter.Add(metrics.zero_gap_fallback_total.load());
}

// Alert rules you MUST have:
// 1. rate(log_rotation_dropped_total[1m]) > 0  → "CRITICAL: Logs being dropped"
// 2. rate(log_enospc_deletions_total[5m]) > 0  → "WARNING: Retention violated"
// 3. rate(log_rotation_gap_fallbacks_total[5m]) > threshold → "INFO: Gap frequency high"
// 4. rate(log_rotations_total[5m]) == 0 AND log_bytes > threshold → "WARNING: Rotation stuck"
```

## Test Coverage

- Rotation at exact size boundaries
- Time-based rotation at specified times
- Combined size/time rotation
- Retention policy enforcement
- Compression functionality
- Performance during rotation (maintain 2M msg/sec)
- Multiple simultaneous rotating files
- Proper fdatasync on close
- Recovery from rotator thread delays
- Clean shutdown with pending rotations
- Error recovery (disk full, permission denied, etc.)
- Handle lifecycle (creation, rotation, destruction)

