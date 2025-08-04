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
#include <shared_mutex>
#include <vector>
#include <stdexcept>
#include <fmt/format.h>
#include <memory>
#include <charconv>

#include "moodycamel/concurrentqueue.h"
#include "robin_hood.h"

#include "log_types.hpp"

namespace slwoggy
{

// Cache line size detection
#if defined(__cpp_lib_hardware_interference_size)
inline constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
inline constexpr size_t CACHE_LINE_SIZE = 64; // Common cache line size
#endif

/**
 * @brief Global registry for structured logging keys
 *
 * This singleton registry maintains a mapping between string keys and numeric IDs
 * to optimize structured logging performance. Instead of storing full string keys
 * with each log entry, we store compact 16-bit IDs.
 *
 * Features:
 * - Thread-safe key registration with shared_mutex
 * - Supports up to MAX_STRUCTURED_KEYS unique keys
 * - Keys are never removed (stable IDs within process lifetime)
 * - O(1) ID to string lookup, O(1) amortized string to ID lookup
 *
 * @warning Key IDs are only stable within a single process run. The same key
 *          may receive different IDs across application restarts. If you need
 *          stable IDs across runs, implement external key mapping configuration.
 *
 * @note Keys are case-sensitive and stored permanently. Plan key names carefully
 *       to avoid exhausting the key limit in long-running applications.
 *
 * Performance tip: Pre-register frequently used keys at startup to avoid
 * mutex contention during high-throughput logging:
 * @code
 * // At application startup:
 * auto& registry = structured_log_key_registry::instance();
 * registry.get_or_register_key("user_id");
 * registry.get_or_register_key("request_id");
 * registry.get_or_register_key("latency_ms");
 * @endcode
 */
struct structured_log_key_registry
{
    /**
     * @brief Get the singleton instance of the key registry
     * @return Reference to the global key registry
     */
    static structured_log_key_registry &instance()
    {
        static structured_log_key_registry registry;
        return registry;
    }

    // Overload for string_view to avoid unnecessary string allocation
    uint16_t get_or_register_key(std::string_view key)
    {
        // Fast path: check thread-local cache (no lock needed)
        if (auto it = tl_cache_.key_to_id.find(key); it != tl_cache_.key_to_id.end()) { return it->second; }

        // Medium path: check global registry with shared lock
        {
            std::shared_lock lock(mutex_);
            if (auto it = key_to_id_.find(key); it != key_to_id_.end())
            {
                tl_cache_.key_to_id[it->first] = it->second;
                return it->second;
            }
        }

        // Need to register - now we must allocate a string
        return get_or_register_key(std::string(key));
    }

    /**
     * @brief Look up a key string by its numeric ID
     * @param id The numeric ID to look up
     * @return The key string, or "unknown" if ID is invalid
     */
    std::string_view get_key(uint16_t id) const
    {
        std::shared_lock lock(mutex_);
        if (auto it = id_to_key_.find(id); it != id_to_key_.end()) return it->second;
        return "unknown";
    }

    /**
     * @brief Registry statistics for monitoring
     */
    struct stats
    {
        uint16_t key_count;         ///< Number of registered keys
        uint16_t max_keys;          ///< Maximum keys allowed (MAX_STRUCTURED_KEYS)
        size_t estimated_memory_kb; ///< Estimated memory usage in KB
        float usage_percent;        ///< Percentage of max keys used
    };

    /**
     * @brief Get current registry statistics
     * @return Statistics about key usage and memory consumption
     */
    stats get_stats() const
    {
        std::shared_lock lock(mutex_);
        stats s;
        s.key_count     = static_cast<uint16_t>(keys_.size());
        s.max_keys      = MAX_STRUCTURED_KEYS;
        s.usage_percent = (s.key_count * 100.0f) / s.max_keys;

        // Estimate memory: vector storage + string data + maps overhead
        size_t string_mem = 0;
        for (const auto &key : keys_) { string_mem += key.capacity() + sizeof(std::string); }
        size_t map_mem = (key_to_id_.size() + id_to_key_.size()) *
                         (sizeof(void *) * 4 + sizeof(uint16_t) + sizeof(std::string_view));
        s.estimated_memory_kb = (string_mem + map_mem + sizeof(*this)) / 1024;

        return s;
    }

    /**
     * @brief Pre-register multiple keys at once
     *
     * Batch registration is more efficient than individual calls as it
     * acquires the mutex only once. Use at application startup for
     * frequently used keys.
     *
     * @param keys Vector of key names to register
     * @return Vector of key IDs in the same order as input
     *
     * Example:
     * @code
     * auto& registry = structured_log_key_registry::instance();
     * auto ids = registry.batch_register({
     *     "user_id", "request_id", "latency_ms",
     *     "status_code", "method", "path"
     * });
     * @endcode
     */
    std::vector<uint16_t> batch_register(const std::vector<std::string> &keys)
    {
        std::unique_lock lock(mutex_);
        std::vector<uint16_t> ids;
        ids.reserve(keys.size());

        for (const auto &key : keys)
        {
            // Check existing first
            if (auto it = key_to_id_.find(key); it != key_to_id_.end())
            {
                ids.push_back(it->second);
                continue;
            }

            // Register new key
            if (next_key_id_ >= MAX_STRUCTURED_KEYS) { throw std::runtime_error("Too many structured log keys"); }

            uint16_t id = next_key_id_++;
            keys_.push_back(key);
            auto key_view        = std::string_view(keys_.back());
            key_to_id_[key_view] = id;
            id_to_key_[id]       = key_view;
            ids.push_back(id);
        }

        return ids;
    }

  private:
    structured_log_key_registry() { keys_.reserve(MAX_STRUCTURED_KEYS); }

    /**
     * @brief Get or register a key, returning its numeric ID
     * @param key The string key to register
     * @return Numeric ID for the key (stable across calls)
     * @throws std::runtime_error if MAX_STRUCTURED_KEYS limit is exceeded
     *
     * @note This method uses a thread-local cache to avoid lock contention.
     *       The fast path (cache hit) requires no locking at all.
     *
     * @warning Memory usage: Each thread that logs structured data will allocate
     *          approximately (MAX_STRUCTURED_KEYS * average_key_length) bytes
     *          for its thread-local cache. With the default MAX_STRUCTURED_KEYS=256,
     *          this is typically 2-4KB per thread.
     */
    uint16_t get_or_register_key(const std::string &key)
    {
        // Fast path: check thread-local cache (no lock needed)
        if (auto it = tl_cache_.key_to_id.find(key); it != tl_cache_.key_to_id.end()) { return it->second; }

        // Medium path: check global registry with shared lock
        {
            std::shared_lock lock(mutex_);
            if (auto it = key_to_id_.find(key); it != key_to_id_.end())
            {
                // Found in global registry, add to thread-local cache
                // Note: it->first is a string_view pointing to stable storage in keys_
                tl_cache_.key_to_id[it->first] = it->second;
                return it->second;
            }
        }

        uint16_t id;
        std::string_view key_view;

        {
            // Slow path: register new key with exclusive lock
            std::unique_lock lock(mutex_);

            // Double-check in case another thread registered while we waited for lock
            if (auto it = key_to_id_.find(key); it != key_to_id_.end())
            {
                tl_cache_.key_to_id[it->first] = it->second;
                return it->second;
            }

            if (next_key_id_ >= MAX_STRUCTURED_KEYS) throw std::runtime_error("Too many structured log keys");

            keys_.push_back(key); // Push first (might throw, so no increments before success)
            id                   = next_key_id_++;
            key_view             = keys_.back();
            key_to_id_[key_view] = id;
            id_to_key_[id]       = key_view;
        }

        // Add to thread-local cache using the stable string_view
        tl_cache_.key_to_id[key_view] = id;

        return id;
    }

  private:
    // Thread-local cache for fast key lookups
    struct thread_cache
    {
        // Maps string_view (pointing to keys_ storage) to ID
        robin_hood::unordered_map<std::string_view, uint16_t> key_to_id;

        // Constructor reserves capacity to avoid rehashing
        thread_cache()
        {
            // Reserve space for all possible keys to avoid any rehashing
            key_to_id.reserve(MAX_STRUCTURED_KEYS);
        }
    };
    inline static thread_local thread_cache tl_cache_;

    // Global registry data (protected by mutex_)
    mutable std::shared_mutex mutex_;
    robin_hood::unordered_map<std::string_view, uint16_t> key_to_id_;
    std::vector<std::string> keys_; // Stable storage
    robin_hood::unordered_map<uint16_t, std::string_view> id_to_key_;
    uint16_t next_key_id_{0};
};

/**
 * @brief Adapter for reading and writing structured metadata in log buffers
 *
 * This class provides an interface for storing key-value pairs in the metadata
 * section of log buffers. The metadata is stored in a compact binary format
 * at the beginning of each log buffer, before the actual log message text.
 *
 * Storage Format:
 * - Header (4 bytes): kv_count (2) + metadata_size (2)
 * - Each KV pair: key_id (2) + value_length (2) + value_data (N)
 *
 * Memory Layout:
 * |<--- METADATA_RESERVE --->|<--- Text Area --->|
 * | Header | KV1 | KV2 | ... | Free | Log message text |
 *
 * Platform Assumptions:
 * - Native endianness (no byte swapping performed)
 * - Natural alignment for uint16_t (2-byte alignment)
 * - Binary format is NOT portable across different architectures
 * - For cross-platform logs, use text-based sinks or external serialization
 *
 * @note The metadata section has a fixed size of METADATA_RESERVE bytes.
 *       If adding a KV pair would exceed this limit, it is silently dropped
 *       and drop statistics are incremented (see get_drop_stats()).
 */
class log_buffer_metadata_adapter
{
  public:
    // Structured data header
    struct metadata_header
    {
        uint16_t kv_count{0};      // Number of key-value pairs
        uint16_t metadata_size{0}; // Total size of metadata section
    };

    static constexpr size_t HEADER_SIZE = sizeof(metadata_header);
    static constexpr size_t TEXT_START  = METADATA_RESERVE + HEADER_SIZE;

#ifdef LOG_COLLECT_STRUCTURED_METRICS
    // Statistics for monitoring dropped metadata
    static std::atomic<uint64_t> dropped_count_;
    static std::atomic<uint64_t> dropped_bytes_;
#endif

    // Metadata key-value pair
    struct kv_pair
    {
        uint16_t key_id;
        std::string_view value;
    };

  private:
    char *data_;
    size_t metadata_pos_;

  public:
    log_buffer_metadata_adapter(char *buffer_data) : data_(buffer_data)
    {
        // Read current metadata position from header
        auto *header  = get_header();
        metadata_pos_ = HEADER_SIZE + header->metadata_size;
    }

    void reset()
    {
        metadata_pos_         = HEADER_SIZE;
        auto *header          = get_header();
        header->kv_count      = 0;
        header->metadata_size = 0;
    }

    /**
     * @brief Add a key-value pair to the metadata section
     * @param key_id Numeric ID of the key (from structured_log_key_registry)
     * @param value String value to store
     * @return true if successfully added, false if insufficient space
     */
    bool add_kv(uint16_t key_id, std::string_view value)
    {
        size_t needed = sizeof(uint16_t) + sizeof(uint16_t) + value.size();
        if (metadata_pos_ + needed > METADATA_RESERVE)
        {
#ifdef LOG_COLLECT_STRUCTURED_METRICS
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            dropped_bytes_.fetch_add(needed, std::memory_order_relaxed);
#endif
#ifdef DEBUG
            static std::atomic<bool> warned{false};
            if (!warned.exchange(true))
            {
                fprintf(stderr, "[LOG] Warning: Structured metadata dropped due to buffer overflow\n");
            }
#endif
            return false;
        }

        // Write: [key_id:2][length:2][value:length]
        *reinterpret_cast<uint16_t *>(data_ + metadata_pos_) = key_id;
        metadata_pos_ += sizeof(uint16_t);

        *reinterpret_cast<uint16_t *>(data_ + metadata_pos_) = static_cast<uint16_t>(value.size());
        metadata_pos_ += sizeof(uint16_t);

        std::memcpy(data_ + metadata_pos_, value.data(), value.size());
        metadata_pos_ += value.size();

        // Update header
        auto *header = get_header();
        header->kv_count++;
        header->metadata_size = static_cast<uint16_t>(metadata_pos_ - HEADER_SIZE);

        return true;
    }

    /**
     * @brief Add a formatted key-value pair directly to metadata buffer
     * @param key_id Numeric ID of the key (from structured_log_key_registry)
     * @param value Value to format directly into buffer
     * @return true if successfully added, false if insufficient space
     *
     * This method formats the value directly into the metadata buffer,
     * avoiding temporary string allocations for better performance.
     */
    template <typename T> bool add_kv_formatted(uint16_t key_id, T &&value)
    {
        // Conservative estimate: assume formatted value won't exceed 128 bytes
        // This covers most integers, floats, and reasonable strings
        size_t header_size = sizeof(uint16_t) + sizeof(uint16_t);

        // Check if we have enough space for headers + max formatted size
        if (metadata_pos_ + header_size + MAX_FORMATTED_SIZE > METADATA_RESERVE)
        {
#ifdef LOG_COLLECT_STRUCTURED_METRICS
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            dropped_bytes_.fetch_add(header_size + 32, std::memory_order_relaxed); // Estimate
#endif
#ifdef DEBUG
            static std::atomic<bool> warned{false};
            if (!warned.exchange(true))
            {
                fprintf(stderr, "[LOG] Warning: Structured metadata dropped due to buffer overflow\n");
            }
#endif
            return false;
        }

        // Write key_id
        *reinterpret_cast<uint16_t *>(data_ + metadata_pos_) = key_id;
        size_t key_pos                                       = metadata_pos_;
        metadata_pos_ += sizeof(uint16_t);

        // Reserve space for length (will update after formatting)
        size_t length_pos = metadata_pos_;
        metadata_pos_ += sizeof(uint16_t);

        // Format directly into buffer
        char *format_start     = data_ + metadata_pos_;
        size_t available_space = METADATA_RESERVE - metadata_pos_;

        auto result = fmt::format_to_n(format_start, available_space, "{}", std::forward<T>(value));

        // Check if formatting was complete (not truncated)
        if (result.size <= available_space)
        {
            // Success - use the actual formatted size
            size_t formatted_size                             = result.size;
            *reinterpret_cast<uint16_t *>(data_ + length_pos) = static_cast<uint16_t>(formatted_size);
            metadata_pos_ += formatted_size;

            // Update header
            auto *header = get_header();
            header->kv_count++;
            header->metadata_size = static_cast<uint16_t>(metadata_pos_ - HEADER_SIZE);

            return true;
        }
        else
        {
            // Formatting would be truncated - rollback
            metadata_pos_ = key_pos;

#ifdef LOG_COLLECT_STRUCTURED_METRICS
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            dropped_bytes_.fetch_add(header_size + result.size, std::memory_order_relaxed);
#endif
            return false;
        }
    }

    // Fast-path specialization for string_view
    bool add_kv_formatted(uint16_t key_id, std::string_view value)
    {
        size_t header_size = sizeof(uint16_t) + sizeof(uint16_t);
        size_t value_size  = value.size();

        // Check exact space needed
        if (metadata_pos_ + header_size + value_size > METADATA_RESERVE)
        {
#ifdef LOG_COLLECT_STRUCTURED_METRICS
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            dropped_bytes_.fetch_add(header_size + value_size, std::memory_order_relaxed);
#endif
            return false;
        }

        // Write key_id
        *reinterpret_cast<uint16_t *>(data_ + metadata_pos_) = key_id;
        metadata_pos_ += sizeof(uint16_t);

        // Write length
        *reinterpret_cast<uint16_t *>(data_ + metadata_pos_) = static_cast<uint16_t>(value_size);
        metadata_pos_ += sizeof(uint16_t);

        // Direct memcpy of string data
        if (value_size > 0)
        {
            std::memcpy(data_ + metadata_pos_, value.data(), value_size);
            metadata_pos_ += value_size;
        }

        // Update header
        auto *header = get_header();
        header->kv_count++;
        header->metadata_size = static_cast<uint16_t>(metadata_pos_ - HEADER_SIZE);

        return true;
    }

    // Fast-path specialization for const char*
    bool add_kv_formatted(uint16_t key_id, const char *value)
    {
        if (!value) return add_kv_formatted(key_id, std::string_view("null"));
        return add_kv_formatted(key_id, std::string_view(value));
    }

    // Fast-path specialization for std::string
    bool add_kv_formatted(uint16_t key_id, const std::string &value)
    {
        return add_kv_formatted(key_id, std::string_view(value));
    }

    // Fast-path specialization for integers using std::to_chars
    template <typename IntType>
    typename std::enable_if<std::is_integral_v<IntType> && !std::is_same_v<IntType, bool>, bool>::type
    add_kv_formatted_int(uint16_t key_id, IntType value)
    {
        // Stack buffer for integer conversion (max 20 chars for int64_t)
        char temp_buffer[32];
        auto [ptr, ec] = std::to_chars(temp_buffer, temp_buffer + sizeof(temp_buffer), value);

        if (ec != std::errc())
        {
            // Fallback to fmt if to_chars fails
            return add_kv_formatted(key_id, static_cast<int64_t>(value));
        }

        size_t value_size  = ptr - temp_buffer;
        size_t header_size = sizeof(uint16_t) + sizeof(uint16_t);

        // Check exact space needed
        if (metadata_pos_ + header_size + value_size > METADATA_RESERVE)
        {
#ifdef LOG_COLLECT_STRUCTURED_METRICS
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            dropped_bytes_.fetch_add(header_size + value_size, std::memory_order_relaxed);
#endif
            return false;
        }

        // Write key_id
        *reinterpret_cast<uint16_t *>(data_ + metadata_pos_) = key_id;
        metadata_pos_ += sizeof(uint16_t);

        // Write length
        *reinterpret_cast<uint16_t *>(data_ + metadata_pos_) = static_cast<uint16_t>(value_size);
        metadata_pos_ += sizeof(uint16_t);

        // Copy converted string
        std::memcpy(data_ + metadata_pos_, temp_buffer, value_size);
        metadata_pos_ += value_size;

        // Update header
        auto *header = get_header();
        header->kv_count++;
        header->metadata_size = static_cast<uint16_t>(metadata_pos_ - HEADER_SIZE);

        return true;
    }

    // Integer specializations - use fundamental types to avoid platform-specific duplicates
    bool add_kv_formatted(uint16_t key_id, int value) { return add_kv_formatted_int(key_id, value); }
    bool add_kv_formatted(uint16_t key_id, unsigned int value) { return add_kv_formatted_int(key_id, value); }
    bool add_kv_formatted(uint16_t key_id, long value) { return add_kv_formatted_int(key_id, value); }
    bool add_kv_formatted(uint16_t key_id, unsigned long value) { return add_kv_formatted_int(key_id, value); }
    bool add_kv_formatted(uint16_t key_id, long long value) { return add_kv_formatted_int(key_id, value); }
    bool add_kv_formatted(uint16_t key_id, unsigned long long value) { return add_kv_formatted_int(key_id, value); }
    bool add_kv_formatted(uint16_t key_id, signed char value) { return add_kv_formatted_int(key_id, value); }
    bool add_kv_formatted(uint16_t key_id, unsigned char value) { return add_kv_formatted_int(key_id, value); }
    bool add_kv_formatted(uint16_t key_id, short value) { return add_kv_formatted_int(key_id, value); }
    bool add_kv_formatted(uint16_t key_id, unsigned short value) { return add_kv_formatted_int(key_id, value); }

    // Fast-path specialization for bool
    bool add_kv_formatted(uint16_t key_id, bool value)
    {
        const std::string_view str_value = value ? "true" : "false";
        return add_kv_formatted(key_id, str_value);
    }

    /**
     * @brief Iterator for reading key-value pairs from metadata
     *
     * Provides forward-only iteration through all stored key-value pairs
     * in the metadata section. Used primarily by log sinks to extract
     * structured data for formatting or forwarding to external systems.
     */
    class iterator
    {
        const char *current_;
        const char *end_;

      public:
        iterator(const char *start, const char *end) : current_(start), end_(end) {}

        bool has_next() const { return current_ + 2 * sizeof(uint16_t) <= end_; }

        kv_pair next()
        {
            kv_pair result;
            result.key_id = *reinterpret_cast<const uint16_t *>(current_);
            current_ += sizeof(uint16_t);

            uint16_t value_len = *reinterpret_cast<const uint16_t *>(current_);
            current_ += sizeof(uint16_t);

            result.value = std::string_view(current_, value_len);
            current_ += value_len;

            return result;
        }
    };

    iterator get_iterator() const
    {
        const auto *header = get_header();
        const char *start  = data_ + HEADER_SIZE;
        const char *end    = start + header->metadata_size;
        return iterator(start, end);
    }

    const metadata_header *get_header() const { return reinterpret_cast<const metadata_header *>(data_); }

#ifdef LOG_COLLECT_STRUCTURED_METRICS
    /**
     * @brief Get statistics about dropped metadata
     * @return Pair of (drop_count, dropped_bytes)
     */
    static std::pair<uint64_t, uint64_t> get_drop_stats()
    {
        return {dropped_count_.load(std::memory_order_relaxed), dropped_bytes_.load(std::memory_order_relaxed)};
    }

    /**
     * @brief Reset drop statistics (useful for testing)
     */
    static void reset_drop_stats()
    {
        dropped_count_.store(0, std::memory_order_relaxed);
        dropped_bytes_.store(0, std::memory_order_relaxed);
    }
#endif

    /**
     * @brief Calculate total size needed for k/v data
     * @return Pair of (total_chars_needed, kv_count)
     *
     * This returns the raw character count needed for all keys and values,
     * without any separators, quotes, or formatting. Callers can use this
     * to calculate their specific formatting needs.
     */
    std::pair<size_t, size_t> calculate_kv_size() const
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

  private:
    metadata_header *get_header() { return reinterpret_cast<metadata_header *>(data_); }
};

#ifdef LOG_COLLECT_STRUCTURED_METRICS
// Define static members for metadata drop tracking (inline to avoid ODR violations)
inline std::atomic<uint64_t> log_buffer_metadata_adapter::dropped_count_{0};
inline std::atomic<uint64_t> log_buffer_metadata_adapter::dropped_bytes_{0};
#endif

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