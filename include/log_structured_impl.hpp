/**
 * @file log_structured_impl.hpp
 * @brief Implementation of structured logging support
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include "log_structured.hpp"
#include "log_buffer.hpp"
#include <charconv>

namespace slwoggy
{

// Implementation of get_metadata_adapter methods
inline log_buffer_metadata_adapter log_buffer_base::get_metadata_adapter() { return log_buffer_metadata_adapter(this); }

inline log_buffer_metadata_adapter log_buffer_base::get_metadata_adapter() const
{
    return log_buffer_metadata_adapter(const_cast<log_buffer_base *>(this));
}

// Implementation of log_buffer_metadata_adapter methods

inline void log_buffer_metadata_adapter::reset()
{
    // Buffer is already reset by log_buffer::reset()
    // Nothing to do here since metadata_pos_ is managed by log_buffer
}

inline bool log_buffer_metadata_adapter::add_kv(uint16_t key_id, std::string_view value)
{
    // Limit value size to MAX_FORMATTED_SIZE
    size_t value_len = std::min(value.size(), MAX_FORMATTED_SIZE);
    
    // Calculate space needed for new KV pair: 2 bytes key_id + 2 bytes len + value
    size_t kv_size = sizeof(uint16_t) + sizeof(uint16_t) + value_len;
    
    // Check if this is the first KV pair
    bool first_kv = (buffer_->metadata_pos_ >= buffer_->capacity_);
    
    // Total space needed: KV size + 1 byte for count (if first KV)
    size_t space_needed = first_kv ? (kv_size + 1) : kv_size;

    // Check if we have room (safe arithmetic to avoid underflow)
    if (space_needed > buffer_->metadata_pos_ || 
        buffer_->metadata_pos_ - space_needed < buffer_->text_pos_)
    {
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
        dropped_bytes_.fetch_add(kv_size, std::memory_order_relaxed);
#endif
        return false;
    }

    // Get current count (0 if no metadata yet)
    uint8_t count = first_kv ? 0 : buffer_->data_[buffer_->metadata_pos_];
    
    // Check if we've reached the maximum number of KV pairs
    if (count >= MAX_STRUCTURED_KEYS) {
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
        dropped_bytes_.fetch_add(kv_size, std::memory_order_relaxed);
#endif
        return false;
    }

    // Calculate new metadata start position
    size_t new_metadata_pos = buffer_->metadata_pos_ - space_needed;
    
    // Write incremented count at new position
    buffer_->data_[new_metadata_pos] = count + 1;
    
    // Write new KV pair right after the new count byte
    size_t write_pos = new_metadata_pos + 1;

    // Write key_id (2 bytes) - use memcpy to avoid alignment issues
    uint16_t key_id_val = key_id;
    std::memcpy(&buffer_->data_[write_pos], &key_id_val, sizeof(uint16_t));
    write_pos += sizeof(uint16_t);

    // Write value length (2 bytes) - use memcpy to avoid alignment issues
    uint16_t value_len_val = static_cast<uint16_t>(value_len);
    std::memcpy(&buffer_->data_[write_pos], &value_len_val, sizeof(uint16_t));
    write_pos += sizeof(uint16_t);

    // Write value
    if (value_len > 0) { 
        std::memcpy(&buffer_->data_[write_pos], value.data(), value_len);
    }

    // Update metadata position to new start
    buffer_->metadata_pos_ = new_metadata_pos;

    return true;
}

template <typename T> inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, T &&value)
{
    // Format to temporary buffer first
    char temp[MAX_FORMATTED_SIZE];
    auto result           = fmt::format_to_n(temp, sizeof(temp), "{}", std::forward<T>(value));
    size_t formatted_size = std::min(result.size, sizeof(temp));

    return add_kv(key_id, std::string_view(temp, formatted_size));
}

// Specializations
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, std::string_view value)
{
    return add_kv(key_id, value);
}

inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, const char *value)
{
    if (!value) return add_kv(key_id, std::string_view("null"));
    return add_kv(key_id, std::string_view(value));
}

inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, const std::string &value)
{
    return add_kv(key_id, std::string_view(value));
}

inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, bool value)
{
    return add_kv(key_id, value ? std::string_view("true") : std::string_view("false"));
}

// Integer specialization helper
template <typename IntType>
inline typename std::enable_if<std::is_integral_v<IntType> && !std::is_same_v<IntType, bool>, bool>::type
log_buffer_metadata_adapter::add_kv_formatted_int(uint16_t key_id, IntType value)
{
    char temp[32];
    auto [ptr, ec] = std::to_chars(temp, temp + sizeof(temp), value);
    if (ec != std::errc()) { return add_kv_formatted(key_id, static_cast<int64_t>(value)); }
    return add_kv(key_id, std::string_view(temp, ptr - temp));
}

// Integer specializations
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, int value)
{
    return add_kv_formatted_int(key_id, value);
}
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, unsigned int value)
{
    return add_kv_formatted_int(key_id, value);
}
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, long value)
{
    return add_kv_formatted_int(key_id, value);
}
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, unsigned long value)
{
    return add_kv_formatted_int(key_id, value);
}
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, long long value)
{
    return add_kv_formatted_int(key_id, value);
}
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, unsigned long long value)
{
    return add_kv_formatted_int(key_id, value);
}
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, signed char value)
{
    return add_kv_formatted_int(key_id, value);
}
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, unsigned char value)
{
    return add_kv_formatted_int(key_id, value);
}
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, short value)
{
    return add_kv_formatted_int(key_id, value);
}
inline bool log_buffer_metadata_adapter::add_kv_formatted(uint16_t key_id, unsigned short value)
{
    return add_kv_formatted_int(key_id, value);
}

// Iterator implementation
inline log_buffer_metadata_adapter::iterator::iterator(const char *start, const char *end, uint8_t count) 
    : current_(start), end_(end), remaining_count_(count)
{
}

inline bool log_buffer_metadata_adapter::iterator::has_next() const
{
    // Check both count and available space
    // Need at least 2 (key_id) + 2 (len) = 4 bytes for a KV pair header
    return remaining_count_ > 0 && current_ + 4 <= end_;
}

inline log_buffer_metadata_adapter::kv_pair log_buffer_metadata_adapter::iterator::next()
{
    kv_pair result;

    // Read key_id (2 bytes) - use memcpy to avoid alignment issues
    uint16_t key_id;
    std::memcpy(&key_id, current_, sizeof(uint16_t));
    result.key_id = key_id;
    current_ += sizeof(uint16_t);

    // Read value length (2 bytes) - use memcpy to avoid alignment issues
    uint16_t value_len;
    std::memcpy(&value_len, current_, sizeof(uint16_t));
    current_ += sizeof(uint16_t);

    // Read value
    result.value = std::string_view(current_, value_len);
    current_ += value_len;
    
    // Decrement remaining count
    remaining_count_--;

    return result;
}

inline log_buffer_metadata_adapter::iterator log_buffer_metadata_adapter::get_iterator() const
{
    if (buffer_->metadata_pos_ >= buffer_->capacity_)
    {
        // No metadata
        return iterator(nullptr, nullptr, 0);
    }

    // Read count byte
    uint8_t count = buffer_->data_[buffer_->metadata_pos_];
    
    // Skip count byte for data start
    const char *start = &buffer_->data_[buffer_->metadata_pos_ + 1];
    const char *end   = &buffer_->data_[buffer_->capacity_];
    return iterator(start, end, count);
}

inline std::pair<size_t, size_t> log_buffer_metadata_adapter::calculate_kv_size() const
{
    size_t total_size = 0;
    size_t count      = 0;

    auto iter = get_iterator();
    while (iter.has_next())
    {
        auto kv       = iter.next();
        auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);
        total_size += key_name.size() + kv.value.size();
        count++;
    }

    return {total_size, count};
}

#ifdef LOG_COLLECT_STRUCTURED_METRICS
// Define static members for metadata drop tracking (inline to avoid ODR violations)
inline std::atomic<uint64_t> log_buffer_metadata_adapter::dropped_count_{0};
inline std::atomic<uint64_t> log_buffer_metadata_adapter::dropped_bytes_{0};
#endif

} // namespace slwoggy