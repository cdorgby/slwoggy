/**
 * @file log_structured.hpp
 * @brief Log structured logging support
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "robin_hood.h"
#include "log_types.hpp"

namespace slwoggy
{
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
 * - Pre-registered internal keys with guaranteed IDs 0-4
 * - Ultra-fast path for internal keys bypassing all caching layers
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
    // Internal metadata key IDs - always registered with fixed IDs
    static constexpr uint16_t INTERNAL_KEY_TS     = 0; // ts - timestamp
    static constexpr uint16_t INTERNAL_KEY_LEVEL  = 1; // level - log level
    static constexpr uint16_t INTERNAL_KEY_MODULE = 2; // module - module name
    static constexpr uint16_t INTERNAL_KEY_FILE   = 3; // file - source file
    static constexpr uint16_t INTERNAL_KEY_LINE   = 4; // line - source line
    static constexpr uint16_t FIRST_USER_KEY_ID   = 5; // User keys start here

    // Internal metadata key names - used for consistency
    static constexpr const char *INTERNAL_KEY_NAME_TS     = "ts";
    static constexpr const char *INTERNAL_KEY_NAME_LEVEL  = "level";
    static constexpr const char *INTERNAL_KEY_NAME_MODULE = "module";
    static constexpr const char *INTERNAL_KEY_NAME_FILE   = "file";
    static constexpr const char *INTERNAL_KEY_NAME_LINE   = "line";

    /**
     * @brief Get the singleton instance of the key registry
     * @return Reference to the global key registry
     */
    static structured_log_key_registry &instance()
    {
        static structured_log_key_registry registry;
        return registry;
    }

    /**
     * @brief Get or register a key and return its numeric ID
     *
     * This method uses a multi-tier lookup strategy for optimal performance:
     * 1. Ultra-fast path: Direct comparison for internal keys (ts, level, etc.)
     * 2. Fast path: Thread-local cache lookup (no locks)
     * 3. Medium path: Global registry with shared lock
     * 4. Slow path: Register new key with exclusive lock
     *
     * @param key The string key to look up or register
     * @return Numeric ID for the key (0-4 for internal keys, 5+ for user keys)
     */
    uint16_t get_or_register_key(std::string_view key)
    {
        // Ultra-fast path for internal keys - no cache or hash lookup needed
        if (!key.empty())
        {
            // Check internal keys with simple string comparison
            if (key == INTERNAL_KEY_NAME_TS) return INTERNAL_KEY_TS;
            if (key == INTERNAL_KEY_NAME_LEVEL) return INTERNAL_KEY_LEVEL;
            if (key == INTERNAL_KEY_NAME_MODULE) return INTERNAL_KEY_MODULE;
            if (key == INTERNAL_KEY_NAME_FILE) return INTERNAL_KEY_FILE;
            if (key == INTERNAL_KEY_NAME_LINE) return INTERNAL_KEY_LINE;
            // Fall through for user-defined keys
        }

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

        // Slow path: Need to register new key
        // Double-check under exclusive lock before allocating
        {
            std::unique_lock lock(mutex_);

            // Check again in case another thread registered while we waited for exclusive lock
            if (auto it = key_to_id_.find(key); it != key_to_id_.end())
            {
                tl_cache_.key_to_id[it->first] = it->second;
                return it->second;
            }

            // Now we know for sure it doesn't exist, allocate and register
            if (next_key_id_ >= MAX_STRUCTURED_KEYS) { throw std::runtime_error("Too many structured log keys"); }

            keys_.push_back(std::string(key)); // Only allocate when truly needed
            uint16_t id          = next_key_id_++;
            auto key_view        = std::string_view(keys_.back());
            key_to_id_[key_view] = id;
            id_to_key_[id]       = key_view;

            // Add to thread-local cache using the stable string_view
            tl_cache_.key_to_id[key_view] = id;

            return id;
        }
    }
    /**
     * @brief Look up a key string by its numeric ID
     *
     * Uses a switch-based fast path for internal key IDs (0-4) before
     * checking caches or the global registry.
     *
     * @param id The numeric ID to look up
     * @return The key string, or "unknown" if ID is invalid
     */
    std::string_view get_key(uint16_t id) const
    {
        // Ultra-fast path for internal keys - no cache lookup needed
        switch (id)
        {
        case INTERNAL_KEY_TS: return INTERNAL_KEY_NAME_TS;
        case INTERNAL_KEY_LEVEL: return INTERNAL_KEY_NAME_LEVEL;
        case INTERNAL_KEY_MODULE: return INTERNAL_KEY_NAME_MODULE;
        case INTERNAL_KEY_FILE: return INTERNAL_KEY_NAME_FILE;
        case INTERNAL_KEY_LINE: return INTERNAL_KEY_NAME_LINE;
        default:
            // Fall through for user keys
            break;
        }

        if (id >= next_key_id_) return "unknown";

        // Fast path: check thread-local cache
        if (auto it = tl_cache_.id_to_key.find(id); it != tl_cache_.id_to_key.end()) { return it->second; }

        // Slow path: look up in global registry with shared lock
        std::shared_lock lock(mutex_);
        if (auto it = id_to_key_.find(id); it != id_to_key_.end())
        {
            // Found in global registry, add to thread-local cache
            tl_cache_.id_to_key[id] = it->second;
            return it->second;
        }
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
    structured_log_key_registry()
    {
        keys_.reserve(MAX_STRUCTURED_KEYS);

        // Pre-register internal metadata keys with guaranteed IDs
        // These are reserved for system use and always available
        register_internal_key(INTERNAL_KEY_NAME_TS, INTERNAL_KEY_TS);
        register_internal_key(INTERNAL_KEY_NAME_LEVEL, INTERNAL_KEY_LEVEL);
        register_internal_key(INTERNAL_KEY_NAME_MODULE, INTERNAL_KEY_MODULE);
        register_internal_key(INTERNAL_KEY_NAME_FILE, INTERNAL_KEY_FILE);
        register_internal_key(INTERNAL_KEY_NAME_LINE, INTERNAL_KEY_LINE);

        // User keys start after internal ones
        next_key_id_ = FIRST_USER_KEY_ID;
    }

    void register_internal_key(const char *key, uint16_t id)
    {
        keys_.push_back(key);
        auto key_view        = std::string_view(keys_.back());
        key_to_id_[key_view] = id;
        id_to_key_[id]       = key_view;
    }

  private:
    // Thread-local cache for fast key lookups
    struct thread_cache
    {
        // Maps string_view (pointing to keys_ storage) to ID
        robin_hood::unordered_map<std::string_view, uint16_t> key_to_id;
        // Maps ID to string_view (pointing to keys_ storage)
        mutable robin_hood::unordered_map<uint16_t, std::string_view> id_to_key;

        // Constructor reserves capacity to avoid rehashing
        thread_cache()
        {
            // Reserve space for all possible keys to avoid any rehashing
            key_to_id.reserve(MAX_STRUCTURED_KEYS);
            id_to_key.reserve(MAX_STRUCTURED_KEYS);
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
 * at the end of each log buffer, growing backward from the buffer end.
 *
 * Storage Format (growing backward from end):
 * - Each KV pair: [value_data (N)][value_length (2)][key_id (2)]
 * - Count byte (1) at the start of metadata section
 * - Value length is limited to MAX_FORMATTED_SIZE bytes
 *
 * Memory Layout:
 * |<-- Header -->|<-- Text -->|<-- Gap -->|<-- Metadata -->|
 * | 6 bytes      | forward → → |           | ← ← backward   |
 *
 * Platform Assumptions:
 * - Native endianness (no byte swapping performed)
 * - Natural alignment for uint16_t (2-byte alignment)
 * - Binary format is NOT portable across different architectures
 * - For cross-platform logs, use text-based sinks or external serialization
 *
 * @note The metadata grows backward from the buffer end. If adding a KV pair
 *       would collide with the text area, it is silently dropped and drop
 *       statistics are incremented (see get_drop_stats()).
 */
// Forward declaration
class log_buffer_base;

class log_buffer_metadata_adapter
{
  public:
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
    log_buffer_base *buffer_;

  public:
    log_buffer_metadata_adapter(log_buffer_base *buffer) : buffer_(buffer) {}

    void reset();
    bool add_kv(uint16_t key_id, std::string_view value);
    template <typename T> bool add_kv_formatted(uint16_t key_id, T &&value);

    // Fast-path specializations
    bool add_kv_formatted(uint16_t key_id, std::string_view value);
    bool add_kv_formatted(uint16_t key_id, const char *value);
    bool add_kv_formatted(uint16_t key_id, const std::string &value);
    bool add_kv_formatted(uint16_t key_id, bool value);

    // Integer specializations
    bool add_kv_formatted(uint16_t key_id, int value);
    bool add_kv_formatted(uint16_t key_id, unsigned int value);
    bool add_kv_formatted(uint16_t key_id, long value);
    bool add_kv_formatted(uint16_t key_id, unsigned long value);
    bool add_kv_formatted(uint16_t key_id, long long value);
    bool add_kv_formatted(uint16_t key_id, unsigned long long value);
    bool add_kv_formatted(uint16_t key_id, signed char value);
    bool add_kv_formatted(uint16_t key_id, unsigned char value);
    bool add_kv_formatted(uint16_t key_id, short value);
    bool add_kv_formatted(uint16_t key_id, unsigned short value);

    // Iterator for reading metadata
    class iterator
    {
        const char *current_;
        const char *end_;
        uint8_t remaining_count_;

      public:
        iterator(const char *start, const char *end, uint8_t count);
        bool has_next() const;
        kv_pair next();
    };

    iterator get_iterator() const;

    /**
     * @brief Calculate total size needed for k/v data
     * @return Pair of (total_chars_needed, kv_count)
     */
    std::pair<size_t, size_t> calculate_kv_size() const;

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

  private:
    template <typename IntType>
    typename std::enable_if<std::is_integral_v<IntType> && !std::is_same_v<IntType, bool>, bool>::type
    add_kv_formatted_int(uint16_t key_id, IntType value);
};

} // namespace slwoggy