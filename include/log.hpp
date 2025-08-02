#pragma once

/**
 * @file log.hpp
 * @brief Thread-safe asynchronous logging system
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 *
 * This logging system provides:
 * - Zero-allocation for compile-time disabled log levels
 * - Lock-free concurrent message queue for minimal contention
 * - Asynchronous log processing with dedicated worker thread
 * - Automatic multi-line indentation for readable output
 * - Smart pointer formatting support
 * - Extensible sink architecture
 * - Compile-time log site registration and tracking
 * - Module-based log level control
 * - Dynamic filename width adjustment for aligned output
 *
 * Basic Usage:
 * @code
 *
 * // Preferred: Modern C++20 format style
 * LOG(warn).format("Temperature {}°C exceeds threshold {}°C", temp, max_temp);
 * LOG(info).format("User {} logged in from {}", username, ip_address);
 *
 * // Stream style with operator<<
 * LOG(info) << "Application started";
 * LOG(debug) << "Processing " << count << " items from " << source;
 * LOG(error) << "Failed to connect: " << error_msg;
 *
 * // Immediate flush with endl
 * LOG(fatal) << "Critical error: " << error << endl;
 *
 * // Smart pointers are automatically formatted
 * auto ptr = std::make_shared<MyClass>();
 * LOG(debug) << "Object at " << ptr;  // Shows address or "nullptr"
 * @endcode
 *
 * Structured Logging:
 * @code
 * // Add key-value metadata to logs for better searchability
 * LOG(info).add("user_id", 12345)
 *          .add("action", "login")
 *          .add("ip", "192.168.1.1")
 *       << "User logged in successfully";
 *
 * // Supports any formattable type
 * LOG(warn).add("temperature", 98.5)
 *          .add("threshold", 95.0)
 *          .add("sensor", "CPU_CORE_0")
 *       << "Temperature exceeds threshold";
 *
 * // Chain with format() for complex messages
 * LOG(error).add("request_id", request.id)
 *           .add("error_code", err.code())
 *           .add("retry_count", retries)
 *           .format("Request failed: {}", err.what());
 * @endcode
 *
 * Module Support:
 * @code
 * // Define a module name for the current compilation unit
 * LOG_MODULE_NAME("network");
 *
 * // Set module-specific log level
 * LOG_MODULE_LEVEL(log_level::debug);
 *
 * // All LOG() calls in this file will use the "network" module settings
 * LOG(trace) << "This won't show if network module level is debug";
 * LOG(info) << "This will show";
 * @endcode
 *
 * Log Site Registry:
 * - Every LOG() macro invocation is automatically registered at compile time
 * - Only sites with level >= GLOBAL_MIN_LOG_LEVEL are registered (zero overhead for disabled levels)
 * - Registry tracks file/line/function/level for each call site
 * - Filename column width automatically adjusts to the longest registered filename
 *
 * Registry Usage:
 * @code
 * // Get all registered log sites
 * const auto& sites = log_site_registry::sites();
 * for (const auto& site : sites) {
 *     std::cout << "Log at " << site.file << ":" << site.line
 *               << " level: " << log_level_names[static_cast<int>(site.min_level)] << "\n";
 * }
 *
 * // Find specific log site
 * auto* site = log_site_registry::find_site("main.cpp", 42);
 * @endcode
 *
 * Log Levels (in order of severity):
 * - trace: Finest-grained debugging information
 * - debug: Debugging information
 * - info:  General informational messages
 * - warn:  Warning messages
 * - error: Error messages
 * - fatal: Critical errors requiring immediate attention
 *
 * Performance Notes:
 * - Set GLOBAL_MIN_LOG_LEVEL to eliminate lower levels at compile time
 * - Logs below GLOBAL_MIN_LOG_LEVEL have zero runtime overhead
 * - Buffer pool pre-allocates BUFFER_POOL_SIZE buffers
 * - Each buffer is LOG_BUFFER_SIZE bytes, cache-line aligned
 * - Log site registration happens once per call site via static initialization
 *
 * Thread Safety:
 * - All logging operations are thread-safe
 * - Log order is preserved within each thread
 * - Timestamps reflect log creation time, not output time
 * - Module and site registries are protected by mutexes
 * - Don't call LOG() from destructors of global/static objects, just to be safe.
 *
 * Advanced Features:
 * - Multi-line logs are automatically indented for alignment
 * - Custom sinks can be added for specialized output handling
 * - Printf-style methods available but discouraged (use format() instead)
 * - Dynamic module discovery and level control
 * - Complete inventory of all active log sites in the binary
 *
 * Runtime Module Control:
 * @code
 * // Get all registered modules
 * auto modules = log_module_registry::instance().get_all_modules();
 * for (auto* mod : modules) {
 *     std::cout << "Module: " << mod->name
 *               << " Level: " << static_cast<int>(mod->level.load()) << "\n";
 * }
 *
 * // Adjust module level at runtime
 * auto* net_module = log_module_registry::instance().get_module("network");
 * net_module->level.store(log_level::warn);  // Only warn and above for network
 * @endcode
 *
 * Configuration:
 * - GLOBAL_MIN_LOG_LEVEL: Compile-time minimum log level
 * - SOURCE_FILE_NAME: Set by CMake for relative file paths in log output.
 *                     When using the amalgamated header without CMake, automatically
 *                     falls back to __FILE__ for full paths.
 * - LOG_MODULE_NAME: Define per-file module name for categorized logging
 * - LOG_MODULE_LEVEL: Set initial log level for the current module
 *
 * Implementation Notes:
 * - Log sites are registered via static initialization within if constexpr blocks
 * - Registration only occurs for levels >= GLOBAL_MIN_LOG_LEVEL (zero overhead for disabled)
 * - Static initialization order between compilation units is not guaranteed
 * - The __FUNCTION__ parameter in log sites is reserved for future use
 * - Module names are case-sensitive and persist for the lifetime of the program
 * - Filename alignment is calculated based on all registered sites at startup
 *
 * Limitations and Debugging:
 * - Site registration happens during static initialization, so sites in dynamically
 *   loaded libraries won't affect filename alignment
 * - Module level changes affect all future logs but not buffered ones
 * - Very long filenames may still be truncated for readability
 * - To debug module assignment: check g_log_module_info.detail->name in debugger
 * - Site registry can be queried at runtime to verify LOG() macro expansion
 *
 * Runtime Diagnostics and Performance Monitoring:
 *
 * The logging system provides comprehensive statistics and diagnostics for monitoring
 * performance, detecting issues, and capacity planning. All statistics are designed
 * for minimal overhead using single-writer patterns where possible.
 *
 * Metrics collection is optional and must be enabled at compile time by defining:
 * - LOG_COLLECT_BUFFER_POOL_METRICS - Buffer pool statistics
 * - LOG_COLLECT_DISPATCHER_METRICS - Dispatcher and queue statistics
 * - LOG_COLLECT_STRUCTURED_METRICS - Structured logging drop statistics
 * - LOG_COLLECT_DISPATCHER_MSG_RATE - Sliding window message rate tracking
 *
 * Available Metrics (when enabled):
 *
 * 1. Buffer Pool Statistics (buffer_pool::get_stats()):
 *    - total_buffers: Total number of pre-allocated buffers
 *    - available_buffers: Currently available for use
 *    - in_use_buffers: Currently checked out
 *    - total_acquires: Lifetime acquire operations
 *    - acquire_failures: Failed acquires (pool exhausted)
 *    - total_releases: Lifetime release operations
 *    - usage_percent: Current utilization percentage
 *    - pool_memory_kb: Total memory footprint
 *    - high_water_mark: Maximum buffers ever in use
 *
 * 2. Dispatcher Statistics (log_line_dispatcher::instance().get_stats()):
 *    - total_dispatched: Total messages processed
 *    - queue_enqueue_failures: Failed enqueue attempts
 *    - current_queue_size: Messages waiting in queue
 *    - max_queue_size: Peak queue size observed
 *    - total_flushes: Flush operations performed
 *    - messages_dropped: Lost messages (shutdown/failures)
 *    - queue_usage_percent: Queue utilization
 *    - worker_iterations: Worker loop iterations
 *    - active_sinks: Currently configured sinks
 *    - avg_dispatch_time_us: Average sink dispatch time
 *    - max_dispatch_time_us: Maximum sink dispatch time
 *    - messages_per_second_1s: Message rate over last 1 second (requires LOG_COLLECT_DISPATCHER_MSG_RATE)
 *    - messages_per_second_10s: Message rate over last 10 seconds (requires LOG_COLLECT_DISPATCHER_MSG_RATE)
 *    - messages_per_second_60s: Message rate over last 60 seconds (requires LOG_COLLECT_DISPATCHER_MSG_RATE)
 *    - avg_batch_size: Average messages per batch
 *    - total_batches: Total batches processed
 *    - max_batch_size: Maximum batch size observed
 *    - min_inflight_time_us: Minimum in-flight time (creation to sink completion)
 *    - avg_inflight_time_us: Average in-flight time
 *    - max_inflight_time_us: Maximum in-flight time
 *    - uptime: Time since dispatcher started
 *
 * 3. Structured Logging Statistics:
 *    - Key Registry (structured_log_key_registry::instance().get_stats()):
 *      - key_count: Number of registered keys
 *      - max_keys: Maximum allowed
 *      - usage_percent: Registry utilization
 *      - estimated_memory_kb: Memory usage estimate
 *
 *    - Metadata Drops (log_buffer_metadata_adapter::get_drop_stats()):
 *      - drop_count: Metadata entries dropped
 *      - dropped_bytes: Total bytes dropped
 *
 * Monitoring Examples:
 * @code
 * #ifdef LOG_COLLECT_BUFFER_POOL_METRICS
 * // Check buffer pool health
 * auto pool_stats = buffer_pool::instance().get_stats();
 * if (pool_stats.usage_percent > 80.0f) {
 *     std::cerr << "WARNING: Buffer pool " << pool_stats.usage_percent
 *               << "% full, high water mark: " << pool_stats.high_water_mark
 *               << "/" << pool_stats.total_buffers << "\n";
 * }
 * #endif
 *
 * #ifdef LOG_COLLECT_DISPATCHER_METRICS
 * // Monitor dispatcher performance
 * auto disp_stats = log_line_dispatcher::instance().get_stats();
 * std::cout << "Logging stats:\n"
 *           << "  Messages: " << disp_stats.total_dispatched << "\n"
 *           << "  Dropped: " << disp_stats.messages_dropped << "\n"
 *           << "  Queue size: " << disp_stats.current_queue_size
 *           << " (max: " << disp_stats.max_queue_size << ")\n"
 *           << "  Avg dispatch: " << disp_stats.avg_dispatch_time_us << " µs\n"
 *           << "  Max dispatch: " << disp_stats.max_dispatch_time_us << " µs\n";
 * #endif
 *
 * #ifdef LOG_COLLECT_STRUCTURED_METRICS
 * // Check for metadata drops
 * auto [drops, bytes] = log_buffer_metadata_adapter::get_drop_stats();
 * if (drops > 0) {
 *     std::cerr << "WARNING: Dropped " << drops << " metadata entries ("
 *               << bytes << " bytes) - consider smaller metadata\n";
 * }
 * #endif
 *
 * // Health check function (requires all metrics enabled)
 * #if defined(LOG_COLLECT_BUFFER_POOL_METRICS) && defined(LOG_COLLECT_DISPATCHER_METRICS)
 * bool check_logging_health() {
 *     auto pool = buffer_pool::instance().get_stats();
 *     auto disp = log_line_dispatcher::instance().get_stats();
 *
 *     return pool.acquire_failures == 0 &&
 *            disp.messages_dropped == 0 &&
 *            disp.queue_enqueue_failures == 0 &&
 *            pool.usage_percent < 90.0f;
 * }
 * #endif
 * @endcode
 *
 * Performance Tuning Guidelines:
 * - Buffer pool exhaustion: Increase BUFFER_POOL_SIZE or reduce log rate
 * - High dispatch times: Check sink performance, consider async sinks
 * - Queue growth: Worker thread may be CPU starved or sinks too slow
 * - Metadata drops: Reduce metadata size or increase METADATA_RESERVE
 * - Key registry full: Audit key usage, use consistent key names
 *
 * Statistics are designed for production use with minimal overhead:
 * - Single-writer counters avoid atomic operations where possible
 * - No locks in hot paths (dispatch/worker thread)
 * - Approximate queue size to avoid contention
 * - Statistics can be reset for testing via reset_stats() methods
 *
 */

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <string_view>
#include <format>
#include <chrono>
#include <memory>
#include <atomic>
#include <sys/types.h>
#include <thread>
#include <mutex>
#include <algorithm>
#include <unistd.h> // For write() and STDOUT_FILENO
#include <fcntl.h>
#include "moodycamel/concurrentqueue.h"
#include "moodycamel/blockingconcurrentqueue.h"

#include "log_types.hpp"
#include "log_site.hpp"
#include "log_module.hpp"
#include "log_buffer.hpp"
#include "log_sink.hpp"
#include "log_formatters.hpp"
#include "log_writers.hpp"
#include "log_version.hpp"

namespace slwoggy {

inline log_sink make_stdout_sink()
{
    return log_sink{
        raw_formatter{true, true},
        file_writer{STDOUT_FILENO}
    };
}

inline log_sink make_json_sink()
{
    return log_sink{
        json_formatter{true, true},
        file_writer{STDOUT_FILENO}
    };
}

} // namespace slwoggy

// formatter specializations for smart pointers
namespace std
{
template <typename T> struct formatter<std::shared_ptr<T>, char> : formatter<const void *, char>
{
    auto format(const std::shared_ptr<T> &ptr, format_context &ctx) const
    {
        if (!ptr) return format_to(ctx.out(), "nullptr");
        return formatter<const void *, char>::format(ptr.get(), ctx);
    }
};

// Specialization for weak pointers
template <typename T> struct formatter<std::weak_ptr<T>, char> : formatter<const void *, char>
{
    auto format(const std::weak_ptr<T> &ptr, format_context &ctx) const
    {
        if (auto shared = ptr.lock()) { return formatter<const void *, char>::format(shared.get(), ctx); }
        return format_to(ctx.out(), "(expired)");
    }
};
} // namespace std

namespace slwoggy {

/**
 * @brief Concept for types that can be logged
 * @tparam T The type to check
 */
template <typename T>
concept Loggable = requires(T value, std::string &str) {
    { std::format("{}", value) } -> std::convertible_to<std::string>;
};

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
            auto new_config = std::make_unique<sink_config>();
            new_config->sinks = sinks;  // Copies shared_ptr, not the sinks
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
        uint64_t max_batch_size;                    ///< Maximum batch size observed
        uint64_t min_inflight_time_us;              ///< Minimum in-flight time in microseconds
        double avg_inflight_time_us;                ///< Average in-flight time in microseconds
        uint64_t max_inflight_time_us;              ///< Maximum in-flight time in microseconds
        std::chrono::steady_clock::duration uptime; ///< Time since dispatcher started
    };
#endif

    void dispatch(struct log_line &line); // Defined after log_line
    void flush();                         // Flush pending logs
    void worker_thread_func();            // Worker thread function

    static log_line_dispatcher &instance()
    {
        static log_line_dispatcher instance;
        return instance;
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

        auto new_config = current->copy();
        if (index >= new_config->sinks.size()) {
            new_config->sinks.resize(index + 1);
        }
        new_config->sinks[index] = sink;

        update_sink_config(std::move(new_config));
    }

    void remove_sink(size_t index)
    {
        std::lock_guard<std::mutex> lock(sink_modify_mutex_);
        auto current = current_sinks_.load(std::memory_order_acquire);
        if (!current || index >= current->sinks.size()) return;

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

#ifdef LOG_COLLECT_DISPATCHER_METRICS
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
    std::mutex sink_modify_mutex_; // Only for modifications

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
    std::atomic<uint64_t> max_batch_size_{0};
    std::atomic<uint64_t> min_inflight_time_us_{UINT64_MAX};
    std::atomic<uint64_t> max_inflight_time_us_{0};

    // Batch tracking (single writer - worker thread)
    uint64_t total_batches_{0};
    uint64_t total_batch_messages_{0};

    // In-flight time tracking (single writer - worker thread)
    uint64_t total_inflight_time_us_{0};
    uint64_t inflight_count_{0};

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
    // Sliding window rate calculation
    static constexpr size_t RATE_WINDOW_SIZE   = 120;                            // 2 minutes of samples at 1/sec
    static constexpr auto RATE_SAMPLE_INTERVAL = std::chrono::milliseconds(100); // Sample every 100ms for better
                                                                                 // granularity

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

/**
 * @brief Represents a single log message with metadata
 *
 * This class handles the formatting and buffering of a single log message
 * along with its associated metadata (timestamp, level, location).
 */
struct log_line
{
    log_buffer *buffer_;
    bool needs_header_{false}; // True after swap, header written on first write

    // Store these for endl support and header writing
    log_level level_;
    std::string_view file_;
    uint32_t line_;
    std::chrono::steady_clock::time_point timestamp_;
    const log_module_info &module_;

    log_line() = delete;

    log_line(log_level level, log_module_info &mod, std::string_view file, uint32_t line)
    : buffer_(level != log_level::nolog ? buffer_pool::instance().acquire() : nullptr),
      level_(level),
      file_(file),
      line_(line),
      timestamp_(log_fast_timestamp()),
      needs_header_(true), // Start with header needed
      module_(mod)
    {
        if (buffer_)
        {
            buffer_->level_     = level_;
            buffer_->file_      = file_;
            buffer_->line_      = line_;
            buffer_->timestamp_ = timestamp_;
        }
    }

    // Move constructor - swap buffers
    log_line(log_line &&other) noexcept
    : buffer_(std::exchange(other.buffer_, nullptr)),
      needs_header_(std::exchange(other.needs_header_, false)),
      level_(other.level_),
      file_(other.file_),
      line_(other.line_),
      timestamp_(other.timestamp_),
      module_(other.module_)
    {
    }

    // Move assignment - swap buffers
    log_line &operator=(log_line &&other) noexcept
    {
        if (this != &other)
        {
            if (buffer_)
            {
                log_line_dispatcher::instance().dispatch(*this);
                buffer_->release();
            }

            // Move from other
            buffer_       = std::exchange(other.buffer_, nullptr);
            needs_header_ = std::exchange(other.needs_header_, false);
            level_        = other.level_;
            file_         = other.file_;
            line_         = other.line_;
            timestamp_    = other.timestamp_;
        }
        return *this;
    }

    // Keep copy deleted
    log_line(const log_line &)            = delete;
    log_line &operator=(const log_line &) = delete;

    ~log_line()
    {
        if (!buffer_) return;

        log_line_dispatcher::instance().dispatch(*this);

        if (buffer_)
        {
            // Release our reference to the buffer
            buffer_->release();
        }
    }

    void print(std::string_view str)
    {
        if (!buffer_) { return; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        buffer_->write_with_padding(str);
    }

    template <typename... Args> void printf(const char *format, Args &&...args)
    {
        if (!buffer_) { return; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        // Forward to buffer's printf implementation
        buffer_->printf_with_padding(format, std::forward<Args>(args)...);
    }

    // Helper method that returns *this for chaining
    template <typename... Args> log_line &printfmt(const char *format, Args &&...args)
    {
        printf(format, std::forward<Args>(args)...);
        return *this;
    }

    template <typename... Args> log_line &format(std::format_string<Args...> fmt, Args &&...args)
    {
        print(std::format(fmt, std::forward<Args>(args)...));
        return *this;
    }

    // Helper method that returns *this for chaining
    template <typename... Args> log_line &fmtprint(std::format_string<Args...> fmt, Args &&...args)
    {
        format(fmt, std::forward<Args>(args)...);
        return *this;
    }

    // Generic version for any formattable type
    template <typename T>
        requires Loggable<T>
    log_line &operator<<(const T &value)
    {
        print(std::format("{}", value));
        return *this;
    }

    template <typename T> log_line &operator<<(T *ptr)
    {
        if (ptr == nullptr) { print("nullptr"); }
        else { print(std::format("{}", static_cast<const void *>(ptr))); }
        return *this;
    }

    // Keep specialized versions for common types
    log_line &operator<<(std::string_view str)
    {
        print(str);
        return *this;
    }

    log_line &operator<<(const char *str)
    {
        print(std::string_view(str));
        return *this;
    }

    log_line &operator<<(const std::string &str)
    {
        print(std::string_view(str));
        return *this;
    }

    log_line &operator<<(int value)
    {
        print(std::format("{}", value));
        return *this;
    }

    log_line &operator<<(unsigned int value)
    {
        print(std::format("{}", value));
        return *this;
    }

    log_line &operator<<(void *ptr)
    {
        print(std::format("{}", ptr));
        return *this;
    }

    log_line &operator<<(log_line &(*func)(log_line &)) { return func(*this); }

    /**
     * @brief Add structured key-value metadata to the log entry
     *
     * Adds a key-value pair to the structured metadata section of the log.
     * The key is registered in the global key registry and stored as a
     * numeric ID for efficiency. The value is formatted to a string using
     * std::format.
     *
     * @tparam T Any type formattable by std::format
     * @param key The metadata key (e.g., "user_id", "request_id")
     * @param value The value to associate with the key
     * @return *this for method chaining
     *
     * @note If the metadata section is full or an error occurs, the operation
     *       is silently ignored to ensure logging continues to work.
     *
     * Example:
     * @code
     * LOG(info).add("user_id", 123)
     *          .add("action", "login")
     *          .add("duration_ms", 45.7)
     *       << "User login completed";
     * @endcode
     */
    template <typename T> log_line &add(std::string_view key, T &&value)
    {
        if (!buffer_) return *this;

        try
        {
            uint16_t key_id = structured_log_key_registry::instance().get_or_register_key(key);

            auto metadata = buffer_->get_metadata_adapter();
            metadata.add_kv_formatted(key_id, std::forward<T>(value));
        }
        catch (...)
        {
            // Silently ignore metadata errors to not break logging
        }

        return *this;
    }

    // Swap buffer with a new one from pool, return old buffer
    log_buffer *swap_buffer()
    {
        auto *old_buffer = buffer_;
        buffer_          = buffer_pool::instance().acquire();

        // Reset positions
        needs_header_ = true;

        // Set buffer metadata if we got a new buffer
        if (buffer_)
        {
            buffer_->level_     = level_;
            buffer_->file_      = file_;
            buffer_->line_      = line_;
            buffer_->timestamp_ = timestamp_;
        }

        return old_buffer;
    }

  private:
    // Write header to buffer and return width
    size_t write_header()
    {
        if (!buffer_) return 0;

        // Use stored timestamp
        auto &dispatcher = log_line_dispatcher::instance();
        int64_t diff_us = std::chrono::duration_cast<std::chrono::microseconds>(timestamp_ - dispatcher.start_time()).count();
        int64_t ms = diff_us / 1000;
        int64_t us = std::abs(diff_us % 1000);

        // Format header: "TTTTTTTT.mmm [LEVEL]    file:line "
        // Note: header doesn't need padding since it's the first line
        size_t text_len_before = buffer_->len();
        buffer_->printf_with_padding("%08lld.%03lld [%-5s] %-10s %*.*s:%d ",
                                     static_cast<long long>(ms),
                                     static_cast<long long>(us),
                                     log_level_names[static_cast<int>(level_)],
                                     module_.detail->name,
                                     log_site_registry::longest_file(),
                                     std::min(log_site_registry::longest_file(), static_cast<int>(file_.size())),
                                     file_.data(),
                                     line_);

        buffer_->header_width_ = buffer_->len() - text_len_before;
        return buffer_->header_width_;
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

    // Initialize with empty sink config
    auto initial_config = std::make_unique<sink_config>();
    current_sinks_.store(initial_config.release(), std::memory_order_release);

    auto sink1 = make_stdout_sink();
    add_sink(std::make_shared<log_sink>(std::move(sink1)));
    //static log_sink_file log_sink_file("/tmp/log.txt");
    //add_sink(&log_sink_file);

    // static log_sink_json log_sink_json;
    // add_sink(&log_sink_json);
}

inline log_line_dispatcher::~log_line_dispatcher()
{
    // Signal shutdown
    shutdown_.store(true);

    // Enqueue sentinel to ensure worker wakes up
    // (Important: we must ensure an element arrives per the docs)
    queue_.enqueue(nullptr);

    // Wait for worker to finish
    if (worker_thread_.joinable()) { worker_thread_.join(); }

    // Clean up sink config
    auto *config = current_sinks_.load(std::memory_order_acquire);
    delete config;
}

inline void log_line_dispatcher::dispatch(struct log_line &line)
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
        else
        {
#ifdef LOG_COLLECT_DISPATCHER_METRICS
            total_dispatched_++;

            // Track max queue size
            auto queue_size      = queue_.size_approx();
            uint64_t current_max = max_queue_size_.load(std::memory_order_relaxed);
            while (queue_size > current_max)
            {
                if (max_queue_size_.compare_exchange_weak(current_max, queue_size, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                    break;
                }
            }
#endif
        }
    }
}

// Worker thread function implementation
inline void log_line_dispatcher::worker_thread_func()
{
    moodycamel::ConsumerToken consumer_token(queue_);
    log_buffer *buffers[MAX_BATCH_SIZE];
    size_t dequeued_count;
    bool should_shutdown = false;

    while (!should_shutdown)
    {
#ifdef LOG_COLLECT_DISPATCHER_METRICS
        worker_iterations_++;
#endif

        // Try to dequeue a batch of buffers
        dequeued_count = queue_.wait_dequeue_bulk_timed(consumer_token, buffers, MAX_BATCH_SIZE, std::chrono::milliseconds(10));

        // If no items after timeout, check if we should shut down
        if (dequeued_count == 0)
        {
            if (shutdown_.load(std::memory_order_relaxed)) { break; }
            continue;
        }

#ifdef LOG_COLLECT_DISPATCHER_METRICS
        // Track batch statistics
        total_batches_++;
        total_batch_messages_ += dequeued_count;

        // Update max batch size
        uint64_t current_max_batch = max_batch_size_.load(std::memory_order_relaxed);
        while (dequeued_count > current_max_batch)
        {
            if (max_batch_size_.compare_exchange_weak(current_max_batch, dequeued_count, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                break;
            }
        }

        // Track batch dispatch timing
        auto dispatch_start = log_fast_timestamp();
#endif

        // Get sink config once for the batch
        auto *config = current_sinks_.load(std::memory_order_acquire);

        // Process buffers using batch processing
        size_t buf_idx = 0;
        while (buf_idx < dequeued_count)
        {
            log_buffer *buffer = buffers[buf_idx];

            // Check for shutdown marker
            if (!buffer)
            {
                // Process remaining buffers before shutting down
                if (buf_idx + 1 < dequeued_count && config && !config->sinks.empty())
                {
                    size_t remaining_count = dequeued_count - buf_idx - 1;
                    size_t processed = 0;
                    
                    // Dispatch remaining buffers as batch to all sinks
                    for (size_t i = 0; i < config->sinks.size(); ++i)
                    {
                        if (config->sinks[i])
                        {
                            size_t sink_processed = config->sinks[i]->process_batch(&buffers[buf_idx + 1], remaining_count);
                            
                            #ifndef NDEBUG
                            if (i == 0) {
                                processed = sink_processed;
                            } else {
                                // All sinks must process the same number of buffers
                                assert(sink_processed == processed);
                            }
                            #else
                            processed = sink_processed;
                            #endif
                        }
                    }

#ifdef LOG_COLLECT_DISPATCHER_METRICS
                    // Track in-flight time for processed buffers
                    auto now = log_fast_timestamp();
                    for (size_t j = 0; j < processed; ++j)
                    {
                        log_buffer *proc_buffer = buffers[buf_idx + 1 + j];
                        if (proc_buffer && !proc_buffer->is_flush_marker())
                        {
                            auto inflight_us = std::chrono::duration_cast<std::chrono::microseconds>(now - proc_buffer->timestamp_).count();
                            total_inflight_time_us_ += inflight_us;
                            inflight_count_++;

                            // Update min/max
                            uint64_t current_min = min_inflight_time_us_.load(std::memory_order_relaxed);
                            while (static_cast<uint64_t>(inflight_us) < current_min)
                            {
                                if (min_inflight_time_us_.compare_exchange_weak(current_min, inflight_us, std::memory_order_relaxed, std::memory_order_relaxed))
                                {
                                    break;
                                }
                            }

                            uint64_t current_max = max_inflight_time_us_.load(std::memory_order_relaxed);
                            while (static_cast<uint64_t>(inflight_us) > current_max)
                            {
                                if (max_inflight_time_us_.compare_exchange_weak(current_max, inflight_us, std::memory_order_relaxed, std::memory_order_relaxed))
                                {
                                    break;
                                }
                            }
                        }
                    }
#endif
                    
                    // Release all buffers (processed and unprocessed)
                    for (size_t j = buf_idx + 1; j < dequeued_count; ++j)
                    {
                        if (buffers[j]) buffers[j]->release();
                    }
                }
                should_shutdown = true;
                break;
            }

            // Check for flush marker
            if (buffer->is_flush_marker())
            {
                buffer->release();
                {
                    std::lock_guard lk(flush_mutex_);
                    flush_done_.fetch_add(1, std::memory_order_release);
                    flush_cv_.notify_all();
                }
                buf_idx++;
                continue;
            }

            // Process batch starting from current position
            if (config && !config->sinks.empty())
            {
                size_t remaining = dequeued_count - buf_idx;
                size_t processed = 0;
                
                // Dispatch batch to all sinks
                for (size_t i = 0; i < config->sinks.size(); ++i)
                {
                    if (config->sinks[i])
                    {
                        size_t sink_processed = config->sinks[i]->process_batch(&buffers[buf_idx], remaining);
                        
                        #ifndef NDEBUG
                        if (i == 0) {
                            processed = sink_processed;
                        } else {
                            // All sinks must process the same number of buffers
                            assert(sink_processed == processed);
                        }
                        #else
                        processed = sink_processed;
                        #endif
                    }
                }

#ifdef LOG_COLLECT_DISPATCHER_METRICS
                // Track in-flight time for processed buffers
                auto now = log_fast_timestamp();
                for (size_t j = 0; j < processed; ++j)
                {
                    log_buffer *proc_buffer = buffers[buf_idx + j];
                    auto inflight_us = std::chrono::duration_cast<std::chrono::microseconds>(now - proc_buffer->timestamp_).count();
                    total_inflight_time_us_ += inflight_us;
                    inflight_count_++;

                    // Update min in-flight time
                    uint64_t current_min = min_inflight_time_us_.load(std::memory_order_relaxed);
                    while (static_cast<uint64_t>(inflight_us) < current_min)
                    {
                        if (min_inflight_time_us_.compare_exchange_weak(current_min, inflight_us, std::memory_order_relaxed, std::memory_order_relaxed))
                        {
                            break;
                        }
                    }

                    // Update max in-flight time
                    uint64_t current_max = max_inflight_time_us_.load(std::memory_order_relaxed);
                    while (static_cast<uint64_t>(inflight_us) > current_max)
                    {
                        if (max_inflight_time_us_.compare_exchange_weak(current_max, inflight_us, std::memory_order_relaxed, std::memory_order_relaxed))
                        {
                            break;
                        }
                    }
                }
#endif

                // Release processed buffers
                for (size_t j = 0; j < processed; ++j)
                {
                    buffers[buf_idx + j]->release();
                }
                
                // If no buffers were processed (all sinks are null), skip this buffer
                if (processed == 0)
                {
                    buffers[buf_idx]->release();
                    processed = 1;
                }
                
                buf_idx += processed;
            }
            else
            {
                // No sinks - check each buffer individually for markers
                while (buf_idx < dequeued_count)
                {
                    log_buffer *buf = buffers[buf_idx];
                    
                    // Stop at markers to process them properly
                    if (!buf || buf->is_flush_marker())
                    {
                        break;
                    }
                    
                    // Release regular buffer
                    buf->release();
                    buf_idx++;
                }
            }
        }

#ifdef LOG_COLLECT_DISPATCHER_METRICS
        // Track dispatch time for the entire batch
        auto dispatch_end = log_fast_timestamp();
        auto dispatch_us = std::chrono::duration_cast<std::chrono::microseconds>(dispatch_end - dispatch_start).count();
        total_dispatch_time_us_ += dispatch_us;
        dispatch_count_for_avg_ += dequeued_count;

        // Update max dispatch time if this is a new maximum (per buffer average for batch)
        auto per_buffer_us   = dispatch_us / dequeued_count;
        uint64_t current_max = max_dispatch_time_us_.load(std::memory_order_relaxed);
        while (static_cast<uint64_t>(per_buffer_us) > current_max)
        {
            if (max_dispatch_time_us_.compare_exchange_weak(current_max, per_buffer_us, std::memory_order_relaxed, std::memory_order_relaxed))
            {
                break;
            }
        }

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
        // Record throughput sample (throttled to avoid too many samples)
        auto now = log_fast_timestamp();
        if (now - last_rate_sample_time_ >= RATE_SAMPLE_INTERVAL)
        {
            rate_samples_[rate_write_idx_] = {now, total_dispatched_};
            rate_write_idx_                = (rate_write_idx_ + 1) % RATE_WINDOW_SIZE;
            if (rate_sample_count_ < RATE_WINDOW_SIZE) rate_sample_count_++;
            last_rate_sample_time_ = now;
        }
    #endif
#endif
    }

    // Drain remaining buffers on shutdown using batch dequeue
    while ((dequeued_count = queue_.try_dequeue_bulk(consumer_token, buffers, MAX_BATCH_SIZE)) > 0)
    {
        auto *config = current_sinks_.load(std::memory_order_acquire);

        // Process buffers in batches, respecting markers even during shutdown
        size_t buf_idx = 0;
        while (buf_idx < dequeued_count)
        {
            log_buffer *buffer = buffers[buf_idx];
            
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
                size_t processed = 0;
                
                // Dispatch batch to all sinks
                for (size_t i = 0; i < config->sinks.size(); ++i)
                {
                    if (config->sinks[i])
                    {
                        size_t sink_processed = config->sinks[i]->process_batch(&buffers[buf_idx], remaining);
                        
                        #ifndef NDEBUG
                        if (i == 0) {
                            processed = sink_processed;
                        } else {
                            // All sinks must process the same number of buffers
                            assert(sink_processed == processed);
                        }
                        #else
                        processed = sink_processed;
                        #endif
                    }
                }
                
                // Release processed buffers
                for (size_t j = 0; j < processed; ++j)
                {
                    buffers[buf_idx + j]->release();
                }
                
                // If no buffers were processed (all sinks are null), skip this buffer
                if (processed == 0)
                {
                    buffers[buf_idx]->release();
                    processed = 1;
                }
                
                buf_idx += processed;
            }
            else
            {
                // No sinks - check each buffer individually for markers
                while (buf_idx < dequeued_count)
                {
                    log_buffer *buf = buffers[buf_idx];
                    
                    // Stop at markers to process them properly
                    if (!buf || buf->is_flush_marker())
                    {
                        break;
                    }
                    
                    // Release regular buffer
                    buf->release();
                    buf_idx++;
                }
            }
        }
    }
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
#ifdef LOG_COLLECT_DISPATCHER_METRICS
    total_flushes_.fetch_add(1, std::memory_order_relaxed);
#endif

    auto *marker = buffer_pool::instance().acquire();
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
    s.max_queue_size         = max_queue_size_.load(std::memory_order_relaxed);
    s.total_flushes          = total_flushes_.load(std::memory_order_relaxed);
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

    // Get max dispatch time
    s.max_dispatch_time_us = max_dispatch_time_us_.load(std::memory_order_relaxed);

    // Calculate uptime
    s.uptime = log_fast_timestamp() - start_time_;

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
    // Calculate message rates for different windows
    s.messages_per_second_1s  = calculate_rate_for_window(std::chrono::seconds(1));
    s.messages_per_second_10s = calculate_rate_for_window(std::chrono::seconds(10));
    s.messages_per_second_60s = calculate_rate_for_window(std::chrono::seconds(60));
    #endif

    // Calculate batch statistics
    s.total_batches  = total_batches_;
    s.max_batch_size = max_batch_size_.load(std::memory_order_relaxed);
    if (total_batches_ > 0) { s.avg_batch_size = static_cast<double>(total_batch_messages_) / total_batches_; }
    else { s.avg_batch_size = 0.0; }

    // Calculate in-flight time statistics
    s.min_inflight_time_us = min_inflight_time_us_.load(std::memory_order_relaxed);
    s.max_inflight_time_us = max_inflight_time_us_.load(std::memory_order_relaxed);

    // Handle case where no messages have been processed
    if (s.min_inflight_time_us == UINT64_MAX) { s.min_inflight_time_us = 0; }

    if (inflight_count_ > 0)
    {
        s.avg_inflight_time_us = static_cast<double>(total_inflight_time_us_) / inflight_count_;
    }
    else { s.avg_inflight_time_us = 0.0; }

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
    max_queue_size_.store(0, std::memory_order_relaxed);
    total_flushes_.store(0, std::memory_order_relaxed);
    max_dispatch_time_us_.store(0, std::memory_order_relaxed);
    max_batch_size_.store(0, std::memory_order_relaxed);
    min_inflight_time_us_.store(UINT64_MAX, std::memory_order_relaxed);
    max_inflight_time_us_.store(0, std::memory_order_relaxed);

    #ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
    rate_write_idx_        = 0;
    rate_sample_count_     = 0;
    last_rate_sample_time_ = std::chrono::steady_clock::time_point{};
    #endif
}
#endif

// our own endl for log_line
inline log_line &endl(log_line &line)
{
    log_line_dispatcher::instance().dispatch(line);
    return line;
}

/**
 * @brief Creates a log line with specified level and automatic source location.
 *
 * This macro is the primary interface for logging. It performs several operations:
 * 1. Compile-time filtering: Logs below GLOBAL_MIN_LOG_LEVEL are completely eliminated
 * 2. Site registration: Each unique LOG() location is registered once via static init
 * 3. Runtime filtering: Checks against the current module's dynamic log level
 * 4. Returns a log_line object that supports streaming (<<) and format() operations
 *
 * @param _level The log level (trace, debug, info, warn, error, fatal)
 *
 * Implementation details:
 * - Uses a lambda to create a self-contained expression
 * - Static local struct ensures one-time registration per call site
 * - Registration occurs inside if constexpr, so only active sites are tracked
 * - Two-level filtering: compile-time (GLOBAL_MIN_LOG_LEVEL) and runtime (module level)
 *
 * @code
 * LOG(info) << "Starting application";
 * LOG(debug).format("Processing {} items", count);
 * LOG(error) << "Failed: " << error_msg << endl;  // endl forces immediate flush
 * @endcode
 */
#define LOG(_level)                                                                                                           \
    []()                                                                                                                      \
    {                                                                                                                         \
        constexpr ::slwoggy::log_level level = ::slwoggy::log_level::_level;                                                  \
        if constexpr (level >= ::slwoggy::GLOBAL_MIN_LOG_LEVEL)                                                               \
        {                                                                                                                     \
            static struct                                                                                                     \
            {                                                                                                                 \
                struct registrar                                                                                              \
                {                                                                                                             \
                    ::slwoggy::log_site_descriptor &site_;                                                                    \
                    registrar()                                                                                               \
                    : site_(::slwoggy::log_site_registry::register_site(SOURCE_FILE_NAME, __LINE__, level, __func__))         \
                    {                                                                                                         \
                    }                                                                                                         \
                } r_;                                                                                                         \
            } _reg;                                                                                                           \
            if (level >= g_log_module_info.detail->level.load(std::memory_order_relaxed) && level >= _reg.r_.site_.min_level) \
            {                                                                                                                 \
                return ::slwoggy::log_line(level, ::slwoggy::g_log_module_info, SOURCE_FILE_NAME, __LINE__);                  \
            }                                                                                                                 \
        }                                                                                                                     \
        return ::slwoggy::log_line(::slwoggy::log_level::nolog, ::slwoggy::g_log_module_info, "", 0);                         \
    }()

} // namespace slwoggy
