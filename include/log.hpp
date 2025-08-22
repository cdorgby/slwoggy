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
 * // Three logging macros are available - choose based on your needs:
 *
 * // LOG() - Default macro, uses traditional text format (same as LOG_TEXT)
 * LOG(info) << "Application started";
 * // Output: 00001234.567 [INFO ] module     file.cpp:42 Application started
 *
 * // LOG_TEXT() - Explicit traditional format (human-readable)
 * LOG_TEXT(warn).format("Temperature {}°C exceeds threshold {}°C", temp, max_temp);
 * // Output: 00001234.567 [WARN ] module     file.cpp:43 Temperature 98°C exceeds threshold 95°C
 *
 * // LOG_STRUCTURED() - Machine-parseable logfmt format
 * LOG_STRUCTURED(info) << "User logged in";
 * // Output: msg="User logged in" ts=1234567890 level=info module=myapp file=file.cpp line=44
 *
 * // All macros support the same features:
 * LOG(debug) << "Processing " << count << " items";                    // Stream style
 * LOG_TEXT(error).format("Failed: {}", error_msg);                     // Format style
 * LOG_STRUCTURED(info).add("user_id", 123) << "Login successful";      // With metadata
 *
 * // Immediate flush with endl works with all formats
 * LOG(fatal) << "Critical error: " << error << endl;
 * @endcode
 *
 * Structured Logging:
 * @code
 * // Add key-value metadata to logs for better searchability
 * // Works with all log macros - metadata is stored internally
 * LOG(info).add("user_id", 12345)
 *          .add("action", "login")
 *          .add("ip", "192.168.1.1")
 *       << "User logged in successfully";
 *
 * // LOG_STRUCTURED outputs metadata in logfmt format
 * LOG_STRUCTURED(warn).add("temperature", 98.5)
 *                     .add("threshold", 95.0)
 *                     .add("sensor", "CPU_CORE_0")
 *       << "Temperature exceeds threshold";
 * // Output: msg="Temperature exceeds threshold" temperature=98.5 threshold=95.0 sensor=CPU_CORE_0 ...
 *
 * // Note: The msg="..." in LOG_STRUCTURED output is a format prefix, not structured data
 * // If you add your own "msg" field, both will appear:
 * LOG_STRUCTURED(info).add("msg", "custom") << "Text";
 * // Output: msg="Text" msg="custom" ts=... level=...
 * //         ^^^^^^^^^^  ^^^^^^^^^^^^
 * //         Format prefix  Your field
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
 * Per-Sink Filtering:
 * @code  
 * // Console shows warnings and above
 * auto console = make_stdout_sink(level_filter{log_level::warn});
 * 
 * // File captures everything for debugging
 * auto debug_file = make_raw_file_sink("/var/log/debug.log");
 * 
 * // Error log captures only errors and fatal
 * auto error_file = make_raw_file_sink("/var/log/errors.log", {}, 
 *                                       level_filter{log_level::error});
 * 
 * // Complex filter: warnings and errors only (not debug or fatal)
 * and_filter warn_error_only;
 * warn_error_only.add(level_filter{log_level::warn})
 *                .add(max_level_filter{log_level::error});
 * auto filtered = make_stdout_sink(warn_error_only);
 * 
 * // Composite OR filter: debug messages OR errors and above
 * or_filter debug_or_severe;
 * debug_or_severe.add(level_range_filter{log_level::debug, log_level::debug})
 *                .add(level_filter{log_level::error});
 * 
 * // NOT filter: everything except info level
 * auto no_info = make_stdout_sink(not_filter{level_range_filter{
 *     log_level::info, log_level::info}});
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
 * - SLWOGGY_RELIABLE_DELIVERY: Enable blocking behavior when buffer pool exhausted (default: enabled)
 *                          When enabled: Threads block until buffers available (no message loss)
 *                          When disabled: Returns nullptr immediately (higher throughput, may drop)
 *                          To disable: #undef SLWOGGY_RELIABLE_DELIVERY before including log.hpp
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
 * - LOG_COLLECT_BUFFER_POOL_METRICS - Buffer pool statistics with usage tracking
 * - LOG_COLLECT_DISPATCHER_METRICS - Dispatcher and queue statistics  
 * - LOG_COLLECT_STRUCTURED_METRICS - Structured logging drop statistics
 * - LOG_COLLECT_DISPATCHER_MSG_RATE - Sliding window message rate tracking
 * - LOG_COLLECT_ROTATION_METRICS - File rotation statistics
 * - LOG_COLLECT_COMPRESSION_METRICS - Compression thread statistics
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
 *    - metadata_usage: Min/max/avg bytes used in metadata area
 *    - text_usage: Min/max/avg bytes used in text area
 *    - total_usage: Min/max/avg total buffer bytes used
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
 *    - avg_dispatch_time_us: Average sink dispatch time PER BUFFER
 *    - max_dispatch_time_us: Maximum sink dispatch time PER BUFFER
 *    - messages_per_second_1s: Message rate over last 1 second (requires LOG_COLLECT_DISPATCHER_MSG_RATE)
 *    - messages_per_second_10s: Message rate over last 10 seconds (requires LOG_COLLECT_DISPATCHER_MSG_RATE)
 *    - messages_per_second_60s: Message rate over last 60 seconds (requires LOG_COLLECT_DISPATCHER_MSG_RATE)
 *    - avg_batch_size: Average messages per batch
 *    - total_batches: Total batches processed
 *    - min_batch_size: Minimum batch size observed
 *    - max_batch_size: Maximum batch size observed
 *    - min_inflight_time_us: Minimum in-flight time (buffer creation to sink completion)
 *    - avg_inflight_time_us: Average in-flight time (buffer creation to sink completion)
 *    - max_inflight_time_us: Maximum in-flight time (buffer creation to sink completion)
 *    - min_dequeue_time_us: Minimum TOTAL time in dequeue_buffers (includes waiting)
 *    - avg_dequeue_time_us: Average TOTAL time in dequeue_buffers (includes waiting)
 *    - max_dequeue_time_us: Maximum TOTAL time in dequeue_buffers (includes waiting)
 *    - uptime: Time since dispatcher started
 *
 * 3. Structured Logging Statistics (requires LOG_COLLECT_STRUCTURED_METRICS):
 *    - Key Registry (structured_log_key_registry::instance().get_stats()):
 *      - key_count: Number of registered keys
 *      - max_keys: Maximum allowed (MAX_STRUCTURED_KEYS)
 *      - usage_percent: Registry utilization
 *      - estimated_memory_kb: Memory usage estimate
 *
 *    - Metadata Drops (log_buffer_metadata_adapter::get_drop_stats()):
 *      - dropped_count: Metadata entries dropped
 *      - dropped_bytes: Total bytes dropped
 *
 * 4. File Rotation Statistics (requires LOG_COLLECT_ROTATION_METRICS):
 *    - Rotation Metrics (rotation_metrics::instance().get_stats()):
 *      - total_rotations: Total rotation operations performed
 *      - avg_rotation_time_us: Average time per rotation
 *      - total_rotation_time_us: Total time spent rotating
 *      - failed_rotations: Rotations that failed
 *      - enospc_errors: Disk full errors encountered
 *      - enospc_pending_deleted: .pending files deleted for space
 *      - enospc_gz_deleted: .gz files deleted for space
 *      - enospc_raw_deleted: Raw log files deleted for space
 *      - retention_files_deleted: Files deleted by retention policy
 *      - retention_bytes_deleted: Bytes freed by retention
 *      - compress_errors: Compression failures
 *      - compress_successes: Successful compressions
 *      - compress_bytes_saved: Space saved by compression
 *      - compress_time_us: Time spent compressing
 *      - open_errors: File open failures
 *      - write_errors: Write failures
 *      - sync_errors: Sync failures
 *      - cache_size: Current rotation cache size
 *      - cache_memory_kb: Memory used by cache
 *      - compression_queue_overflows: Times compression queue was full
 *
 *    - Compression Statistics (requires LOG_COLLECT_COMPRESSION_METRICS):
 *      file_rotation_service::instance().get_compression_stats():
 *      - files_queued: Total files queued for compression
 *      - files_compressed: Successfully compressed files
 *      - files_cancelled: Files cancelled before/during compression
 *      - queue_overflows: Times queue was full when enqueueing
 *      - current_queue_size: Current compression queue depth
 *      - queue_high_water_mark: Maximum queue size reached
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
 * #ifdef LOG_COLLECT_ROTATION_METRICS
 * // Monitor rotation metrics
 * auto rot_stats = rotation_metrics::instance().get_stats();
 * if (rot_stats.failed_rotations > 0 || rot_stats.enospc_errors > 0) {
 *     std::cerr << "WARNING: Rotation issues - " << rot_stats.failed_rotations 
 *               << " failures, " << rot_stats.enospc_errors << " disk full errors\n";
 * }
 * #endif
 * 
 * #ifdef LOG_COLLECT_COMPRESSION_METRICS
 * // Monitor compression metrics
 * auto comp_stats = file_rotation_service::instance().get_compression_stats();
 * std::cout << "Compression queue: " << comp_stats.current_queue_size 
 *           << "/" << comp_stats.queue_high_water_mark << " (max)\n";
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
#pragma once

#include <cstring>
#include <cstdlib>
#include <memory>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "fmt_config.hpp"       // IWYU pragma: keep
#include "log_types.hpp"        // IWYU pragma: keep
#include "log_site.hpp"         // IWYU pragma: keep
#include "log_module.hpp"       // IWYU pragma: keep
#include "log_buffer.hpp"       // IWYU pragma: keep
#include "log_sink.hpp"         // IWYU pragma: keep
#include "log_formatters.hpp"   // IWYU pragma: keep
#include "log_writers.hpp"      // IWYU pragma: keep
#include "log_sinks.hpp"        // IWYU pragma: keep
#include "log_version.hpp"      // IWYU pragma: keep
#include "log_line.hpp"         // IWYU pragma: keep
#include "log_dispatcher.hpp"   // IWYU pragma: keep
#include "log_file_rotator.hpp" // IWYU pragma: keep

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

/**
 * @brief keep the filename + @p KeepParts parts of the path (KeepParts = 1 e.g. "/dev/swloggy/src/main.cpp" -> "src/main.cpp")
 * 
 * @tparam N Length of the path string
 * @tparam KeepParts Number of path parts to keep (default: 1)
 * @return constexpr const char* 
 */
template <size_t N, int KeepParts = 1> constexpr const char *get_path_suffix(const char (&path)[N])
{
    // Count total separators
    int total_separators = 0;
    for (size_t i = 0; i < N && path[i]; ++i)
    {
        if (path[i] == '/' || path[i] == '\\') { total_separators++; }
    }

    // Calculate how many separators to skip
    int skip_separators = total_separators - KeepParts;
    if (skip_separators <= 0)
    {
        return path; // Return full path if we want to keep everything
    }

    // Find the starting point after skipping
    const char *result = path;
    int skipped        = 0;
    for (size_t i = 0; i < N && path[i]; ++i)
    {
        if (path[i] == '/' || path[i] == '\\')
        {
            skipped++;
            if (skipped == skip_separators)
            {
                result = &path[i + 1];
                break;
            }
        }
    }

    return result;
}

/**
 * @brief Macro to get shortened source file path at compile time
 * 
 * Uses get_path_suffix() to extract just the last directory and filename
 * from __FILE__. This provides consistent, shorter file paths in logs
 * while preserving enough context to identify the source location.
 * 
 * @return Shortened file path (e.g., "src/main.cpp" from "/path/to/project/src/main.cpp")
 */
#define file_source() get_path_suffix(__FILE__)


/**
 * @brief Base macro for log line creation with specified type
 * @internal
 * 
 * This macro contains the common logic for all LOG variants:
 * 1. Compile-time filtering: Logs below GLOBAL_MIN_LOG_LEVEL are completely eliminated
 * 2. Site registration: Each unique LOG() location is registered once via static init
 * 3. Runtime filtering: Checks against the current module's dynamic log level
 * 4. Returns a log_line object of the specified type
 */
#define LOG_BASE(_level, _line_type, _module)                                                                       \
    []()                                                                                                            \
    {                                                                                                               \
        constexpr ::slwoggy::log_level level = ::slwoggy::log_level::_level;                                        \
        if constexpr (level >= ::slwoggy::GLOBAL_MIN_LOG_LEVEL)                                                     \
        {                                                                                                           \
            static struct                                                                                           \
            {                                                                                                       \
                struct registrar                                                                                    \
                {                                                                                                   \
                    ::slwoggy::log_site_descriptor &site_;                                                          \
                    registrar()                                                                                     \
                    : site_(::slwoggy::log_site_registry::register_site(file_source(), __LINE__, level, __func__))  \
                    {                                                                                               \
                    }                                                                                               \
                } r_;                                                                                               \
            } _reg;                                                                                                 \
            if (level >= _module.detail->level.load(std::memory_order_relaxed) && level >= _reg.r_.site_.min_level) \
            {                                                                                                       \
                return ::slwoggy::_line_type(level, _module, file_source(), __LINE__);                              \
            }                                                                                                       \
        }                                                                                                           \
        return ::slwoggy::_line_type(::slwoggy::log_level::nolog, ::slwoggy::g_log_module_info, "", 0);             \
    }()

/**
 * @brief Base macro for log line creation with compile-time module lookup
 * @internal
 * 
 * Similar to LOG_BASE but uses a different module specified at compile time.
 * Module is looked up once and cached in a static variable.
 */
#define LOG_BASE_WITH_MODULE(_level, _line_type, _module_name)                                                          \
    []()                                                                                                                \
    {                                                                                                                   \
        static_assert(std::is_convertible_v<decltype(_module_name), const char *>,                                      \
                      "Module name must be convertible to const char*");                                                \
        constexpr ::slwoggy::log_level level = ::slwoggy::log_level::_level;                                            \
        if constexpr (level >= ::slwoggy::GLOBAL_MIN_LOG_LEVEL)                                                         \
        {                                                                                                               \
            static struct                                                                                               \
            {                                                                                                           \
                ::slwoggy::log_module_info module{::slwoggy::log_module_registry::instance().get_module(_module_name)}; \
                struct registrar                                                                                        \
                {                                                                                                       \
                    ::slwoggy::log_site_descriptor &site_;                                                              \
                    registrar()                                                                                         \
                    : site_(::slwoggy::log_site_registry::register_site(file_source(), __LINE__, level, __func__))      \
                    {                                                                                                   \
                    }                                                                                                   \
                } r_;                                                                                                   \
            } _static_data;                                                                                             \
            if (level >= _static_data.module.detail->level.load(std::memory_order_relaxed) &&                           \
                level >= _static_data.r_.site_.min_level)                                                               \
            {                                                                                                           \
                return ::slwoggy::_line_type(level, _static_data.module, file_source(), __LINE__);                      \
            }                                                                                                           \
        }                                                                                                               \
        /* Fallback when level is filtered at compile time - _static_data doesn't exist */                              \
        return ::slwoggy::_line_type(::slwoggy::log_level::nolog, ::slwoggy::g_log_module_info, "", 0);                 \
    }()

/**
 * @brief Log macros that allow specifying a module name at the call site
 * 
 * These macros use a module specified at compile time, looked up once
 * and cached in a static variable for efficiency.
 *
 * Three variants are available:
 * - LOG_MOD_TEXT: Uses traditional text format (log_line_headered)
 * - LOG_MOD_STRUCT: Uses structured logfmt format (log_line_structured)
 * - LOG_MOD: Alias for LOG_MOD_TEXT (default to text format)
 *
 * @param _level Log level (trace, debug, info, warn, error, critical)
 * @param _module_name Module name as a string literal
 *
 * @code
 * // Log with "network" module settings using text format
 * LOG_MOD_TEXT(info, "network") << "Connection established";
 * LOG_MOD(info, "network") << "Same as above - defaults to text";
 * 
 * // Log with "database" module using structured format
 * LOG_MOD_STRUCT(error, "database")
 *     .add("query_id", 123)
 *     .format("Query failed: {}", error);
 * @endcode
 */
#define LOG_MOD_TEXT(_level, _module_name)   LOG_BASE_WITH_MODULE(_level, log_line_headered, _module_name)
#define LOG_MOD_STRUCT(_level, _module_name) LOG_BASE_WITH_MODULE(_level, log_line_structured, _module_name)
#define LOG_MOD(_level, _module_name)        LOG_BASE_WITH_MODULE(_level, log_line_headered, _module_name)

/**
 * @brief Creates a structured log line (logfmt format) with automatic source location
 * 
 * Outputs logs in logfmt format with automatic metadata fields.
 * Ideal for machine parsing, log aggregation systems, and structured queries.
 *
 * Format: msg="text" key=value key2=value2 ts=... level=... module=... file=... line=...
 *
 * Automatically includes these metadata fields:
 * - ts: Timestamp in nanoseconds since epoch
 * - level: Log level (trace, debug, info, warn, error, fatal)
 * - module: Module name from LOG_MODULE_NAME (defaults to "generic")
 * - file: Source file name
 * - line: Source line number
 *
 * Note: The msg="..." prefix is part of the output format, not a structured field.
 * Adding a custom "msg" field via .add() will result in both appearing in output.
 *
 * @code
 * LOG_STRUCTURED(info) << "User logged in";
 * // Output: msg="User logged in" ts=1234567890 level=info module=auth file=login.cpp line=42
 * 
 * LOG_STRUCTURED(debug).add("user_id", 123)
 *                      .add("latency_ms", 45)
 *                      .format("Request processed in {}ms", 45);
 * // Output: msg="Request processed in 45ms" user_id=123 latency_ms=45 ts=... level=debug ...
 * @endcode
 */
#define LOG_STRUCTURED(_level) LOG_BASE(_level, log_line_structured, ::slwoggy::g_log_module_info)

/**
 * @brief Creates a traditional text log line with header and automatic source location
 * 
 * Outputs logs in traditional human-readable format with aligned columns.
 * Ideal for console output, development, and human inspection.
 *
 * Format: TTTTTTTT.mmm [LEVEL] module     file:line message
 *
 * Where:
 * - TTTTTTTT.mmm: Milliseconds since logger start with microsecond precision
 * - LEVEL: 5-character padded log level
 * - module: 10-character padded module name
 * - file:line: Source location (file width auto-adjusts to longest filename)
 * - message: The log message text
 *
 * @code
 * LOG_TEXT(info) << "Starting application";
 * // Output: 00001234.567 [INFO ] myapp      main.cpp:42 Starting application
 *
 * LOG_TEXT(debug).format("Processing {} items", count);
 * // Output: 00001235.123 [DEBUG] myapp      main.cpp:43 Processing 15 items
 * @endcode
 */
#define LOG_TEXT(_level) LOG_BASE(_level, log_line_headered, ::slwoggy::g_log_module_info)

/**
 * @brief Default log macro - uses the configured default log line type
 * 
 * The behavior depends on LOG_LINE_TYPE definition:
 * - If LOG_LINE_TYPE is defined before including log.hpp, uses that type
 * - Otherwise defaults to log_line_headered (traditional format)
 *
 * @code
 * LOG(info) << "Starting application";
 * LOG(debug).format("Processing {} items", count);
 * LOG(error) << "Failed: " << error_msg << endl;  // endl forces immediate flush
 * @endcode
 */
#define LOG(_level) LOG_TEXT(_level)

#include "log_line_impl.hpp"       // IWYU pragma: keep
#include "log_dispatcher_impl.hpp" // IWYU pragma: keep
#include "log_file_rotator_impl.hpp" // IWYU pragma: keep