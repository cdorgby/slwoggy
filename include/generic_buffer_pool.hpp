/**
 * @file generic_buffer_pool.hpp
 * @brief Pool manager for generic buffer storage
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

// Forward declaration - actual buffers include this pool header
// No need to include any specific buffer implementation here
#include "moodycamel/concurrentqueue.h"
#include <memory>
#include <atomic>
#include <cstddef>
#include <cassert>

namespace slwoggy
{

/**
 * @brief Pool manager for pre-allocated buffer storage
 * 
 * Manages a contiguous block of memory divided into fixed-size buffer slots.
 * Each slot contains a reference count followed by the actual buffer data.
 * This design ensures zero heap allocation during normal operation.
 */
class generic_buffer_pool
{
public:
    static constexpr size_t DEFAULT_BUFFER_COUNT = 32 * 1024;  // 32K buffers
    static constexpr size_t DEFAULT_BUFFER_SIZE = 2048;        // 2KB per buffer
    
    struct stats
    {
        size_t total_buffers;      ///< Total number of buffers in pool
        size_t available_buffers;  ///< Number of buffers currently available
        size_t in_use_buffers;     ///< Number of buffers currently in use
        uint64_t total_acquires;   ///< Total acquire operations
        uint64_t acquire_failures; ///< Failed acquire attempts (pool exhausted)
        uint64_t total_releases;   ///< Total release operations
        size_t pool_memory_mb;     ///< Total memory used by pool in MB
        uint64_t high_water_mark;  ///< Maximum buffers ever in use
    };
    
private:
    const size_t buffer_count_;
    const size_t buffer_size_;
    const size_t total_size_;
    
    std::unique_ptr<char[]> storage_;                          ///< Contiguous storage for all buffers
    moodycamel::ConcurrentQueue<char*> available_buffers_;     ///< Queue of available buffer pointers
    
    // Statistics (only collected when metrics are enabled)
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
    std::atomic<uint64_t> total_acquires_{0};
    std::atomic<uint64_t> acquire_failures_{0};
    std::atomic<uint64_t> total_releases_{0};
    std::atomic<size_t> high_water_mark_{0};
#endif
    std::atomic<size_t> in_use_count_{0};  // Always needed for pool management
    
public:
    /**
     * @brief Construct a buffer pool with specified parameters
     * @param buffer_count Number of buffers to pre-allocate
     * @param buffer_size Size of each buffer in bytes
     */
    explicit generic_buffer_pool(size_t buffer_count = DEFAULT_BUFFER_COUNT,
                                 size_t buffer_size = DEFAULT_BUFFER_SIZE)
        : buffer_count_(buffer_count)
        , buffer_size_(buffer_size)
        , total_size_(buffer_count * buffer_size)
        , available_buffers_(buffer_count)
    {
        // Allocate contiguous storage for all buffers
        storage_ = std::make_unique<char[]>(total_size_);
        
        // Initialize available buffer queue
        for (size_t i = 0; i < buffer_count_; ++i)
        {
            char* buffer_storage = storage_.get() + (i * buffer_size_);
            available_buffers_.enqueue(buffer_storage);
        }
    }
    
    // Non-copyable, non-movable
    generic_buffer_pool(const generic_buffer_pool&) = delete;
    generic_buffer_pool& operator=(const generic_buffer_pool&) = delete;
    generic_buffer_pool(generic_buffer_pool&&) = delete;
    generic_buffer_pool& operator=(generic_buffer_pool&&) = delete;
    
    /**
     * @brief Acquire storage for a new buffer
     * @return Pointer to buffer storage, or nullptr if pool is exhausted
     */
    char* acquire_storage()
    {
        char* storage = nullptr;
        if (available_buffers_.try_dequeue(storage))
        {
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
            total_acquires_.fetch_add(1, std::memory_order_relaxed);
#endif
            
            size_t in_use = in_use_count_.fetch_add(1, std::memory_order_relaxed) + 1;
            
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
            // Update high water mark
            size_t prev_high = high_water_mark_.load(std::memory_order_relaxed);
            while (in_use > prev_high)
            {
                if (high_water_mark_.compare_exchange_weak(prev_high, in_use,
                                                          std::memory_order_relaxed,
                                                          std::memory_order_relaxed))
                {
                    break;
                }
            }
#else
            (void)in_use;  // Avoid unused variable warning
#endif
            
            return storage;
        }
        
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
        acquire_failures_.fetch_add(1, std::memory_order_relaxed);
#endif
        return nullptr;
    }
    
    /**
     * @brief Return storage to the pool
     * @param storage Pointer to the beginning of buffer storage
     */
    void return_storage(char* storage)
    {
        assert(storage >= storage_.get() && 
               storage < storage_.get() + total_size_);
        assert((storage - storage_.get()) % buffer_size_ == 0);
        
        // Clear the buffer for security/debugging (optional)
        // std::memset(storage, 0, buffer_size_);
        
        available_buffers_.enqueue(storage);
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
        total_releases_.fetch_add(1, std::memory_order_relaxed);
#endif
        in_use_count_.fetch_sub(1, std::memory_order_relaxed);
    }
    
    /**
     * @brief Get pool statistics
     * @return Current statistics snapshot
     */
    stats get_stats() const
    {
        stats s;
        s.total_buffers = buffer_count_;
        s.in_use_buffers = in_use_count_.load(std::memory_order_relaxed);
        s.available_buffers = s.total_buffers - s.in_use_buffers;
        s.pool_memory_mb = total_size_ / (1024 * 1024);
        
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
        s.total_acquires = total_acquires_.load(std::memory_order_relaxed);
        s.acquire_failures = acquire_failures_.load(std::memory_order_relaxed);
        s.total_releases = total_releases_.load(std::memory_order_relaxed);
        s.high_water_mark = high_water_mark_.load(std::memory_order_relaxed);
#else
        s.total_acquires = 0;
        s.acquire_failures = 0;
        s.total_releases = 0;
        s.high_water_mark = s.in_use_buffers;
#endif
        return s;
    }
    
    /**
     * @brief Reset statistics counters
     */
    void reset_stats()
    {
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
        total_acquires_.store(0, std::memory_order_relaxed);
        acquire_failures_.store(0, std::memory_order_relaxed);
        total_releases_.store(0, std::memory_order_relaxed);
        high_water_mark_.store(in_use_count_.load(std::memory_order_relaxed),
                              std::memory_order_relaxed);
#endif
    }
    
    /**
     * @brief Get the size of each buffer
     */
    size_t get_buffer_size() const { return buffer_size_; }
    
    /**
     * @brief Get total number of buffers in pool
     */
    size_t get_buffer_count() const { return buffer_count_; }
};

} // namespace slwoggy