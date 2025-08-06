/**
 * @file log_buffer.hpp
 * @brief Log buffer management and structured logging support
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <array>
#include <chrono>
#include <atomic>
#include <fmt/format.h>
#include <memory>

#include "moodycamel/concurrentqueue.h"

#include "log_types.hpp"
#include "log_structured.hpp"

namespace slwoggy
{


// Cache-aligned buffer structure
struct alignas(CACHE_LINE_SIZE) log_buffer
{
    log_level level_{log_level::nolog}; // Default to no logging level
    std::string_view file_;
    uint32_t line_;
    std::chrono::steady_clock::time_point timestamp_;

    size_t write_pos_{log_buffer_metadata_adapter::TEXT_START}; // Start writing text after metadata area
    size_t header_width_{0};                                    // Width of header for extracting just the message

    constexpr size_t size() const { return data_.size(); }
    constexpr size_t len() const { return write_pos_ - log_buffer_metadata_adapter::TEXT_START; }
    constexpr size_t available() const { return size() - write_pos_; }

    // Get metadata length from header
    size_t len_meta() const
    {
        const auto metadata = get_metadata_adapter();
        return metadata.get_header()->metadata_size;
    }

    // Get count of key-value pairs in metadata
    size_t get_kv_count() const
    {
        const auto metadata = get_metadata_adapter();
        return metadata.get_header()->kv_count;
    }

    void add_ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    void release();

    // Get metadata adapter
    log_buffer_metadata_adapter get_metadata_adapter() { return log_buffer_metadata_adapter(data_.data()); }

    // Get const metadata adapter for reading
    const log_buffer_metadata_adapter get_metadata_adapter() const
    {
        return log_buffer_metadata_adapter(const_cast<char *>(data_.data()));
    }

    // Reset buffer for reuse
    void reset()
    {
        write_pos_    = log_buffer_metadata_adapter::TEXT_START; // Reset to start of text area
        level_        = log_level::nolog;
        header_width_ = 0;

        // Reset metadata via adapter
        auto metadata = get_metadata_adapter();
        metadata.reset();
    }

    // Write raw data to buffer
    size_t write_raw(std::string_view str)
    {
        if (str.empty()) return 0;

        size_t available = size() - write_pos_;
        size_t to_write  = std::min(str.size(), available);

        if (to_write > 0)
        {
            std::memcpy(data_.data() + write_pos_, str.data(), to_write);
            write_pos_ += to_write;
        }

        return to_write;
    }

    // Append a character if there's room, otherwise replace the last character
    void append_or_replace_last(char c)
    {
        if (write_pos_ < size())
        {
            // Room to append
            data_[write_pos_++] = c;
        }
        else if (write_pos_ > log_buffer_metadata_adapter::TEXT_START)
        {
            // Buffer full - replace last character
            data_[write_pos_ - 1] = c;
        }
        // else: no text to replace, do nothing
    }

    // Write string with newline padding
    void write_with_padding(std::string_view str)
    {
        size_t start = 0;
        size_t pos   = str.find('\n');

        while (pos != std::string_view::npos)
        {
            // Write up to and including newline
            write_raw(str.substr(start, pos - start + 1));

            // If there's more text after newline, add padding
            if (pos + 1 < str.size()) { write_raw(std::string(header_width_, ' ')); }

            start = pos + 1;
            pos   = str.find('\n', start);
        }

        // Write remainder
        if (start < str.size()) { write_raw(str.substr(start)); }
    }

    // Handle newline padding for text already written to buffer
    void handle_newline_padding(size_t start_pos)
    {
        if (write_pos_ <= start_pos) return;

        // Scan for newlines in the text we just wrote
        size_t pos = start_pos;

        while (pos < write_pos_)
        {
            // Find next newline
            size_t newline_pos = pos;
            while (newline_pos < write_pos_ && data_[newline_pos] != '\n') { newline_pos++; }

            if (newline_pos < write_pos_ && newline_pos + 1 < write_pos_)
            {
                // Found a newline with text after it - need to insert padding
                size_t remaining = write_pos_ - (newline_pos + 1);

                // Check if we have room for padding
                size_t available = size() - write_pos_;
                if (available >= header_width_)
                {
                    // Shift remaining text to make room for padding
                    std::memmove(data_.data() + newline_pos + 1 + header_width_, data_.data() + newline_pos + 1, remaining);

                    // Insert padding spaces
                    std::memset(data_.data() + newline_pos + 1, ' ', header_width_);

                    // Update our write position
                    write_pos_ += header_width_;
                    newline_pos += header_width_;
                }
            }

            pos = newline_pos + 1;
        }
    }

    // Printf directly into buffer with newline padding
    template <typename... Args> void printf_with_padding(const char *format, Args &&...args)
    {
        // Remember where we start writing
        size_t start_pos = write_pos_;

        // Check if we have enough space for at least some of the output
        size_t available = size() - write_pos_;
        if (available == 0) return;

        // Format directly into the buffer at current position
        int written = snprintf(data_.data() + write_pos_,
                               available, // snprintf needs full available size including null terminator
                               format,
                               std::forward<Args>(args)...);

        if (written > 0)
        {
            if (written < static_cast<int>(available))
            {
                // Advance write position by what was actually written (not including null terminator)
                // When written < available, the entire string fit
                write_pos_ += written;
            }
            else
            {
                // String was truncated, advance by available-1 (snprintf wrote available-1 chars + null)
                write_pos_ += available - 1;
            }

            // Now handle newline padding in-place
            handle_newline_padding(start_pos);
        }
    }

    // Format directly into buffer using fmt with newline padding
    template <typename... Args> void format_to_buffer_with_padding(fmt::format_string<Args...> fmt, Args &&...args)
    {
        // Remember where we start writing
        size_t start_pos = write_pos_;

        // Check if we have enough space for at least some of the output
        size_t available = size() - write_pos_;
        if (available == 0) return;

        // Format directly into the buffer at current position
        char *output_start = data_.data() + write_pos_;
        auto result = fmt::format_to_n(output_start, available, fmt, std::forward<Args>(args)...);

        // Update write position based on what would have been written (capped by available space)
        size_t written = std::min(result.size, available);
        write_pos_ += written;

        // Now handle newline padding in-place
        handle_newline_padding(start_pos);
    }

    bool is_flush_marker() const { return level_ == log_level::nolog && len() == 0; }

    // Get text content (skipping metadata)
    std::string_view get_text() const
    {
        return std::string_view(data_.data() + log_buffer_metadata_adapter::TEXT_START,
                                write_pos_ - log_buffer_metadata_adapter::TEXT_START);
    }

    // Get just the message text (skipping metadata and header)
    std::string_view get_message() const
    {
        size_t message_start = log_buffer_metadata_adapter::TEXT_START + header_width_;
        if (message_start >= write_pos_)
        {
            return std::string_view(); // No message, just header
        }
        return std::string_view(data_.data() + message_start, write_pos_ - message_start);
    }

  private:
    std::array<char, LOG_BUFFER_SIZE> data_;
    std::atomic<int> ref_count_{0};
};

// Buffer pool singleton class
class buffer_pool
{
  public:
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
    /**
     * @brief Buffer pool statistics for monitoring and diagnostics
     */
    struct stats
    {
        size_t total_buffers;      ///< Total number of buffers in pool
        size_t available_buffers;  ///< Number of buffers currently available
        size_t in_use_buffers;     ///< Number of buffers currently in use
        uint64_t total_acquires;   ///< Total acquire operations
        uint64_t acquire_failures; ///< Failed acquire attempts (pool exhausted)
        uint64_t total_releases;   ///< Total release operations
        float usage_percent;       ///< Pool usage percentage
        size_t pool_memory_kb;     ///< Total memory used by buffer pool
        uint64_t high_water_mark;  ///< Maximum buffers ever in use

        // Buffer area usage statistics
        struct area_stats
        {
            size_t min_bytes;      ///< Minimum bytes used
            size_t max_bytes;      ///< Maximum bytes used
            double avg_bytes;      ///< Average bytes used
            uint64_t sample_count; ///< Number of samples
        };

        area_stats metadata_usage; ///< Metadata area usage stats
        area_stats text_usage;     ///< Text area usage stats
        area_stats total_usage;    ///< Total buffer usage stats
    };
#endif

  private:
    std::unique_ptr<log_buffer[]> buffer_storage_;
    moodycamel::ConcurrentQueue<log_buffer *> available_buffers_;

#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
    // Statistics tracking
    std::atomic<uint64_t> total_acquires_{0};
    std::atomic<uint64_t> acquire_failures_{0};
    std::atomic<uint64_t> total_releases_{0};
    std::atomic<uint64_t> buffers_in_use_{0};
    std::atomic<uint64_t> high_water_mark_{0};

    // Area usage tracking - using atomics for lock-free updates
    std::atomic<uint64_t> metadata_total_bytes_{0};
    std::atomic<uint64_t> metadata_min_bytes_{UINT64_MAX};
    std::atomic<uint64_t> metadata_max_bytes_{0};

    std::atomic<uint64_t> text_total_bytes_{0};
    std::atomic<uint64_t> text_min_bytes_{UINT64_MAX};
    std::atomic<uint64_t> text_max_bytes_{0};

    std::atomic<uint64_t> total_bytes_used_{0};
    std::atomic<uint64_t> total_min_bytes_{UINT64_MAX};
    std::atomic<uint64_t> total_max_bytes_{0};

    std::atomic<uint64_t> usage_samples_{0};
#endif

    buffer_pool()
    {
        buffer_storage_ = std::make_unique<log_buffer[]>(BUFFER_POOL_SIZE);
        for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i) { available_buffers_.enqueue(&buffer_storage_[i]); }
    }

  public:
    static buffer_pool &instance()
    {
        static buffer_pool instance;
        return instance;
    }

    log_buffer *acquire()
    {
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
        total_acquires_.fetch_add(1, std::memory_order_relaxed);
#endif

        log_buffer *buffer = nullptr;
        if (available_buffers_.try_dequeue(buffer) && buffer)
        {
            buffer->add_ref();

#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
            // Update usage tracking
            auto in_use = buffers_in_use_.fetch_add(1, std::memory_order_relaxed) + 1;

            // Update high water mark
            uint64_t current_hwm = high_water_mark_.load(std::memory_order_relaxed);
            while (in_use > current_hwm)
            {
                if (high_water_mark_.compare_exchange_weak(current_hwm, in_use, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    break;
                }
            }
#endif
        }
        else
        {
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
            acquire_failures_.fetch_add(1, std::memory_order_relaxed);
#endif
        }
        return buffer;
    }

    void release(log_buffer *buffer)
    {
        if (buffer)
        {
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
            total_releases_.fetch_add(1, std::memory_order_relaxed);
            buffers_in_use_.fetch_sub(1, std::memory_order_relaxed);
#endif
            available_buffers_.enqueue(buffer);
        }
    }

#ifdef LOG_COLLECT_BUFFER_POOL_METRICS

  public:
    /**
     * @brief Track buffer usage statistics
     * @param buffer Buffer being released back to pool
     */
    void track_buffer_usage(const log_buffer *buffer)
    {
        if (!buffer) return;

        // Skip flush markers as they're not real log messages
        if (buffer->is_flush_marker()) return;

        // Get usage sizes
        size_t metadata_bytes = buffer->len_meta();
        size_t text_bytes     = buffer->len();
        size_t total_bytes    = metadata_bytes + text_bytes;

        // Skip completely empty buffers (no content was ever written)
        if (total_bytes == 0) return;

        // Update totals for averaging
        metadata_total_bytes_.fetch_add(metadata_bytes, std::memory_order_relaxed);
        text_total_bytes_.fetch_add(text_bytes, std::memory_order_relaxed);
        total_bytes_used_.fetch_add(total_bytes, std::memory_order_relaxed);
        usage_samples_.fetch_add(1, std::memory_order_relaxed);

        // Update min/max for metadata
        update_min(metadata_min_bytes_, metadata_bytes);
        update_max(metadata_max_bytes_, metadata_bytes);

        // Update min/max for text
        update_min(text_min_bytes_, text_bytes);
        update_max(text_max_bytes_, text_bytes);

        // Update min/max for total
        update_min(total_min_bytes_, total_bytes);
        update_max(total_max_bytes_, total_bytes);
    }

    void update_min(std::atomic<uint64_t> &min_var, uint64_t value)
    {
        uint64_t current_min = min_var.load(std::memory_order_relaxed);
        while (value < current_min)
        {
            if (min_var.compare_exchange_weak(current_min, value, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                break;
            }
        }
    }

    void update_max(std::atomic<uint64_t> &max_var, uint64_t value)
    {
        uint64_t current_max = max_var.load(std::memory_order_relaxed);
        while (value > current_max)
        {
            if (max_var.compare_exchange_weak(current_max, value, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                break;
            }
        }
    }

  public:
    /**
     * @brief Get current buffer pool statistics
     * @return Statistics snapshot
     */
    stats get_stats() const
    {
        stats s;
        s.total_buffers     = BUFFER_POOL_SIZE;
        s.in_use_buffers    = buffers_in_use_.load(std::memory_order_relaxed);
        s.available_buffers = s.total_buffers - s.in_use_buffers;
        s.total_acquires    = total_acquires_.load(std::memory_order_relaxed);
        s.acquire_failures  = acquire_failures_.load(std::memory_order_relaxed);
        s.total_releases    = total_releases_.load(std::memory_order_relaxed);
        s.usage_percent     = (s.in_use_buffers * 100.0f) / s.total_buffers;
        s.pool_memory_kb    = (s.total_buffers * LOG_BUFFER_SIZE) / 1024;
        s.high_water_mark   = high_water_mark_.load(std::memory_order_relaxed);

        // Populate area usage stats
        uint64_t samples = usage_samples_.load(std::memory_order_relaxed);
        if (samples > 0)
        {
            // Metadata usage
            s.metadata_usage.sample_count = samples;
            s.metadata_usage.min_bytes    = metadata_min_bytes_.load(std::memory_order_relaxed);
            s.metadata_usage.max_bytes    = metadata_max_bytes_.load(std::memory_order_relaxed);
            s.metadata_usage.avg_bytes = static_cast<double>(metadata_total_bytes_.load(std::memory_order_relaxed)) / samples;

            // Text usage
            s.text_usage.sample_count = samples;
            s.text_usage.min_bytes    = text_min_bytes_.load(std::memory_order_relaxed);
            s.text_usage.max_bytes    = text_max_bytes_.load(std::memory_order_relaxed);
            s.text_usage.avg_bytes = static_cast<double>(text_total_bytes_.load(std::memory_order_relaxed)) / samples;

            // Total usage
            s.total_usage.sample_count = samples;
            s.total_usage.min_bytes    = total_min_bytes_.load(std::memory_order_relaxed);
            s.total_usage.max_bytes    = total_max_bytes_.load(std::memory_order_relaxed);
            s.total_usage.avg_bytes = static_cast<double>(total_bytes_used_.load(std::memory_order_relaxed)) / samples;
        }
        else
        {
            // No samples yet - initialize to zero
            s.metadata_usage = {};
            s.text_usage     = {};
            s.total_usage    = {};
        }

        return s;
    }

    /**
     * @brief Reset statistics counters (useful for testing)
     */
    void reset_stats()
    {
        total_acquires_.store(0, std::memory_order_relaxed);
        acquire_failures_.store(0, std::memory_order_relaxed);
        total_releases_.store(0, std::memory_order_relaxed);
        // Note: don't reset buffers_in_use_ or high_water_mark_ as they reflect current state

        // Reset area usage stats
        metadata_total_bytes_.store(0, std::memory_order_relaxed);
        metadata_min_bytes_.store(UINT64_MAX, std::memory_order_relaxed);
        metadata_max_bytes_.store(0, std::memory_order_relaxed);

        text_total_bytes_.store(0, std::memory_order_relaxed);
        text_min_bytes_.store(UINT64_MAX, std::memory_order_relaxed);
        text_max_bytes_.store(0, std::memory_order_relaxed);

        total_bytes_used_.store(0, std::memory_order_relaxed);
        total_min_bytes_.store(UINT64_MAX, std::memory_order_relaxed);
        total_max_bytes_.store(0, std::memory_order_relaxed);

        usage_samples_.store(0, std::memory_order_relaxed);
    }
#endif
};

inline void log_buffer::release()
{
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        // Last reference - track usage before reset
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
        buffer_pool::instance().track_buffer_usage(this);
#endif
        // Reset buffer before returning to pool
        reset();
        buffer_pool::instance().release(this);
    }
}

} // namespace slwoggy