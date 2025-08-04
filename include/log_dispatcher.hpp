/**
 * @file log_dispatcher.hpp
 * @brief Asynchronous log message dispatcher implementation
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 *
 * The log dispatcher initializes with a default stdout sink for convenience.
 * This default sink is automatically replaced when the first sink is added
 * via add_sink(). Calls to set_sink() or remove_sink() also disable the
 * default sink behavior. This ensures logs are visible by default while
 * allowing full customization when needed.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <memory>
#include <atomic>
#include <sys/types.h>
#include <thread>
#include <mutex>
#include <unistd.h> // For write() and STDOUT_FILENO
#include <fcntl.h>

#include "moodycamel/concurrentqueue.h"
#include "moodycamel/blockingconcurrentqueue.h"

#include "log_types.hpp"
#include "log_buffer.hpp"
#include "log_sink.hpp"

namespace slwoggy
{
/**
 * @brief Manages log message dispatching to multiple sinks
 */
struct log_line_dispatcher
{
    /**
     * @brief Immutable sink configuration for lock-free access
     */
    struct sink_config
    {
        std::vector<std::shared_ptr<log_sink>> sinks;

        // Helper to create a copy with modifications
        std::unique_ptr<sink_config> copy() const
        {
            auto new_config   = std::make_unique<sink_config>();
            new_config->sinks = sinks; // Copies shared_ptr, not the sinks
            return new_config;
        }
    };

#ifdef LOG_COLLECT_DISPATCHER_METRICS
    /**
     * @brief Dispatcher statistics for monitoring and diagnostics
     */
    struct stats
    {
        uint64_t total_dispatched;       ///< Total log messages dispatched
        uint64_t queue_enqueue_failures; ///< Failed enqueue attempts
        uint64_t current_queue_size;     ///< Current messages in queue
        uint64_t max_queue_size;         ///< Maximum queue size observed
        uint64_t total_flushes;          ///< Total flush operations
        uint64_t messages_dropped;       ///< Messages dropped (if any)
        float queue_usage_percent;       ///< Queue usage percentage
        uint64_t worker_iterations;      ///< Worker thread loop iterations
        size_t active_sinks;             ///< Number of active sinks
        double avg_dispatch_time_us;     ///< Average dispatch time in microseconds
        uint64_t max_dispatch_time_us;   ///< Maximum dispatch time in microseconds
    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
        double messages_per_second_1s;  ///< Message rate over last 1 second
        double messages_per_second_10s; ///< Message rate over last 10 seconds
        double messages_per_second_60s; ///< Message rate over last 60 seconds
    #endif
        double avg_batch_size;                      ///< Average number of messages per batch
        uint64_t total_batches;                     ///< Total number of batches processed
        uint64_t min_batch_size;                    ///< Minimum batch size observed
        uint64_t max_batch_size;                    ///< Maximum batch size observed
        uint64_t min_inflight_time_us;              ///< Minimum in-flight time in microseconds
        double avg_inflight_time_us;                ///< Average in-flight time in microseconds
        uint64_t max_inflight_time_us;              ///< Maximum in-flight time in microseconds
        uint64_t min_dequeue_time_us;               ///< Minimum time spent in dequeue_buffers
        double avg_dequeue_time_us;                 ///< Average time spent in dequeue_buffers
        uint64_t max_dequeue_time_us;               ///< Maximum time spent in dequeue_buffers
        std::chrono::steady_clock::duration uptime; ///< Time since dispatcher started
    };
#endif

    void dispatch(struct log_line &line); // Defined after log_line
    void flush();                         // Flush pending logs
    void worker_thread_func();            // Worker thread function

  public:
    static log_line_dispatcher &instance()
    {
        static log_line_dispatcher instance_;
        return instance_;
    }

    constexpr auto start_time() const noexcept { return start_time_; }
    constexpr auto start_time_us() const noexcept { return start_time_us_; }

    /**
     * @brief Add a new log sink
     *
     * @warning Sink modifications use RCU (Read-Copy-Update) pattern which is
     *          optimized for read-heavy workloads. Frequent sink modifications
     *          will cause performance degradation due to:
     *          - Mutex contention during updates
     *          - Memory allocation for new configuration
     *          - Cache invalidation across threads
     *
     * @note Configure all sinks at application startup and avoid runtime changes
     *
     * @param sink Pointer to sink (must remain valid until removed)
     */
    void add_sink(std::shared_ptr<log_sink> sink)
    {
        std::lock_guard<std::mutex> lock(sink_modify_mutex_);
        auto current = current_sinks_.load(std::memory_order_acquire);
        if (!current) return;

        auto new_config = current->copy();

        // If this is the first add_sink call and we have the default sink, replace it
        if (has_default_sink_)
        {
            new_config->sinks.clear();
            has_default_sink_ = false;
        }

        new_config->sinks.push_back(sink);

        // Store new config and schedule old for deletion
        update_sink_config(std::move(new_config));
    }

    std::shared_ptr<log_sink> get_sink(size_t index)
    {
        // Lock to prevent the config from being deleted out from under us.
        std::lock_guard<std::mutex> lock(sink_modify_mutex_);
        auto config = current_sinks_.load(std::memory_order_acquire);
        return (config && index < config->sinks.size()) ? config->sinks[index] : nullptr;
    }

    /**
     * @brief Set or replace a sink at specific index
     *
     * @warning See add_sink() for performance considerations. The RCU pattern
     *          means each modification allocates a new configuration object.
     *
     * @param index Sink index (0 to MAX_SINKS-1)
     * @param sink New sink pointer (nullptr to clear)
     */
    void set_sink(size_t index, std::shared_ptr<log_sink> sink)
    {
        std::lock_guard<std::mutex> lock(sink_modify_mutex_);
        auto current = current_sinks_.load(std::memory_order_acquire);
        if (!current) return;

        has_default_sink_ = false; // Clear the default sink flag

        auto new_config = current->copy();
        if (index >= new_config->sinks.size()) { new_config->sinks.resize(index + 1); }
        new_config->sinks[index] = sink;

        update_sink_config(std::move(new_config));
    }

    void remove_sink(size_t index)
    {
        std::lock_guard<std::mutex> lock(sink_modify_mutex_);
        auto current = current_sinks_.load(std::memory_order_acquire);
        if (!current || index >= current->sinks.size()) return;

        has_default_sink_ = false; // Clear the default sink flag

        auto new_config = current->copy();
        new_config->sinks.erase(new_config->sinks.begin() + index);

        update_sink_config(std::move(new_config));
    }

    ~log_line_dispatcher();

#ifdef LOG_COLLECT_DISPATCHER_METRICS
    /**
     * @brief Get current dispatcher statistics
     * @return Statistics snapshot
     */
    stats get_stats() const;

    /**
     * @brief Reset statistics counters (useful for testing)
     */
    void reset_stats();
#endif

  private:
    log_line_dispatcher();

    void update_sink_config(std::unique_ptr<sink_config> new_config);

    // Worker thread helper methods
    size_t dequeue_buffers(moodycamel::ConsumerToken& token, log_buffer** buffers, bool wait);
    size_t process_buffer_batch(log_buffer** buffers, size_t start_idx, size_t count, sink_config* config);
    bool process_queue(log_buffer** buffers, size_t dequeued_count, sink_config* config);
    void drain_queue(moodycamel::ConsumerToken& token);

#ifdef LOG_COLLECT_DISPATCHER_METRICS
    // Helper functions for statistics collection
    void update_batch_stats(size_t dequeued_count);
    void track_inflight_times(log_buffer **buffers, size_t start_idx, size_t count);
    void update_dispatch_timing_stats(std::chrono::steady_clock::time_point start, size_t message_count);
    void update_message_rate_sample();

    // Atomic min/max update helpers
    static void update_atomic_min(std::atomic<uint64_t> &atomic_val, uint64_t new_val);
    static void update_atomic_max(std::atomic<uint64_t> &atomic_val, uint64_t new_val);

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
    double calculate_rate_for_window(std::chrono::seconds window_duration) const
    {
        if (rate_sample_count_ == 0) return 0.0;

        auto now          = log_fast_timestamp();
        auto window_start = now - window_duration;

        // Find newest sample (most recent)
        size_t newest_idx = (rate_write_idx_ + RATE_WINDOW_SIZE - 1) % RATE_WINDOW_SIZE;
        if (rate_sample_count_ == 0 || rate_samples_[newest_idx].timestamp < window_start)
        {
            return 0.0; // No samples in window
        }

        // Find oldest sample within window
        uint64_t oldest_count = 0;
        std::chrono::steady_clock::time_point oldest_time;
        bool found_sample = false;

        for (size_t i = 0; i < rate_sample_count_; i++)
        {
            size_t idx = (rate_write_idx_ + RATE_WINDOW_SIZE - 1 - i) % RATE_WINDOW_SIZE;
            if (rate_samples_[idx].timestamp >= window_start)
            {
                oldest_count = rate_samples_[idx].cumulative_count;
                oldest_time  = rate_samples_[idx].timestamp;
                found_sample = true;
            }
            else
            {
                break; // Older samples won't match
            }
        }

        if (found_sample && oldest_time != rate_samples_[newest_idx].timestamp)
        {
            auto time_diff = std::chrono::duration<double>(rate_samples_[newest_idx].timestamp - oldest_time).count();
            auto msg_diff  = rate_samples_[newest_idx].cumulative_count - oldest_count;
            return msg_diff / time_diff;
        }
        return 0.0;
    }
    #endif
#endif

    std::chrono::steady_clock::time_point start_time_;
    int64_t start_time_us_;

    // Lock-free sink access
    std::atomic<sink_config *> current_sinks_{nullptr};
    std::mutex sink_modify_mutex_;
    bool has_default_sink_{true}; // Flag to track if we still have the default stdout sink // Only for modifications

    // Async processing members
    moodycamel::BlockingConcurrentQueue<log_buffer *> queue_;
    std::thread worker_thread_;
    std::atomic<bool> shutdown_{false};

    mutable std::mutex flush_mutex_;
    mutable std::condition_variable flush_cv_;

    std::atomic<uint64_t> flush_seq_{0};
    std::atomic<uint64_t> flush_done_{0};

#ifdef LOG_COLLECT_DISPATCHER_METRICS
    // Statistics tracking
    // These are only written by dispatch() method (single writer)
    uint64_t total_dispatched_{0};
    uint64_t queue_enqueue_failures_{0};
    uint64_t messages_dropped_{0};

    // These are only written by worker thread (single writer)
    uint64_t worker_iterations_{0};
    uint64_t total_dispatch_time_us_{0};
    uint64_t dispatch_count_for_avg_{0};

    // These need atomic as they're read/written from multiple threads
    std::atomic<uint64_t> max_queue_size_{0};
    std::atomic<uint64_t> total_flushes_{0};
    std::atomic<uint64_t> max_dispatch_time_us_{0};
    std::atomic<uint64_t> min_batch_size_{UINT64_MAX};
    std::atomic<uint64_t> max_batch_size_{0};
    std::atomic<uint64_t> min_inflight_time_us_{UINT64_MAX};
    std::atomic<uint64_t> max_inflight_time_us_{0};
    std::atomic<uint64_t> min_dequeue_time_us_{UINT64_MAX};
    std::atomic<uint64_t> max_dequeue_time_us_{0};

    // Batch tracking (single writer - worker thread)
    uint64_t total_batches_{0};
    uint64_t total_batch_messages_{0};

    // In-flight time tracking (single writer - worker thread)
    uint64_t total_inflight_time_us_{0};
    uint64_t inflight_count_{0};
    
    // Dequeue timing tracking (single writer - worker thread)
    uint64_t total_dequeue_time_us_{0};
    uint64_t dequeue_count_{0};

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
    // Sliding window rate calculation
    static constexpr size_t RATE_WINDOW_SIZE   = 120;                            // 2 minutes of samples at 1/sec
    static constexpr auto RATE_SAMPLE_INTERVAL = std::chrono::milliseconds(100); // Sample every 100ms

    struct rate_sample
    {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t cumulative_count;
    };

    rate_sample rate_samples_[RATE_WINDOW_SIZE];
    size_t rate_write_idx_{0};
    size_t rate_sample_count_{0};
    std::chrono::steady_clock::time_point last_rate_sample_time_;
    #endif
#endif
};

} // namespace slwoggy