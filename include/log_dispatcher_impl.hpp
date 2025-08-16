/**
 * @file log_dispatcher_impl.hpp
 * @brief Implementation of log message dispatcher and worker thread
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include "log_line.hpp"
#include "log_dispatcher.hpp"
#include "log_sinks.hpp" // For make_stdout_sink

namespace slwoggy
{

// Stack-allocated buffer for critical messages when pool is exhausted
struct stack_buffer : public log_buffer_base
{
    alignas(CACHE_LINE_SIZE) char storage[buffer_pool::BUFFER_SIZE];
    stack_buffer(bool human_readable) : log_buffer_base(storage, sizeof(storage))
    {
        reset();
        set_padding_enabled(human_readable);
    }
};

inline log_line_dispatcher::log_line_dispatcher()
: start_time_(log_fast_timestamp()),
  start_time_us_(std::chrono::duration_cast<std::chrono::microseconds>(start_time_.time_since_epoch()).count()),
  queue_(MAX_DISPATCH_QUEUE_SIZE), // Initial capacity
  worker_thread_(&log_line_dispatcher::worker_thread_func, this)
{
#ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
    // Initialize last_rate_sample_time_ to current time to avoid huge initial delta
    last_rate_sample_time_ = log_fast_timestamp();
#endif

    // Initialize with default stdout sink
    auto initial_config = std::make_unique<sink_config>();
    initial_config->sinks.push_back(make_stdout_sink());
    current_sinks_.store(initial_config.release(), std::memory_order_release);
}

inline void log_line_dispatcher::shutdown(bool wait_for_completion)
{
    // Only shutdown once
    bool expected = false;
    if (!shutdown_.compare_exchange_strong(expected, true))
    {
        // Already shutting down or shut down
        if (wait_for_completion && worker_thread_.joinable())
        {
            worker_thread_.join();
        }
        return;
    }

    // Enqueue sentinel to wake worker
    queue_.enqueue(nullptr);

    // Optionally wait for worker to finish
    if (wait_for_completion && worker_thread_.joinable())
    {
        worker_thread_.join();
    }
}

inline void log_line_dispatcher::restart()
{
    // Only restart if shut down
    if (!shutdown_.load())
    {
        return; // Already running
    }

    // Wait for any pending shutdown to complete
    if (worker_thread_.joinable())
    {
        worker_thread_.join();
    }

    // Reset shutdown flag
    shutdown_.store(false);

    // Start new worker thread
    worker_thread_ = std::thread(&log_line_dispatcher::worker_thread_func, this);
}

inline log_line_dispatcher::~log_line_dispatcher()
{
    // Use shutdown method with wait
    shutdown(true);

    // Clean up sink config
    auto *config = current_sinks_.load(std::memory_order_acquire);
    delete config;
}

inline void log_line_dispatcher::dispatch(struct log_line_base &line)
{
    // Add this check
    if (shutdown_.load(std::memory_order_relaxed))
    {
        // The dispatcher is shutting down or is already dead.
        // It's no longer safe to enqueue. We must drop the log.
#ifdef LOG_COLLECT_DISPATCHER_METRICS
        messages_dropped_++;
#endif
        return;
    }

    if (line.buffer_ && (line.buffer_->len() > 0 || line.buffer_->get_kv_count() > 0))
    {
        // Buffer has text content and/or structured metadata

        // Swap buffer - line gets fresh buffer, we get the buffer to dispatch
        auto *buffer_to_dispatch = line.swap_buffer();
        if (!buffer_to_dispatch)
        {
#ifdef LOG_COLLECT_DISPATCHER_METRICS
            messages_dropped_++;
#endif
            return;
        }

        thread_local moodycamel::ProducerToken thread_local_token(queue_);
        if (!queue_.enqueue(thread_local_token, buffer_to_dispatch))
        {
#ifdef LOG_COLLECT_DISPATCHER_METRICS
            queue_enqueue_failures_++;
#endif
            buffer_to_dispatch->release();
        }
        // Successfully enqueued - will be counted by worker thread when processed
    }
}

// Worker thread helper methods implementation

inline size_t log_line_dispatcher::dequeue_buffers(moodycamel::ConsumerToken& token, log_buffer_base** buffers, bool wait)
{
    if (!wait)
    {
        return queue_.try_dequeue_bulk(token, buffers, MAX_BATCH_SIZE);
    }

#ifdef LOG_COLLECT_DISPATCHER_METRICS
    auto dequeue_start = log_fast_timestamp();
#endif

    // Initial wait - block indefinitely until data arrives
    size_t total_dequeued = queue_.wait_dequeue_bulk(token, buffers, MAX_BATCH_SIZE);
    
    if (total_dequeued == 0)
    {
        return 0; // Shouldn't happen unless queue is being destroyed
    }
    
    // Got some data - try to collect more within bounded time
    auto batch_start = log_fast_timestamp();
    
    // Phase 2: Bounded time collection
    while (total_dequeued < MAX_BATCH_SIZE)
    {
        auto elapsed = log_fast_timestamp() - batch_start;
        if (elapsed >= BATCH_COLLECT_TIMEOUT)
        {
            break; // Initial timeout reached
        }
        
        // Calculate remaining timeout
        auto remaining = BATCH_COLLECT_TIMEOUT - elapsed;
        auto poll_wait = (remaining < BATCH_POLL_INTERVAL) ? remaining : BATCH_POLL_INTERVAL;
        
        // Convert to milliseconds for the API (minimum 1ms)
        auto poll_wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(poll_wait);
        if (poll_wait_ms.count() == 0 && poll_wait.count() > 0) {
            poll_wait_ms = std::chrono::milliseconds(1);
        }
        
        // Try to get more data
        size_t additional = queue_.wait_dequeue_bulk_timed(token, 
                                                          buffers + total_dequeued, 
                                                          MAX_BATCH_SIZE - total_dequeued,
                                                          poll_wait_ms);
        
        if (additional > 0)
        {
            total_dequeued += additional;
        }
    }
    
    // Phase 3: Continue polling while data is still flowing
    while (total_dequeued < MAX_BATCH_SIZE)
    {
        // Short poll - 1ms gives OS time to context switch producers
        size_t additional = queue_.wait_dequeue_bulk_timed(token, 
                                                          buffers + total_dequeued, 
                                                          MAX_BATCH_SIZE - total_dequeued,
                                                          std::chrono::milliseconds(1));
        
        if (additional == 0)
        {
            break; // No more data flowing, stop collecting
        }
        
        total_dequeued += additional;
    }
    
#ifdef LOG_COLLECT_DISPATCHER_METRICS
    // Track dequeue timing
    auto dequeue_end = log_fast_timestamp();
    auto dequeue_us = std::chrono::duration_cast<std::chrono::microseconds>(dequeue_end - dequeue_start).count();
    total_dequeue_time_us_ += dequeue_us;
    dequeue_count_++;
    
    // Update min/max (using non-atomic worker thread locals)
    update_min(worker_min_dequeue_time_us_, static_cast<uint64_t>(dequeue_us));
    update_max(worker_max_dequeue_time_us_, static_cast<uint64_t>(dequeue_us));
#endif
    
    return total_dequeued;
}

inline size_t log_line_dispatcher::process_buffer_batch(log_buffer_base** buffers, size_t start_idx, size_t count, sink_config* config)
{
    if (count == 0)
    {
        return 0;
    }

    size_t processed = 0;

    if (config && !config->sinks.empty())
    {
        // Dispatch batch to all sinks
        for (size_t i = 0; i < config->sinks.size(); ++i)
        {
            if (config->sinks[i])
            {
                size_t sink_processed = config->sinks[i]->process_batch(&buffers[start_idx], count);

#ifndef NDEBUG
                if (i == 0)
                {
                    processed = sink_processed;
                }
                else
                {
                    // All sinks must process the same number of buffers
                    assert(sink_processed == processed);
                }
#else
                processed = sink_processed;
#endif
            }
        }
    }

#ifdef LOG_COLLECT_DISPATCHER_METRICS
    // Track in-flight time for processed buffers
    track_inflight_times(buffers, start_idx, processed);
#endif

    // Release processed buffers
    for (size_t j = 0; j < processed; ++j)
    {
        buffers[start_idx + j]->release();
    }

    return processed;
}

inline bool log_line_dispatcher::process_queue(log_buffer_base** buffers, size_t dequeued_count, sink_config* config)
{
    size_t buf_idx = 0;
    bool should_shutdown = false;
    int flush_requested = 0;

    while (buf_idx < dequeued_count)
    {
        log_buffer_base* buffer = buffers[buf_idx];

        // Check for shutdown marker
        if (!buffer)
        {
            // Process remaining buffers before shutting down
            if (buf_idx + 1 < dequeued_count && config && !config->sinks.empty())
            {
                size_t remaining_count = dequeued_count - buf_idx - 1;
                size_t processed = process_buffer_batch(buffers, buf_idx + 1, remaining_count, config);
#ifdef LOG_COLLECT_DISPATCHER_METRICS
                total_dispatched_ += processed;
#endif
            }
            
            // Release all buffers (processed and unprocessed) - matching original behavior
            for (size_t j = buf_idx + 1; j < dequeued_count; ++j)
            {
                if (buffers[j]) buffers[j]->release();
            }
            
            should_shutdown = true;
            break;
        }

        // Check for flush marker
        if (buffer->is_flush_marker())
        {
            buffer->release();
#ifdef LOG_COLLECT_DISPATCHER_METRICS
            worker_total_flushes_++;
#endif
            flush_requested++;
            buf_idx++;
            continue;
        }

        // Process batch starting from current position
        if (config && !config->sinks.empty())
        {
            size_t remaining = dequeued_count - buf_idx;
            size_t processed = process_buffer_batch(buffers, buf_idx, remaining, config);

            // If no buffers were processed (all sinks are null), skip this buffer
            if (processed == 0)
            {
                buffers[buf_idx]->release();
                processed = 1;
            }

            buf_idx += processed;
#ifdef LOG_COLLECT_DISPATCHER_METRICS
            total_dispatched_ += processed;
#endif
        }
        else
        {
            // No sinks - check each buffer individually for markers
            size_t start_idx = buf_idx;
            while (buf_idx < dequeued_count)
            {
                log_buffer_base *buf = buffers[buf_idx];

                // Stop at markers to process them properly
                if (!buf || buf->is_flush_marker()) { break; }

                // Release regular buffer
                buf->release();
                buf_idx++;
            }
#ifdef LOG_COLLECT_DISPATCHER_METRICS
            // Count messages processed even with no sinks
            if (buf_idx > start_idx) {
                total_dispatched_ += (buf_idx - start_idx);
            }
#endif
        }
    }

    if (flush_requested > 0)
    {
        // Notify flush waiters
        flush_done_.fetch_add(flush_requested, std::memory_order_release);
        std::lock_guard lk(flush_mutex_);
        flush_cv_.notify_all();
    }

    return should_shutdown;
}

inline void log_line_dispatcher::drain_queue(moodycamel::ConsumerToken& token)
{
    log_buffer_base* buffers[MAX_BATCH_SIZE];
    size_t dequeued_count;

    // Drain remaining buffers on shutdown using batch dequeue
    while ((dequeued_count = dequeue_buffers(token, buffers, false)) > 0)
    {
        auto* config = current_sinks_.load(std::memory_order_acquire);
        
        // Process buffers in batches, respecting markers even during shutdown
        size_t buf_idx = 0;
        while (buf_idx < dequeued_count)
        {
            log_buffer_base* buffer = buffers[buf_idx];

            // Skip null buffers and flush markers during shutdown
            if (!buffer || buffer->is_flush_marker())
            {
                if (buffer) buffer->release();
                buf_idx++;
                continue;
            }

            if (config && !config->sinks.empty())
            {
                // Process batch from current position
                size_t remaining = dequeued_count - buf_idx;
                size_t processed = process_buffer_batch(buffers, buf_idx, remaining, config);

                // If no buffers were processed (all sinks are null), skip this buffer
                if (processed == 0)
                {
                    buffers[buf_idx]->release();
                    processed = 1;
                }

                buf_idx += processed;
#ifdef LOG_COLLECT_DISPATCHER_METRICS
                total_dispatched_ += processed;
#endif
            }
            else
            {
                // No sinks - check each buffer individually for markers
                size_t start_idx = buf_idx;
                while (buf_idx < dequeued_count)
                {
                    log_buffer_base *buf = buffers[buf_idx];

                    // Stop at markers to process them properly
                    if (!buf || buf->is_flush_marker()) { break; }

                    // Release regular buffer
                    buf->release();
                    buf_idx++;
                }
#ifdef LOG_COLLECT_DISPATCHER_METRICS
                // Count messages processed even with no sinks
                if (buf_idx > start_idx) {
                    total_dispatched_ += (buf_idx - start_idx);
                }
#endif
            }
        }
    }
    
    // Final check for any unreported buffer pool failures
    uint64_t final_failures = buffer_pool::instance().get_pending_failures();
    if (final_failures > 0)
    {
        // Use stack buffer to guarantee we can report this
        stack_buffer warning_buffer{true}; // Enable padding for newline
        warning_buffer.level_ = log_level::warn;
        warning_buffer.timestamp_ = log_fast_timestamp();
        warning_buffer.file_ = "log_dispatcher";
        warning_buffer.line_ = 0;
        
        // Format the final warning message
        auto message = fmt::format("Buffer pool exhausted - {} log messages dropped during session", final_failures);
        warning_buffer.write_raw(message);
        warning_buffer.finalize();
        
        // Get current sinks and process the warning
        auto* config = current_sinks_.load(std::memory_order_acquire);
        if (config && !config->sinks.empty())
        {
            log_buffer_base* warning_array[1] = { &warning_buffer };
            // Process directly through sinks, bypassing normal batch processing
            for (auto& sink : config->sinks)
            {
                if (sink)
                {
                    sink->process_batch(warning_array, 1);
                }
            }
        }
        // No need to reset pending_failures since we're shutting down
    }
}

// Worker thread function implementation
inline void log_line_dispatcher::worker_thread_func()
{
    moodycamel::ConsumerToken consumer_token(queue_);
    log_buffer_base *buffers[MAX_BATCH_SIZE];
    size_t dequeued_count;
    bool should_shutdown = false;

    while (!should_shutdown)
    {
#ifdef LOG_COLLECT_DISPATCHER_METRICS
        worker_iterations_++;
#endif
        
        // Check for pending buffer pool failures to report
        uint64_t pending_failures = buffer_pool::instance().get_pending_failures();
        if (pending_failures > 0)
        {
            // Try to acquire a buffer to report the failures
            auto* warning_buffer = buffer_pool::instance().acquire(false);
            if (warning_buffer)
            {
                // Successfully got a buffer - format warning message
                warning_buffer->level_ = log_level::warn;
                warning_buffer->timestamp_ = log_fast_timestamp();
                warning_buffer->file_ = "log_dispatcher";
                warning_buffer->line_ = 0;
                
                // Format the warning message
                auto message = fmt::format("Buffer pool exhausted - {} log messages dropped", pending_failures);
                warning_buffer->write_raw(message);
                warning_buffer->finalize();
                
                // Get current sinks and process the warning
                auto* config = current_sinks_.load(std::memory_order_acquire);
                if (config && !config->sinks.empty())
                {
                    log_buffer_base* warning_array[1] = { warning_buffer };
                    process_buffer_batch(warning_array, 0, 1, config);
                }
                else
                {
                    // No sinks, just release the buffer
                    warning_buffer->release();
                }
                
                // Reset the pending count only after successful reporting
                buffer_pool::instance().reset_pending_failures();
            }
            // If we couldn't get a buffer, leave the count for next iteration
        }

        // Try to dequeue a batch of buffers
        dequeued_count = dequeue_buffers(consumer_token, buffers, true);

        // If no items after timeout, check if we should shut down
        if (dequeued_count == 0)
        {
            if (shutdown_.load(std::memory_order_relaxed)) { break; }
            continue;
        }

#ifdef LOG_COLLECT_DISPATCHER_METRICS
        // Track batch statistics
        update_batch_stats(dequeued_count);
        
        // Track max queue size from worker thread
        auto queue_size = queue_.size_approx();
        if (queue_size > worker_max_queue_size_)
        {
            worker_max_queue_size_ = queue_size;
        }

        // Track batch dispatch timing
        auto dispatch_start = log_fast_timestamp();
#endif

        // Get sink config once for the batch
        auto *config = current_sinks_.load(std::memory_order_acquire);

        // Process the queue
        should_shutdown = process_queue(buffers, dequeued_count, config);

#ifdef LOG_COLLECT_DISPATCHER_METRICS
        // Track dispatch time for the entire batch
        update_dispatch_timing_stats(dispatch_start, dequeued_count);

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
        // Record throughput sample (throttled to avoid too many samples)
        update_message_rate_sample();
    #endif
#endif
    }

    // Drain remaining buffers on shutdown
    drain_queue(consumer_token);
}

// Helper to update sink configuration
inline void log_line_dispatcher::update_sink_config(std::unique_ptr<sink_config> new_config)
{
    auto *old_config = current_sinks_.exchange(new_config.release(), std::memory_order_acq_rel);

    if (old_config)
    {
        // Simple but safe approach: flush to ensure worker thread is done with old config
        // Since sink modifications are extremely rare, this is acceptable
        flush();
        delete old_config;
    }
}

// Flush implementation
inline void log_line_dispatcher::flush()
{
    auto *marker = buffer_pool::instance().acquire(false);
    if (!marker) return;

    marker->level_ = log_level::nolog;

    const uint64_t my_seq = flush_seq_.fetch_add(1, std::memory_order_relaxed) + 1;
    {
        std::unique_lock lk(flush_mutex_);
        queue_.enqueue(marker);
        flush_cv_.wait(lk, [&] { return flush_done_.load(std::memory_order_acquire) >= my_seq; });
    }
}

#ifdef LOG_COLLECT_DISPATCHER_METRICS
// Get statistics implementation
inline log_line_dispatcher::stats log_line_dispatcher::get_stats() const
{
    stats s;
    s.total_dispatched       = total_dispatched_;
    s.queue_enqueue_failures = queue_enqueue_failures_;
    s.current_queue_size     = queue_.size_approx();
    s.max_queue_size         = worker_max_queue_size_;
    s.total_flushes          = worker_total_flushes_;
    s.messages_dropped       = messages_dropped_;
    s.queue_usage_percent    = (s.current_queue_size * 100.0f) / MAX_DISPATCH_QUEUE_SIZE;
    s.worker_iterations      = worker_iterations_;

    // Get active sinks count
    auto *config   = current_sinks_.load(std::memory_order_acquire);
    s.active_sinks = config ? config->sinks.size() : 0;

    // Calculate average dispatch time
    if (dispatch_count_for_avg_ > 0)
    {
        s.avg_dispatch_time_us = static_cast<double>(total_dispatch_time_us_) / dispatch_count_for_avg_;
    }
    else { s.avg_dispatch_time_us = 0.0; }

    // Get max dispatch time from worker thread local
    s.max_dispatch_time_us = worker_max_dispatch_time_us_;

    // Calculate uptime
    s.uptime = log_fast_timestamp() - start_time_;

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
    // Calculate message rates for different windows
    s.messages_per_second_1s  = calculate_rate_for_window(std::chrono::seconds(1));
    s.messages_per_second_10s = calculate_rate_for_window(std::chrono::seconds(10));
    s.messages_per_second_60s = calculate_rate_for_window(std::chrono::seconds(60));
    #endif

    // Calculate batch statistics from worker thread locals
    s.total_batches  = total_batches_;
    s.min_batch_size = worker_min_batch_size_;
    s.max_batch_size = worker_max_batch_size_;
    if (total_batches_ > 0) { s.avg_batch_size = static_cast<double>(total_batch_messages_) / total_batches_; }
    else { s.avg_batch_size = 0.0; }
    
    // Handle case where no batches have been processed
    if (s.min_batch_size == UINT64_MAX) { s.min_batch_size = 0; }

    // Calculate in-flight time statistics from worker thread locals
    s.min_inflight_time_us = worker_min_inflight_time_us_;
    s.max_inflight_time_us = worker_max_inflight_time_us_;

    // Handle case where no messages have been processed
    if (s.min_inflight_time_us == UINT64_MAX) { s.min_inflight_time_us = 0; }

    if (inflight_count_ > 0)
    {
        s.avg_inflight_time_us = static_cast<double>(total_inflight_time_us_) / inflight_count_;
    }
    else { s.avg_inflight_time_us = 0.0; }

    // Calculate dequeue timing statistics from worker thread locals
    s.min_dequeue_time_us = worker_min_dequeue_time_us_;
    s.max_dequeue_time_us = worker_max_dequeue_time_us_;
    
    // Handle case where no dequeues have occurred
    if (s.min_dequeue_time_us == UINT64_MAX) { s.min_dequeue_time_us = 0; }
    
    if (dequeue_count_ > 0)
    {
        s.avg_dequeue_time_us = static_cast<double>(total_dequeue_time_us_) / dequeue_count_;
    }
    else { s.avg_dequeue_time_us = 0.0; }
    
    return s;
}

// Reset statistics implementation
inline void log_line_dispatcher::reset_stats()
{
    total_dispatched_       = 0;
    queue_enqueue_failures_ = 0;
    messages_dropped_       = 0;
    worker_iterations_      = 0;
    total_dispatch_time_us_ = 0;
    dispatch_count_for_avg_ = 0;
    total_batches_          = 0;
    total_batch_messages_   = 0;
    total_inflight_time_us_ = 0;
    inflight_count_         = 0;
    total_dequeue_time_us_  = 0;
    dequeue_count_          = 0;
    worker_max_queue_size_ = 0;
    worker_total_flushes_ = 0;
    worker_max_dispatch_time_us_ = 0;
    worker_min_batch_size_ = UINT64_MAX;
    worker_max_batch_size_ = 0;
    worker_min_inflight_time_us_ = UINT64_MAX;
    worker_max_inflight_time_us_ = 0;
    worker_min_dequeue_time_us_ = UINT64_MAX;
    worker_max_dequeue_time_us_ = 0;

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
    rate_write_idx_        = 0;
    rate_sample_count_     = 0;
    last_rate_sample_time_ = std::chrono::steady_clock::time_point{};
    #endif
}
#endif

#ifdef LOG_COLLECT_DISPATCHER_METRICS
// Helper function implementations

inline void log_line_dispatcher::update_batch_stats(size_t dequeued_count)
{
    total_batches_++;
    total_batch_messages_ += dequeued_count;

    // Update min/max batch size (using non-atomic worker thread locals)
    update_min(worker_min_batch_size_, static_cast<uint64_t>(dequeued_count));
    update_max(worker_max_batch_size_, static_cast<uint64_t>(dequeued_count));
}

inline void log_line_dispatcher::track_inflight_times(log_buffer_base **buffers, size_t start_idx, size_t count)
{
    auto now = log_fast_timestamp();

    for (size_t j = 0; j < count; ++j)
    {
        log_buffer_base *proc_buffer = buffers[start_idx + j];
        if (proc_buffer && !proc_buffer->is_flush_marker())
        {
            auto inflight_us = std::chrono::duration_cast<std::chrono::microseconds>(now - proc_buffer->timestamp_).count();
            total_inflight_time_us_ += inflight_us;
            inflight_count_++;

            // Update min/max (using non-atomic worker thread locals)
            update_min(worker_min_inflight_time_us_, static_cast<uint64_t>(inflight_us));
            update_max(worker_max_inflight_time_us_, static_cast<uint64_t>(inflight_us));
        }
    }
}

inline void log_line_dispatcher::update_dispatch_timing_stats(std::chrono::steady_clock::time_point start, size_t message_count)
{
    auto dispatch_end = log_fast_timestamp();
    auto dispatch_us  = std::chrono::duration_cast<std::chrono::microseconds>(dispatch_end - start).count();
    total_dispatch_time_us_ += dispatch_us;
    dispatch_count_for_avg_ += message_count;

    // Update max dispatch time (per buffer average for batch)
    // This represents the time to process each buffer, not the total batch time
    // Example: 120µs to process 10 buffers = 12µs per buffer
    auto per_buffer_us = dispatch_us / message_count;
    update_max(worker_max_dispatch_time_us_, static_cast<uint64_t>(per_buffer_us));
}

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
inline void log_line_dispatcher::update_message_rate_sample()
{
    auto now = log_fast_timestamp();
    if (now - last_rate_sample_time_ >= RATE_SAMPLE_INTERVAL)
    {
        rate_samples_[rate_write_idx_] = {now, total_dispatched_};
        rate_write_idx_                = (rate_write_idx_ + 1) % RATE_WINDOW_SIZE;
        if (rate_sample_count_ < RATE_WINDOW_SIZE) rate_sample_count_++;
        last_rate_sample_time_ = now;
    }
}
    #endif
#endif // LOG_COLLECT_DISPATCHER_METRICS

} // namespace slwoggy