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
#include <cassert>

#include "moodycamel/blockingconcurrentqueue.h" // IWYU pragma: keep
#include "moodycamel/concurrentqueue.h"         // IWYU pragma: keep

#include "log_types.hpp"

// Forward declarations (must be in slwoggy namespace)
namespace slwoggy
{
class log_buffer_metadata_adapter;
}

namespace slwoggy
{

// Base buffer class with all logic
class log_buffer_base
{
public:
    // Buffer header (first bytes of data_)
    struct buffer_header
    {
        uint16_t rec_size;     // Total allocated size
        uint16_t text_len;     // Length of text data
        uint16_t metadata_off; // Offset where metadata starts
    };

    static constexpr size_t HEADER_SIZE = sizeof(buffer_header);

    log_level level_{log_level::nolog};
    uint32_t line_{0};
    size_t header_width_{0};
    bool padding_enabled_{false}; // true for human-readable format with padding, false for structured/logfmt
    bool filtered_{false}; // Set by filters to indicate this message should be dropped
    
protected:
    size_t text_pos_{HEADER_SIZE};
    size_t metadata_pos_;
    std::atomic<int> ref_count_{0};
    
    // Data pointers - accessed together
    char* data_;
    size_t capacity_;
    
public:
    // Cold metadata - less frequently accessed
    std::string_view file_;
    std::chrono::steady_clock::time_point timestamp_;

protected:
    // Protected constructor - prevents default construction
    log_buffer_base(char* data, size_t capacity) noexcept
        : metadata_pos_(capacity), data_(data), capacity_(capacity) {
        // In debug builds, validate inputs
        assert(data != nullptr);
        assert(capacity > HEADER_SIZE);
    }
    
    // Delete default constructor - force proper initialization
    log_buffer_base() = delete;
    
public:
    // Delete copy operations - prevent aliasing issues with raw pointer
    log_buffer_base(const log_buffer_base&) = delete;
    log_buffer_base& operator=(const log_buffer_base&) = delete;
    
    // Delete move operations - pooled objects shouldn't move
    log_buffer_base(log_buffer_base&&) = delete;
    log_buffer_base& operator=(log_buffer_base&&) = delete;

    ~log_buffer_base() = default;

    constexpr size_t size() const noexcept { return capacity_; }
    constexpr size_t len() const noexcept { return text_pos_ - HEADER_SIZE; }
    constexpr size_t available() const noexcept { return metadata_pos_ - text_pos_; }

    // Get buffer header
    buffer_header *get_header() noexcept { return reinterpret_cast<buffer_header *>(data_); }
    const buffer_header *get_header() const noexcept { return reinterpret_cast<const buffer_header *>(data_); }

    // Get metadata length
    size_t len_meta() const
    {
        const auto *header = get_header();
        return capacity_ - header->metadata_off;
    }

    // Get count of key-value pairs in metadata
    size_t get_kv_count() const
    {
        if (metadata_pos_ >= capacity_) return 0; // No metadata
        // First byte of metadata is the count
        return static_cast<size_t>(data_[metadata_pos_]);
    }

    void add_ref() noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    void release();

    // Friend class for metadata adapter
    friend class log_buffer_metadata_adapter;

    // Get metadata adapter - implemented in log_structured.hpp
    inline log_buffer_metadata_adapter get_metadata_adapter();
    inline log_buffer_metadata_adapter get_metadata_adapter() const;

    // Reset buffer for reuse
    void reset() noexcept
    {
        text_pos_     = HEADER_SIZE;
        metadata_pos_ = capacity_;
        level_        = log_level::nolog;
        header_width_ = 0;
        filtered_     = false;  // Clear filter state on reset

        // Initialize header
        auto *header         = get_header();
        header->rec_size     = static_cast<uint16_t>(capacity_);
        header->text_len     = 0;
        header->metadata_off = static_cast<uint16_t>(capacity_);
    }

    // Write raw data to buffer (text grows forward)
    size_t write_raw(std::string_view str)
    {
        if (str.empty()) return 0;

        // If padding is enabled (human-readable mode), just write as-is
        if (padding_enabled_) {
            size_t available_space = metadata_pos_ - text_pos_;
            size_t to_write = std::min(str.size(), available_space);
            
            if (to_write > 0) {
                std::memcpy(data_ + text_pos_, str.data(), to_write);
                text_pos_ += to_write;
                get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);
            }
            return to_write;
        }

        // Structured/logfmt mode - escape newlines
        size_t start = 0;
        size_t pos = str.find('\n');
        
        // No newlines? Write everything as-is
        if (pos == std::string_view::npos) {
            size_t available_space = metadata_pos_ - text_pos_;
            size_t to_write = std::min(str.size(), available_space);
            
            if (to_write > 0) {
                std::memcpy(data_ + text_pos_, str.data(), to_write);
                text_pos_ += to_write;
                get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);
            }
            return to_write;
        }
        
        // Has newlines - escape them
        size_t total_written = 0;
        while (pos != std::string_view::npos) {
            // Write text before newline
            if (pos > start) {
                size_t available_space = metadata_pos_ - text_pos_;
                size_t chunk_size = pos - start;
                size_t to_write = std::min(chunk_size, available_space);
                
                if (to_write > 0) {
                    std::memcpy(data_ + text_pos_, str.data() + start, to_write);
                    text_pos_ += to_write;
                    total_written += to_write;
                }
            }
            
            // Write escaped newline
            size_t available_space = metadata_pos_ - text_pos_;
            if (available_space >= 2) {
                data_[text_pos_++] = '\\';
                data_[text_pos_++] = 'n';
                total_written += 2;
            }
            
            start = pos + 1;
            pos = str.find('\n', start);
        }
        
        // Write remainder
        if (start < str.size()) {
            size_t available_space = metadata_pos_ - text_pos_;
            size_t remainder = str.size() - start;
            size_t to_write = std::min(remainder, available_space);
            
            if (to_write > 0) {
                std::memcpy(data_ + text_pos_, str.data() + start, to_write);
                text_pos_ += to_write;
                total_written += to_write;
            }
        }
        
        get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);
        return total_written;
    }

    // Append a character if there's room, otherwise replace the last character
    // Only adds the character if it's not already the last character in the buffer
    void append_or_replace_last(char c)
    {
        // Check if we have text content and the character is already the last character
        size_t text_len = text_pos_ - HEADER_SIZE;
        if (text_len > 0 && data_[text_pos_ - 1] == c)
        {
            // Character is already at the end, nothing to do
            return;
        }
        
        if (text_pos_ < metadata_pos_)
        {
            // Room to append
            data_[text_pos_++]     = c;
            get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);
        }
        else if (text_len > 0)
        {
            // Buffer full but has text - replace last character
            data_[text_pos_ - 1] = c;
        }
        // else: no text to replace, do nothing
    }

    // Write string with newline padding
    void write_with_padding(std::string_view str, bool start_with_newline_and_pad = false)
    {
        // If requested, start with newline and padding
        if (start_with_newline_and_pad) {
            write_raw("\n");
            if (padding_enabled_) {
                write_raw(std::string(header_width_, ' '));
            }
        }
        
        if (!padding_enabled_) {
            write_raw(str);
            return;
        }

        size_t start = 0;
        size_t pos   = str.find('\n');

        while (pos != std::string_view::npos)
        {
            // Write up to and including newline
            write_raw(str.substr(start, pos - start + 1));

            // If there's more text after newline, add padding
            if (pos + 1 < str.size())
            {
                write_raw(std::string(header_width_, ' '));
            }

            start = pos + 1;
            pos   = str.find('\n', start);
        }

        // Write remainder
        if (start < str.size()) { write_raw(str.substr(start)); }
    }

    // Handle newline padding for text already written to buffer
    void handle_newline_padding(size_t start_pos)
    {
        if (text_pos_ <= start_pos) return;

        if (!padding_enabled_) {
            // Escape newlines for structured/logfmt mode
            size_t pos = start_pos;
            while (pos < text_pos_) {
                if (data_[pos] == '\n') {
                    // Need to replace '\n' with "\\n" (adds 1 byte)
                    size_t available = metadata_pos_ - text_pos_;
                    if (available >= 1) {
                        size_t remaining = text_pos_ - (pos + 1);
                        if (remaining > 0) {
                            // Bounds check before memmove
                            assert(pos + 2 + remaining <= capacity_);
                            // Shift text after newline to make room for extra byte
                            std::memmove(data_ + pos + 2, data_ + pos + 1, remaining);
                        }
                        // Replace newline with escaped version
                        data_[pos] = '\\';
                        data_[pos + 1] = 'n';
                        text_pos_++;
                        get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);
                        pos += 2; // Skip past the escaped newline
                    } else {
                        // No room to expand - truncate to ensure complete escape
                        // Better to lose one char than have incomplete escape sequence
                        if (pos + 1 < text_pos_) {
                            // We have room for complete \n
                            data_[pos] = '\\';
                            data_[pos + 1] = 'n';
                            // Truncate anything after to maintain consistency
                            text_pos_ = pos + 2;
                            get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);
                            break; // Buffer full, stop processing
                        } else {
                            // Can't fit complete escape, truncate at newline position
                            text_pos_ = pos;
                            get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);
                            break;
                        }
                    }
                } else {
                    pos++;
                }
            }
            return;
        }

        // Original padding behavior for normal mode
        size_t pos = start_pos;

        while (pos < text_pos_)
        {
            // Find next newline
            size_t newline_pos = pos;
            while (newline_pos < text_pos_ && data_[newline_pos] != '\n') { newline_pos++; }

            if (newline_pos < text_pos_ && newline_pos + 1 < text_pos_)
            {
                // Found a newline with text after it - need to insert padding
                size_t remaining = text_pos_ - (newline_pos + 1);

                // Check if we have room for padding
                size_t available = metadata_pos_ - text_pos_;
                if (available >= header_width_)
                {
                    // Bounds check before memmove
                    assert(newline_pos + 1 + header_width_ + remaining <= capacity_);
                    // Shift remaining text to make room for padding
                    std::memmove(data_ + newline_pos + 1 + header_width_, data_ + newline_pos + 1, remaining);

                    // Insert padding spaces
                    std::memset(data_ + newline_pos + 1, ' ', header_width_);

                    // Update our write position
                    text_pos_ += header_width_;
                    get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);
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
        size_t start_pos = text_pos_;

        // Check if we have enough space for at least some of the output
        size_t available = metadata_pos_ - text_pos_;
        if (available == 0) return;

        // Format directly into the buffer at current position
        int written = snprintf(data_ + text_pos_,
                               available, // snprintf needs full available size including null terminator
                               format,
                               std::forward<Args>(args)...);

        if (written > 0)
        {
            if (written < static_cast<int>(available))
            {
                // Advance write position by what was actually written (not including null terminator)
                // When written < available, the entire string fit
                text_pos_ += written;
            }
            else
            {
                // String was truncated, advance by available-1 (snprintf wrote available-1 chars + null)
                text_pos_ += available - 1;
            }

            get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);

            // Now handle newline padding in-place
            handle_newline_padding(start_pos);
        }
    }

    // Format directly into buffer using fmt with newline padding
    template <typename... Args> void format_to_buffer_with_padding(fmt::format_string<Args...> fmt, Args &&...args)
    {
        // Remember where we start writing
        size_t start_pos = text_pos_;

        // Check if we have enough space for at least some of the output
        size_t available = metadata_pos_ - text_pos_;
        if (available == 0) return;

        // Format directly into the buffer at current position
        char *output_start = data_ + text_pos_;
        auto result        = fmt::format_to_n(output_start, available, fmt, std::forward<Args>(args)...);

        // Update write position based on what would have been written (capped by available space)
        size_t written = std::min(result.size, available);
        text_pos_ += written;
        get_header()->text_len = static_cast<uint16_t>(text_pos_ - HEADER_SIZE);

        // Now handle newline padding in-place
        handle_newline_padding(start_pos);
    }

    bool is_flush_marker() const { return level_ == log_level::nolog && len() == 0; }

    // Get text content (skipping buffer header)
    std::string_view get_text() const
    {
        const auto *header = get_header();
        return std::string_view(data_ + HEADER_SIZE, header->text_len);
    }

    // Get just the message text (skipping buffer header and log header)
    std::string_view get_message() const
    {
        size_t message_start = HEADER_SIZE + header_width_;
        const auto *header   = get_header();
        if (message_start >= HEADER_SIZE + header->text_len)
        {
            return std::string_view(); // No message, just header
        }
        return std::string_view(data_ + message_start, HEADER_SIZE + header->text_len - message_start);
    }

    // Finalize buffer (update metadata offset in header)
    void finalize()
    {
        auto *header         = get_header();
        header->metadata_off = static_cast<uint16_t>(metadata_pos_);
    }

    bool is_padding_enabled() const noexcept { return padding_enabled_; }
    void set_padding_enabled(bool enabled) noexcept { padding_enabled_ = enabled; }
};

// Helper base to hold storage before log_buffer_base
template<size_t BufferSize>
struct log_buffer_storage {
    // Align the actual data array to cache line for better performance  
    alignas(CACHE_LINE_SIZE) std::array<char, BufferSize> data_storage_{};
};

// Templated buffer with actual storage
template<size_t BufferSize>
struct alignas(CACHE_LINE_SIZE) log_buffer final 
    : private log_buffer_storage<BufferSize>  // Storage comes first
    , public log_buffer_base                  // Then base class
{
    // Static assertions for compile-time safety
    static_assert(BufferSize > sizeof(buffer_header) + 256, 
                  "Buffer too small for header and reasonable content");
    static_assert(BufferSize <= 65535, 
                  "Buffer size must fit in uint16_t for rec_size field");
    
    log_buffer() noexcept 
        : log_buffer_storage<BufferSize>{}
        , log_buffer_base(this->data_storage_.data(), BufferSize) {
        reset();
    }
};

// Buffer pool singleton class
class buffer_pool
{
  public:
    // Public constant for buffer size so tests can reference it
    static constexpr size_t BUFFER_SIZE = 2048;
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
    using buffer_type = log_buffer<BUFFER_SIZE>;
    
    std::unique_ptr<buffer_type[]> buffer_storage_;
#ifdef LOG_RELIABLE_DELIVERY
    moodycamel::BlockingConcurrentQueue<log_buffer_base *> available_buffers_;
#else
    moodycamel::ConcurrentQueue<log_buffer_base *> available_buffers_;
#endif

    std::atomic<uint64_t> pending_failure_report_{0}; // For dispatcher notification
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
    std::atomic<uint64_t> acquire_failures_{0}; // For statistics
    // Statistics tracking
    std::atomic<uint64_t> total_acquires_{0};
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
        buffer_storage_ = std::make_unique<buffer_type[]>(BUFFER_POOL_SIZE);
        for (size_t i = 0; i < BUFFER_POOL_SIZE; ++i)
        {
            buffer_storage_[i].reset(); // Initialize each buffer
            available_buffers_.enqueue(&buffer_storage_[i]);
        }
    }

  public:
    static buffer_pool &instance()
    {
        static buffer_pool instance;
        return instance;
    }

    log_buffer_base *acquire(bool human_readable)
    {
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
        total_acquires_.fetch_add(1, std::memory_order_relaxed);
#endif

        // Use thread-local consumer token for better performance
        thread_local moodycamel::ConsumerToken consumer_token(available_buffers_);
        
        log_buffer_base *buffer = nullptr;
#ifdef LOG_RELIABLE_DELIVERY
        available_buffers_.wait_dequeue(consumer_token, buffer);
#else
        available_buffers_.try_dequeue(consumer_token, buffer);
#endif
        if (buffer)
        {
            buffer->add_ref();
            buffer->set_padding_enabled(human_readable);

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
            pending_failure_report_.fetch_add(1, std::memory_order_relaxed);
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
            acquire_failures_.fetch_add(1, std::memory_order_relaxed);
#endif
        }
        return buffer;
    }

    void release(log_buffer_base *buffer)
    {
        if (buffer)
        {
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
            total_releases_.fetch_add(1, std::memory_order_relaxed);
            buffers_in_use_.fetch_sub(1, std::memory_order_relaxed);
#endif
            // Use thread-local producer token for better performance
            // But check if we're in shutdown to avoid segfault
            static thread_local moodycamel::ProducerToken* producer_token = nullptr;
            if (!producer_token) {
                producer_token = new moodycamel::ProducerToken(available_buffers_);
            }
            available_buffers_.enqueue(*producer_token, buffer);
        }
    }

    /**
     * @brief Get count of pending failures to report
     * @return Current count of unreported acquire failures
     */
    uint64_t get_pending_failures() const
    {
        return pending_failure_report_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Reset pending failure count after successful reporting
     */
    void reset_pending_failures()
    {
        pending_failure_report_.store(0, std::memory_order_relaxed);
    }

#ifdef LOG_COLLECT_BUFFER_POOL_METRICS

  public:
    /**
     * @brief Track buffer usage statistics
     * @param buffer Buffer being released back to pool
     */
    void track_buffer_usage(const log_buffer_base *buffer)
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
        s.pool_memory_kb    = (s.total_buffers * BUFFER_SIZE) / 1024;
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
        total_releases_.store(0, std::memory_order_relaxed);
        acquire_failures_.store(0, std::memory_order_relaxed);
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

inline void log_buffer_base::release()
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