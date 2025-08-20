/**
 * @file log_file_rotator.hpp
 * @brief File rotation support for log files
 * @author dorgby.net
 * 
 * This file provides comprehensive file rotation capabilities including:
 * - Size-based rotation (rotate when file reaches specified size)
 * - Time-based rotation (rotate at intervals or specific times)
 * - Combined rotation (size OR time triggers)
 * - Retention policies (by count, age, or total size)
 * - Automatic compression of rotated files
 * - Zero-gap rotation using atomic operations
 * - ENOSPC handling with automatic cleanup
 */
#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <io.h>
#include <windows.h>
#endif

#include "moodycamel/concurrentqueue.h"
#include "moodycamel/blockingconcurrentqueue.h"
#include "moodycamel/lightweightsemaphore.h"

namespace slwoggy
{

// Forward declarations
class file_rotation_service;
class rotation_handle;

/**
 * @brief State machine for compression status
 * 
 * State transitions:
 * - idle -> queued: When file is enqueued for compression
 * - queued -> compressing: When compression thread starts processing
 * - queued -> cancelled: When file is deleted before compression starts
 * - compressing -> done: When compression completes successfully
 * - compressing -> cancelled: When file is deleted during compression
 * - Any state -> cancelled: When retention policy deletes the file
 */
enum class compression_state : uint8_t
{
    idle,        ///< Not queued for compression
    queued,      ///< In compression queue waiting to be processed
    compressing, ///< Currently being compressed
    done,        ///< Compression complete
    cancelled    ///< Cancelled by retention/cleanup
};

/**
 * @brief Rotation policy configuration
 * 
 * Defines when and how log files should be rotated, including
 * rotation triggers, retention policies, and post-rotation actions.
 */
struct rotate_policy
{
    /**
     * @brief Rotation trigger mode
     */
    enum class kind
    {
        none,          ///< No rotation (default)
        size,          ///< Rotate based on file size
        time,          ///< Rotate based on time interval
        size_or_time   ///< Rotate on size OR time (whichever triggers first)
    };
    kind mode = kind::none;  ///< Active rotation mode

    /// @name Size-based rotation
    /// @{
    uint64_t max_bytes = 0; ///< Maximum file size before rotation (0 = disabled)
    /// @}

    /// @name Time-based rotation
    /// @{
    std::chrono::seconds every{0}; ///< Rotation interval (e.g., 86400s for daily)
    std::chrono::seconds at{0};    ///< Time of day for rotation (seconds since midnight UTC)
    /// @}

    /// @name Retention policies
    /// @note Applied in precedence order: keep_files, max_total_bytes, max_age
    /// @warning Retention may be violated during ENOSPC conditions
    /// @{
    int keep_files = 5;                  ///< Maximum number of rotated files to keep
    std::chrono::seconds max_age{0};     ///< Delete files older than this (0 = no age limit)
    uint64_t max_total_bytes = 0;        ///< Maximum total size of all log files (0 = no limit)
    /// @}

    /// @name Post-rotation actions
    /// @{
    bool compress       = false;         ///< Compress rotated files with gzip
    bool sync_on_rotate = false;         ///< Call fdatasync before rotation for durability
    /// @}

    /// @name Error handling
    /// @{
    int max_retries = 10;                ///< Maximum retry attempts on rotation failure
    /// @}
};

#ifdef LOG_COLLECT_ROTATION_METRICS
/**
 * @brief Metrics for monitoring rotation behavior
 * 
 * Provides comprehensive metrics about file rotation operations,
 * including rotation counts, timing, error conditions, and ENOSPC handling.
 * 
 * @note All metrics are thread-safe using atomic operations
 */
struct rotation_metrics
{
    // Dropped writes (POSIX violation detection)
    std::atomic<uint64_t> dropped_records_total{0};
    std::atomic<uint64_t> dropped_bytes_total{0};

    // Rotation progress
    std::atomic<uint64_t> rotations_total{0};
    std::atomic<uint64_t> rotation_duration_us_sum{0};
    std::atomic<uint64_t> rotation_duration_us_count{0};

    // ENOSPC auto-deletions (retention violations)
    std::atomic<uint64_t> enospc_deletions_pending{0};
    std::atomic<uint64_t> enospc_deletions_gz{0};
    std::atomic<uint64_t> enospc_deletions_raw{0};
    std::atomic<uint64_t> enospc_deleted_bytes{0};

    // Zero-gap fallback frequency
    std::atomic<uint64_t> zero_gap_fallback_total{0};

    // Additional metrics
    std::atomic<uint64_t> compression_failures{0};
    std::atomic<uint64_t> compression_queue_overflows{0};
    std::atomic<uint64_t> prepare_fd_failures{0};
    std::atomic<uint64_t> fsync_failures{0};

    static rotation_metrics &instance()
    {
        static rotation_metrics metrics;
        return metrics;
    }

    // Statistics structure for consistent reporting
    struct stats
    {
        // Rotation activity
        uint64_t total_rotations;
        uint64_t avg_rotation_time_us;
        uint64_t total_rotation_time_us;
        
        // Data loss tracking
        uint64_t dropped_records;
        uint64_t dropped_bytes;
        
        // ENOSPC handling
        uint64_t enospc_pending_deleted;
        uint64_t enospc_gz_deleted;
        uint64_t enospc_raw_deleted;
        uint64_t enospc_bytes_freed;
        
        // Error tracking
        uint64_t zero_gap_fallbacks;
        uint64_t compression_failures;
        uint64_t compression_queue_overflows;
        uint64_t prepare_fd_failures;
        uint64_t fsync_failures;
    };
    
    stats get_stats() const
    {
        stats s;
        
        // Rotation activity
        s.total_rotations = rotations_total.load();
        auto count = rotation_duration_us_count.load();
        s.total_rotation_time_us = rotation_duration_us_sum.load();
        s.avg_rotation_time_us = (count > 0) ? s.total_rotation_time_us / count : 0;
        
        // Data loss tracking
        s.dropped_records = dropped_records_total.load();
        s.dropped_bytes = dropped_bytes_total.load();
        
        // ENOSPC handling
        s.enospc_pending_deleted = enospc_deletions_pending.load();
        s.enospc_gz_deleted = enospc_deletions_gz.load();
        s.enospc_raw_deleted = enospc_deletions_raw.load();
        s.enospc_bytes_freed = enospc_deleted_bytes.load();
        
        // Error tracking
        s.zero_gap_fallbacks = zero_gap_fallback_total.load();
        s.compression_failures = compression_failures.load();
        s.compression_queue_overflows = compression_queue_overflows.load();
        s.prepare_fd_failures = prepare_fd_failures.load();
        s.fsync_failures = fsync_failures.load();
        
        return s;
    }

    void dump_metrics() const; // Implementation in .cpp file
};
#endif // LOG_COLLECT_ROTATION_METRICS

/**
 * @brief Handle for managing a rotating log file
 * 
 * This class represents a handle to a log file with rotation capabilities.
 * It manages the current file descriptor, tracks bytes written, and
 * coordinates with the rotation service for seamless file rotation.
 * 
 * @note This class is thread-safe and can be shared across multiple writers
 */
class rotation_handle : public std::enable_shared_from_this<rotation_handle>
{
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

  public:
    // Destructor - clean up temp file if any
    ~rotation_handle()
    {
        int next_fd = next_fd_.load();
        if (next_fd >= 0)
        {
            ::close(next_fd);
            if (!next_temp_filename_.empty()) { ::unlink(next_temp_filename_.c_str()); }
        }
    }

    // Metrics for observability
    std::atomic<uint64_t> dropped_records_{0};
    std::atomic<uint64_t> dropped_bytes_{0};
    
  public:
    // Public methods for metrics (maintains encapsulation)
    void increment_dropped_records() { dropped_records_.fetch_add(1, std::memory_order_relaxed); }
    void increment_dropped_bytes(size_t bytes) { dropped_bytes_.fetch_add(bytes, std::memory_order_relaxed); }

  private:
    // Policy and metadata
    rotate_policy policy_;
    std::string base_filename_;
    std::chrono::system_clock::time_point next_rotation_time_;

    // Cached list of rotated files
    struct rotated_file_entry
    {
        std::string filename;
        std::chrono::system_clock::time_point timestamp;
        size_t size;
        std::atomic<compression_state> comp_state{compression_state::idle};
        std::weak_ptr<rotation_handle> handle;
    };
    std::vector<std::shared_ptr<rotated_file_entry>> rotated_files_cache_;
    std::mutex cache_mutex_;

    // Back-reference to service
    file_rotation_service *service_;

    bool should_rotate_size(size_t next_write_size) const
    {
        return (bytes_written_.load() + next_write_size) >= policy_.max_bytes;
    }

  public:
    /**
     * @brief Get the current file descriptor
     * @return Current file descriptor, or -1 if in error state
     */
    int get_current_fd() const { return current_fd_.load(std::memory_order_acquire); }

    /**
     * @brief Check if rotation is needed
     * @param next_write_size Size of the next write in bytes
     * @return true if rotation should occur, false otherwise
     */
    bool should_rotate(size_t next_write_size) const
    {
        // Don't attempt rotation if in error state
        if (in_error_state_.load(std::memory_order_acquire)) { return false; }

        // Writers check BOTH size and time policies during writes
        switch (policy_.mode)
        {
        case rotate_policy::kind::size: return should_rotate_size(next_write_size);
        case rotate_policy::kind::time: return std::chrono::system_clock::now() >= next_rotation_time_;
        case rotate_policy::kind::size_or_time:
            return should_rotate_size(next_write_size) || (std::chrono::system_clock::now() >= next_rotation_time_);
        default: return false;
        }
    }

    void compute_next_rotation_time()
    {
        // Apply policy.at offset; if unset, treat as 00:00 UTC
        auto now = std::chrono::system_clock::now();

        if (policy_.mode == rotate_policy::kind::time || policy_.mode == rotate_policy::kind::size_or_time)
        {

            // Daily rotation at specified time (or 00:00 UTC if not set)
            if (policy_.every == std::chrono::seconds(std::chrono::hours{24}))
            {
                auto dp      = std::chrono::floor<std::chrono::days>(now);
                auto cutover = dp + std::chrono::duration_cast<std::chrono::system_clock::duration>(policy_.at);

                // If we're past today's cutover, move to tomorrow
                if (now >= cutover) { next_rotation_time_ = cutover + std::chrono::days{1}; }
                else { next_rotation_time_ = cutover; }
            }
            else
            {
                // For non-daily intervals, just add the interval
                next_rotation_time_ = now + policy_.every;
            }
        }
    }

    int get_next_fd();
    void add_bytes_written(size_t bytes);
    void close();
};

/**
 * @brief Singleton service managing all file rotation operations
 * 
 * This service runs a background thread that handles:
 * - File rotation when size/time thresholds are reached
 * - Retention policy enforcement (deleting old files)
 * - Compression of rotated files
 * - ENOSPC emergency cleanup
 * - Zero-gap rotation using atomic operations
 * 
 * @note Thread-safe singleton accessed via instance() method
 */
#ifdef LOG_COLLECT_COMPRESSION_METRICS
/**
 * @brief Compression thread statistics
 */
struct compression_stats
{
    uint64_t files_queued;           ///< Total files queued for compression
    uint64_t files_compressed;       ///< Total files successfully compressed
    uint64_t files_cancelled;        ///< Total files cancelled before/during compression
    uint64_t queue_overflows;        ///< Times queue was full when trying to enqueue
    size_t current_queue_size;       ///< Current number of items in queue
    size_t queue_high_water_mark;    ///< Maximum queue size ever reached
};
#endif

class file_rotation_service
{
  private:
    struct rotation_message
    {
        enum type
        {
            ROTATE,
            CLOSE,
            SHUTDOWN
        };
        type msg_type;
        std::shared_ptr<rotation_handle> handle;
        int old_fd;
        std::string temp_filename;
    };

    // Message queue for rotator thread
    moodycamel::BlockingConcurrentQueue<rotation_message> queue_;

    // Active handles
    std::mutex handles_mutex_;
    std::vector<std::weak_ptr<rotation_handle>> handles_;

    // Rotator thread
    std::thread rotator_thread_;
    std::atomic<bool> running_;
    
    // Compression thread infrastructure
    std::thread compression_thread_;
    std::atomic<bool> compression_running_{false};
    moodycamel::BlockingConcurrentQueue<std::shared_ptr<rotation_handle::rotated_file_entry>> compression_queue_;
    std::atomic<size_t> compression_queue_size_{0};
    std::atomic<size_t> compression_queue_high_water_{0};
    size_t compression_queue_max_{10};
    std::chrono::milliseconds compression_delay_{500};
    
#ifdef LOG_COLLECT_COMPRESSION_METRICS
    // Compression statistics
    std::atomic<uint64_t> compression_files_queued_{0};
    std::atomic<uint64_t> compression_files_compressed_{0};
    std::atomic<uint64_t> compression_files_cancelled_{0};
#endif

    file_rotation_service();
    ~file_rotation_service();

    void rotator_thread_func();
    void handle_rotation(const rotation_message &msg);
    void handle_close(const rotation_message &msg);
    void drain_queue();
    
    // Helper functions for rotation
    bool perform_zero_gap_rotation(const std::filesystem::path& base_path,
                                    const std::filesystem::path& rotated_path,
                                    const std::string& temp_filename,
                                    rotation_handle* handle);
    void sync_directory(const std::filesystem::path& base_path);

    std::string generate_rotated_filename(const std::string &base);
    std::string generate_temp_filename(const std::string &base);

    void prepare_next_fd_with_retry(rotation_handle *handle);
    bool emergency_cleanup(rotation_handle *handle);

    void apply_retention_timestamped(rotation_handle *handle);
    std::shared_ptr<rotation_handle::rotated_file_entry> add_to_cache(rotation_handle *handle, const std::string &filename, size_t size);
    void initialize_cache(rotation_handle *handle);
    
    // Compression functions
    bool compress_file_sync(std::shared_ptr<rotation_handle::rotated_file_entry> entry);
    void compression_thread_func();
    bool enqueue_for_compression(std::shared_ptr<rotation_handle::rotated_file_entry> entry);
    void drain_compression_queue();
    
    void cleanup_expired_handles();

  public:
    /**
     * @brief Get the singleton instance of the rotation service
     * @return Reference to the global rotation service
     */
    static file_rotation_service &instance()
    {
        static file_rotation_service service;
        return service;
    }

    /**
     * @brief Open a file with rotation support
     * @param filename Path to the log file
     * @param policy Rotation policy to apply
     * @return Shared pointer to rotation handle
     * @throws std::runtime_error if file cannot be opened
     */
    std::shared_ptr<rotation_handle> open(const std::string &filename, const rotate_policy &policy);

    void enqueue_rotation(std::shared_ptr<rotation_handle> handle, int old_fd, const std::string &temp_filename)
    {
        queue_.enqueue({rotation_message::ROTATE, handle, old_fd, temp_filename});
    }

    void enqueue_close(std::shared_ptr<rotation_handle> handle, int fd)
    {
        queue_.enqueue({rotation_message::CLOSE, handle, fd, ""});
    }
    
    /**
     * @brief Start the compression thread
     * @param delay Batching delay - how long to wait for more items to batch together
     * @param max_queue_size Maximum dispatch queue size - prevents unbounded growth, 
     *                       NOT a limit on total files compressed
     */
    void start_compression_thread(
        std::chrono::milliseconds delay = std::chrono::milliseconds{500},
        size_t max_queue_size = 10);
    
    /**
     * @brief Stop the compression thread
     */
    void stop_compression_thread();
    
    /**
     * @brief Check if compression thread is running
     */
    bool is_compression_enabled() const { return compression_running_.load(); }
    
#ifdef LOG_COLLECT_COMPRESSION_METRICS
    /**
     * @brief Get compression thread statistics
     */
    compression_stats get_compression_stats() const
    {
        compression_stats stats;
        stats.files_queued = compression_files_queued_.load();
        stats.files_compressed = compression_files_compressed_.load();
        stats.files_cancelled = compression_files_cancelled_.load();
#ifdef LOG_COLLECT_ROTATION_METRICS
        stats.queue_overflows = rotation_metrics::instance().compression_queue_overflows.load();
#else
        stats.queue_overflows = 0;
#endif
        stats.current_queue_size = compression_queue_size_.load();
        stats.queue_high_water_mark = compression_queue_high_water_.load();
        return stats;
    }
    
    /**
     * @brief Reset compression statistics (for testing)
     */
    void reset_compression_stats()
    {
        compression_files_queued_ = 0;
        compression_files_compressed_ = 0;
        compression_files_cancelled_ = 0;
        compression_queue_high_water_ = compression_queue_size_.load();
#ifdef LOG_COLLECT_ROTATION_METRICS
        rotation_metrics::instance().compression_queue_overflows = 0;
#endif
    }
#endif
};

// Inline implementations that need full definitions

inline int rotation_handle::get_next_fd()
{
    // Check error state first
    if (in_error_state_.load(std::memory_order_acquire))
    {
        return -1; // Signal writer to discard
    }

    // The rotator thread will always signal the semaphore eventually
    // (either with a prepared FD or when entering error state)
    while (true)
    {
        // Fast path - next fd is ready
        if (next_fd_ready_.load(std::memory_order_acquire))
        {
            int new_fd = next_fd_.exchange(-1, std::memory_order_acq_rel);
            if (new_fd != -1)
            {
                int old_fd = current_fd_.exchange(new_fd);
                bytes_written_.store(0);
                next_fd_ready_.store(false);

                // Notify rotator thread
                service_->enqueue_rotation(shared_from_this(), old_fd, next_temp_filename_);
                return new_fd;
            }
        }

        // Slow path - wait for rotator to prepare next fd
        next_fd_semaphore_.wait();

        // Check if we entered error state while waiting
        if (in_error_state_.load(std::memory_order_acquire)) { return -1; }
    }
}

inline void rotation_handle::add_bytes_written(size_t bytes)
{
    bytes_written_.fetch_add(bytes, std::memory_order_relaxed);
}

inline void rotation_handle::close() { service_->enqueue_close(shared_from_this(), current_fd_.load()); }

} // namespace slwoggy