#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "moodycamel/concurrentqueue.h"
#include "moodycamel/blockingconcurrentqueue.h"
#include "moodycamel/lightweightsemaphore.h"

namespace slwoggy
{

// Forward declarations
class file_rotation_service;
class rotation_handle;

// Rotation policy configuration
struct rotate_policy
{
    enum class kind
    {
        none,
        size,
        time,
        size_or_time
    };
    kind mode = kind::none;

    // Size policy
    uint64_t max_bytes = 0; // e.g. 256_MB

    // Time policy
    std::chrono::minutes every{0}; // e.g. 24h for daily
    std::chrono::minutes at{0};    // cutover time for daily

    // Retention (NOT GUARANTEED - violated on ENOSPC!)
    // Precedence: 1. keep_files, 2. max_total_bytes, 3. max_age
    int keep_files = 5;
    std::chrono::hours max_age{0};
    uint64_t max_total_bytes = 0;

    // Post-rotate
    bool compress       = false;
    bool sync_on_rotate = false; // fdatasync before rotation

    // Error handling
    int max_retries = 10;
};

// Metrics for observability
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
    std::atomic<uint64_t> prepare_fd_failures{0};
    std::atomic<uint64_t> fsync_failures{0};

    static rotation_metrics &instance()
    {
        static rotation_metrics metrics;
        return metrics;
    }

    void dump_metrics() const; // Implementation in .cpp file
};

// Handle/token that sinks use
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
    };
    std::vector<rotated_file_entry> rotated_files_cache_;
    std::mutex cache_mutex_;

    // Back-reference to service
    file_rotation_service *service_;

    bool should_rotate_size(size_t next_write_size) const
    {
        return (bytes_written_.load() + next_write_size) > policy_.max_bytes;
    }

  public:
    int get_current_fd() const { return current_fd_.load(std::memory_order_acquire); }

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
            if (policy_.every == std::chrono::hours{24})
            {
                auto dp      = std::chrono::floor<std::chrono::days>(now);
                auto cutover = dp + policy_.at;

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

// Singleton rotation service
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
    void add_to_cache(rotation_handle *handle, const std::string &filename, size_t size);
    void initialize_cache(rotation_handle *handle);
    void update_cache_entry(const std::string &old_name, const std::string &new_name);

    void compress_file_async(const std::string &filename);
    void cleanup_expired_handles();

  public:
    static file_rotation_service &instance()
    {
        static file_rotation_service service;
        return service;
    }

    std::shared_ptr<rotation_handle> open(const std::string &filename, const rotate_policy &policy);

    void enqueue_rotation(std::shared_ptr<rotation_handle> handle, int old_fd, const std::string &temp_filename)
    {
        queue_.enqueue({rotation_message::ROTATE, handle, old_fd, temp_filename});
    }

    void enqueue_close(std::shared_ptr<rotation_handle> handle, int fd)
    {
        queue_.enqueue({rotation_message::CLOSE, handle, fd, ""});
    }
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