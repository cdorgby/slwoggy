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
inline log_buffer_metadata_adapter log_buffer::get_metadata_adapter() { return log_buffer_metadata_adapter(this); }

inline log_buffer_metadata_adapter log_buffer::get_metadata_adapter() const
{
    return log_buffer_metadata_adapter(const_cast<log_buffer *>(this));
}

// Implementation of log_buffer_metadata_adapter methods

inline void log_buffer_metadata_adapter::reset()
{
    // Buffer is already reset by log_buffer::reset()
    // Nothing to do here since metadata_pos_ is managed by log_buffer
}

inline bool log_buffer_metadata_adapter::add_kv(uint16_t key_id, std::string_view value)
{
    // Calculate space needed: 1 byte count + 2 bytes key_id + 1 byte len + value
    size_t kv_size      = sizeof(uint16_t) + 1 + value.size();
    size_t total_needed = kv_size + 1; // +1 for count byte

    // Check if we have room
    if (buffer_->metadata_pos_ < buffer_->text_pos_ + total_needed)
    {
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
        dropped_bytes_.fetch_add(kv_size, std::memory_order_relaxed);
#endif
        return false;
    }

    // Get current count (or 0 if no metadata yet)
    uint8_t count = (buffer_->metadata_pos_ < LOG_BUFFER_SIZE) ? buffer_->data_[buffer_->metadata_pos_] : 0;

    // Move position back to make room for new KV pair
    size_t new_pos = buffer_->metadata_pos_ - kv_size - 1;

    // If we have existing metadata, move it back
    if (count > 0)
    {
        // Existing data starts at metadata_pos_ + 1 (after count byte)
        // and goes to LOG_BUFFER_SIZE
        size_t existing_data_size = LOG_BUFFER_SIZE - (buffer_->metadata_pos_ + 1);
        if (existing_data_size > 0)
        {
            std::memmove(&buffer_->data_[new_pos + 1 + kv_size], &buffer_->data_[buffer_->metadata_pos_ + 1], existing_data_size);
        }
    }

    // Write new count at new position
    buffer_->data_[new_pos] = count + 1;

    // Write new KV pair after count (forward order)
    size_t write_pos = new_pos + 1;

    // Write key_id (2 bytes)
    *reinterpret_cast<uint16_t *>(&buffer_->data_[write_pos]) = key_id;
    write_pos += sizeof(uint16_t);

    // Write value length (1 byte)
    buffer_->data_[write_pos] = static_cast<uint8_t>(value.size());
    write_pos += 1;

    // Write value
    if (value.size() > 0) { std::memcpy(&buffer_->data_[write_pos], value.data(), value.size()); }

    // Update metadata position
    buffer_->metadata_pos_ = new_pos;

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
inline log_buffer_metadata_adapter::iterator::iterator(const char *start, const char *end) : current_(start), end_(end)
{
}

inline bool log_buffer_metadata_adapter::iterator::has_next() const
{
    // Need at least 2 (key_id) + 1 (len) = 3 bytes for a KV pair
    return current_ + 3 <= end_;
}

inline log_buffer_metadata_adapter::kv_pair log_buffer_metadata_adapter::iterator::next()
{
    kv_pair result;

    // Read key_id (2 bytes)
    result.key_id = *reinterpret_cast<const uint16_t *>(current_);
    current_ += sizeof(uint16_t);

    // Read value length (1 byte)
    uint8_t value_len = *current_;
    current_ += 1;

    // Read value
    result.value = std::string_view(current_, value_len);
    current_ += value_len;

    return result;
}

inline log_buffer_metadata_adapter::iterator log_buffer_metadata_adapter::get_iterator() const
{
    if (buffer_->metadata_pos_ >= LOG_BUFFER_SIZE)
    {
        // No metadata
        return iterator(nullptr, nullptr);
    }

    // Skip count byte
    const char *start = &buffer_->data_[buffer_->metadata_pos_ + 1];
    const char *end   = &buffer_->data_[LOG_BUFFER_SIZE];
    return iterator(start, end);
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