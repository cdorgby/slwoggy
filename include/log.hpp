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
 * // The logger starts with a default stdout sink, so this works immediately:
 * LOG(info) << "Application started";
 *
 * // Preferred: Modern C++20 format style
 * LOG(warn).format("Temperature {}°C exceeds threshold {}°C", temp, max_temp);
 * LOG(info).format("User {} logged in from {}", username, ip_address);
 *
 * // Stream style with operator<<
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
 * Sink Configuration:
 * @code
 * // By default, logs go to stdout. No configuration needed:
 * LOG(info) << "This appears on stdout immediately";
 * 
 * // To use a custom sink, the first add_sink() replaces the default:
 * log_line_dispatcher::instance().add_sink(make_raw_file_sink("/var/log/app.log"));
 * 
 * // Now logs go to the file instead of stdout
 * LOG(info) << "This goes to the file";
 * 
 * // Add additional sinks (they append, not replace):
 * log_line_dispatcher::instance().add_sink(make_json_sink());
 * 
 * // Now logs go to both the file and stdout (as JSON)
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
 * - Thread-local cache memory: Each thread logging structured data uses
 *   ~2-4KB for key caching (scales with thread count)
 *
 * Statistics are designed for production use with minimal overhead:
 * - Single-writer counters avoid atomic operations where possible
 * - No locks in hot paths (dispatch/worker thread)
 * - Approximate queue size to avoid contention
 * - Statistics can be reset for testing via reset_stats() methods
 *
 */

#include <cstring>
#include <cstdlib>
#include <format>
#include <memory>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "log_types.hpp"      // IWYU pragma: keep
#include "log_site.hpp"       // IWYU pragma: keep
#include "log_module.hpp"     // IWYU pragma: keep
#include "log_buffer.hpp"     // IWYU pragma: keep
#include "log_sink.hpp"       // IWYU pragma: keep
#include "log_formatters.hpp" // IWYU pragma: keep
#include "log_writers.hpp"    // IWYU pragma: keep
#include "log_sinks.hpp"      // IWYU pragma: keep
#include "log_version.hpp"    // IWYU pragma: keep
#include "log_line.hpp"       // IWYU pragma: keep
#include "log_dispatcher.hpp" // IWYU pragma: keep

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


#ifndef SOURCE_FILE_NAME
// Fallback to __FILE__ if SOURCE_FILE_NAME is not defined
#define SOURCE_FILE_NAME __FILE__
#endif
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


#include "log_line_impl.hpp"       // IWYU pragma: keep
#include "log_dispatcher_impl.hpp" // IWYU pragma: keep