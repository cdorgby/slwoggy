/**
 * @file log_filters.hpp
 * @brief Concrete filter implementations for log processing
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 * 
 * ============================================================================
 * WARNING: EXAMPLE FILTERS ONLY - NOT FOR PRODUCTION USE
 * ============================================================================
 * 
 * The filters in this file are PROVIDED AS EXAMPLES ONLY to demonstrate
 * the filtering API and for testing the filter infrastructure.
 * 
 * These implementations are:
 * - Not optimized for production performance
 * - Not thread-safe beyond the dispatcher's single worker thread
 * - Missing important production features (persistence, metrics, etc.)
 * - Intended only for testing and demonstration purposes
 * 
 * For production use, implement your own filters based on your specific
 * requirements with proper:
 * - Performance optimization
 * - Monitoring and metrics
 * - Configuration management
 * - Error handling and recovery
 * 
 * ============================================================================
 */
#pragma once

#include "log_filter.hpp"
#include "log_types.hpp"  // For log_fast_timestamp
#include <unordered_map>
#include <chrono>
#include <random>
#include <atomic>

namespace slwoggy
{

/**
 * @brief Deduplication filter - drops duplicate messages within a time window
 * 
 * Tracks message hashes and drops duplicates seen within the specified window.
 * Uses a simple hash of the message text for deduplication.
 * Note: This filter is only called from the dispatcher's worker thread.
 */
class dedup_filter : public log_filter
{
private:
    struct message_info {
        std::chrono::steady_clock::time_point last_seen;
        uint32_t count;
    };
    
    std::chrono::milliseconds window_;
    std::unordered_map<size_t, message_info> seen_messages_;
    std::chrono::steady_clock::time_point last_cleanup_;
    
    static constexpr auto CLEANUP_INTERVAL = std::chrono::seconds(10);
    
    size_t hash_buffer(const log_buffer_base* buffer) const
    {
        if (!buffer || buffer->is_flush_marker()) return 0;
        
        // Direct FNV-1a hash on message data without temporary objects
        auto text = buffer->get_message();
        size_t hash = 14695981039346656037ULL; // FNV-1a offset basis
        for (char c : text) {
            hash ^= static_cast<size_t>(c);
            hash *= 1099511628211ULL; // FNV-1a prime
        }
        return hash;
    }
    
    void cleanup_old_entries()
    {
        auto now = log_fast_timestamp();
        if (now - last_cleanup_ < CLEANUP_INTERVAL) return;
        
        auto cutoff = now - window_;
        auto it = seen_messages_.begin();
        while (it != seen_messages_.end())
        {
            if (it->second.last_seen < cutoff)
            {
                it = seen_messages_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        last_cleanup_ = now;
    }
    
public:
    explicit dedup_filter(std::chrono::milliseconds window = std::chrono::seconds(5))
        : window_(window), last_cleanup_(log_fast_timestamp())
    {}
    
    void process_batch(log_buffer_base** buffers, size_t count) override
    {
        auto now = log_fast_timestamp();
        
        // Periodic cleanup
        cleanup_old_entries();
        
        for (size_t i = 0; i < count; ++i)
        {
            auto* buffer = buffers[i];
            
            // Skip null buffers and flush markers
            if (!buffer || buffer->is_flush_marker())
            {
                continue;
            }
            
            auto hash = hash_buffer(buffer);
            auto it = seen_messages_.find(hash);
            
            if (it != seen_messages_.end())
            {
                // Check if within dedup window
                if (now - it->second.last_seen < window_)
                {
                    // Duplicate within window - mark as filtered
                    buffer->filtered_ = true;
                    it->second.count++;
                    it->second.last_seen = now;
                }
                else
                {
                    // Outside window - pass and update
                    it->second.last_seen = now;
                    it->second.count = 1;
                }
            }
            else
            {
                // First time seeing this message
                seen_messages_[hash] = {now, 1};
            }
        }
    }
    
    void shutdown() override
    {
        seen_messages_.clear();
    }
    
    const char* name() const override { return "dedup"; }
    
    void reset() override
    {
        seen_messages_.clear();
        last_cleanup_ = log_fast_timestamp();
    }
};

/**
 * @brief Statistical sampling filter - randomly drops messages based on rate
 * 
 * Passes through a percentage of messages randomly.
 * Useful for high-volume logging where sampling is acceptable.
 * Note: This filter is only called from the dispatcher's worker thread.
 */
class sampler_filter : public log_filter
{
private:
    double sample_rate_;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_;
    
public:
    explicit sampler_filter(double sample_rate = 0.1)  // Default 10% sampling
        : sample_rate_(std::max(0.0, std::min(1.0, sample_rate))),
          rng_(std::random_device{}()),
          dist_(0.0, 1.0)
    {}
    
    void process_batch(log_buffer_base** buffers, size_t count) override
    {
        for (size_t i = 0; i < count; ++i)
        {
            auto* buffer = buffers[i];
            
            // Skip null buffers and flush markers
            if (!buffer || buffer->is_flush_marker())
            {
                continue;
            }
            
            // Random sampling - mark as filtered if not selected
            if (dist_(rng_) >= sample_rate_)
            {
                buffer->filtered_ = true;
            }
        }
    }
    
    void shutdown() override {}
    
    const char* name() const override { return "sampler"; }
    
    void reset() override
    {
        rng_.seed(std::random_device{}());
    }
};

/**
 * @brief Rate limiting filter - limits messages per time window
 * 
 * Enforces a maximum number of messages per time window.
 * Uses a token bucket algorithm with integer arithmetic for performance.
 * Note: This filter is only called from the dispatcher's worker thread.
 */
class rate_limit_filter : public log_filter
{
private:
    static constexpr int64_t NANOS_PER_SECOND = 1000000000LL;
    
    int64_t max_per_second_;
    int64_t tokens_in_nanos_;  // Tokens * NANOS_PER_SECOND for precision
    std::chrono::steady_clock::time_point last_refill_;
    
public:
    explicit rate_limit_filter(size_t max_per_second = 1000)
        : max_per_second_(static_cast<int64_t>(max_per_second)),
          tokens_in_nanos_(static_cast<int64_t>(max_per_second) * NANOS_PER_SECOND),
          last_refill_(log_fast_timestamp())
    {}
    
    void process_batch(log_buffer_base** buffers, size_t count) override
    {
        // Refill tokens based on elapsed time using integer arithmetic
        auto now = log_fast_timestamp();
        auto elapsed_nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_refill_).count();
        
        if (elapsed_nanos > 0)
        {
            // Add tokens: elapsed_nanos * max_per_second / NANOS_PER_SECOND
            // But we keep tokens scaled by NANOS_PER_SECOND for precision
            int64_t new_tokens = elapsed_nanos * max_per_second_;
            tokens_in_nanos_ += new_tokens;
            
            // Cap at bucket size (1 second worth of messages)
            int64_t max_tokens_in_nanos = max_per_second_ * NANOS_PER_SECOND;
            if (tokens_in_nanos_ > max_tokens_in_nanos) {
                tokens_in_nanos_ = max_tokens_in_nanos;
            }
            last_refill_ = now;
        }
        
        for (size_t i = 0; i < count; ++i)
        {
            auto* buffer = buffers[i];
            
            // Skip null buffers and flush markers
            if (!buffer || buffer->is_flush_marker())
            {
                continue;
            }
            
            // Check if we have tokens available (1 token = NANOS_PER_SECOND in our scale)
            if (tokens_in_nanos_ >= NANOS_PER_SECOND)
            {
                tokens_in_nanos_ -= NANOS_PER_SECOND;
            }
            else
            {
                // No tokens - mark as filtered
                buffer->filtered_ = true;
            }
        }
    }
    
    void shutdown() override {}
    
    const char* name() const override { return "rate_limit"; }
    
    void reset() override
    {
        last_refill_ = log_fast_timestamp();
        tokens_in_nanos_ = max_per_second_ * NANOS_PER_SECOND;
    }
};

/**
 * @brief Test filter that passes or drops all messages
 * 
 * Used for testing the filter infrastructure.
 */
class test_filter : public log_filter
{
private:
    bool drop_all_;
    std::atomic<size_t> processed_count_{0};
    
public:
    explicit test_filter(bool drop_all = false)
        : drop_all_(drop_all)
    {}
    
    void process_batch(log_buffer_base** buffers, size_t count) override
    {
        for (size_t i = 0; i < count; ++i)
        {
            auto* buffer = buffers[i];
            
            // Skip null buffers and flush markers
            if (!buffer || buffer->is_flush_marker())
            {
                continue;
            }
            
            if (drop_all_)
            {
                buffer->filtered_ = true;
            }
            processed_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void shutdown() override {}
    
    const char* name() const override { return "test"; }
    
    void reset() override
    {
        processed_count_.store(0, std::memory_order_relaxed);
    }
    
    size_t get_processed_count() const
    {
        return processed_count_.load(std::memory_order_relaxed);
    }
    
    void set_drop_all(bool drop)
    {
        drop_all_ = drop;
    }
};

} // namespace slwoggy