/**
 * @file log_types.hpp
 * @brief Core type definitions and constants for the logging system
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <cstdint>
#include <array>
#include <string>
#include <algorithm>
#include <chrono>
#include <fmt/format.h>

namespace slwoggy
{

// Buffer pool constants
inline constexpr size_t BUFFER_POOL_SIZE        = 32 * 1024; // Number of pre-allocated buffers
inline constexpr size_t LOG_SINK_BUFFER_SIZE    = 64 * 1024; // Intermediate buffer for batching buffers for writes
inline constexpr size_t MAX_BATCH_SIZE          = 4 * 1024;  // Number of buffer pulled from the queue during dispatch
inline constexpr size_t MAX_DISPATCH_QUEUE_SIZE = 32 * 1024; // Max size of buffers waiting to be processed by the
                                                             // dispatcher

// Batching configuration constants
inline constexpr auto BATCH_COLLECT_TIMEOUT = std::chrono::microseconds(100); // Max time to collect a batch
inline constexpr auto BATCH_POLL_INTERVAL   = std::chrono::microseconds(10);  // Polling interval when collecting

// Log buffer constants
inline constexpr size_t LOG_BUFFER_SIZE       = 2048;
inline constexpr size_t METADATA_RESERVE      = 256; // Reserve bytes for structured metadata
inline constexpr uint32_t MAX_STRUCTURED_KEYS = 256; // Maximum structured keys
inline constexpr size_t MAX_FORMATTED_SIZE    = 128; // max size of values when allowed in the metadata

// JSON formatting constants
inline constexpr size_t UNICODE_ESCAPE_SIZE   = 7;   // \uXXXX + null terminator
inline constexpr size_t UNICODE_ESCAPE_CHARS  = 6;   // \uXXXX
inline constexpr size_t TIMESTAMP_BUFFER_SIZE = 256; // Buffer for timestamp formatting
inline constexpr size_t LINE_BUFFER_SIZE      = 64;  // Buffer for line number formatting

// Metrics collection configuration
// Define these before including log.hpp to enable metrics collection:
// #define LOG_COLLECT_BUFFER_POOL_METRICS 1 // Enable buffer pool statistics
// #define LOG_COLLECT_DISPATCHER_METRICS  1 // Enable dispatcher statistics
// #define LOG_COLLECT_STRUCTURED_METRICS  1 // Enable structured logging statistics
// #define LOG_COLLECT_DISPATCHER_MSG_RATE 1 // Enable sliding window message rate (requires
// LOG_COLLECT_DISPATCHER_METRICS)

/**
 * @brief Concept for types that can be logged
 * @tparam T The type to check
 */
template <typename T>
concept Loggable = requires(T value, std::string &str) {
    { fmt::format("{}", value) } -> std::convertible_to<std::string>;
};

/**
 * @brief Enumeration of available log levels in ascending order of severity
 */
enum class log_level : int8_t
{
    nolog = -1, ///< No logging
    trace = 0,  ///< Finest-grained information
    debug = 1,  ///< Debugging information
    info  = 2,  ///< General information
    warn  = 3,  ///< Warning messages
    error = 4,  ///< Error messages
    fatal = 5,  ///< Critical errors
};

/**
 * @brief Global minimal log level. Messages below this level will be eliminated at compile time.
 */
inline constexpr log_level GLOBAL_MIN_LOG_LEVEL = log_level::trace;

// Log level names for formatting
inline const char *log_level_names[] = {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"};

inline const std::array<const char *, 6> log_level_colors = {
    "\033[37m", // trace
    "\033[36m", // debug
    "\033[32m", // info
    "\033[33m", // warn
    "\033[31m", // error
    "\033[35m", // fatal
};

/**
 * @brief Convert string to log_level
 * @param str Level name (case insensitive)
 * @return Corresponding log_level, or log_level::nolog if invalid
 *
 * Recognized values: "trace", "debug", "info", "warn", "error", "fatal", "nolog", "off"
 */
inline log_level log_level_from_string(const char *str)
{
    if (!str) return log_level::nolog;

    // Convert to lowercase for comparison
    std::string lower(str);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "trace") return log_level::trace;
    if (lower == "debug") return log_level::debug;
    if (lower == "info") return log_level::info;
    if (lower == "warn" || lower == "warning") return log_level::warn;
    if (lower == "error") return log_level::error;
    if (lower == "fatal") return log_level::fatal;
    if (lower == "nolog" || lower == "off" || lower == "none") return log_level::nolog;

    return log_level::nolog;
}

/**
 * @brief Convert log_level to string
 * @param level The log level
 * @return String representation of the level
 */
inline const char *string_from_log_level(log_level level)
{
    switch (level)
    {
    case log_level::trace: return "trace";
    case log_level::debug: return "debug";
    case log_level::info: return "info";
    case log_level::warn: return "warn";
    case log_level::error: return "error";
    case log_level::fatal: return "fatal";
    case log_level::nolog: return "nolog";
    default: return "unknown";
    }
}
} // namespace slwoggy

// Platform-specific fast timing utilities
#ifdef __APPLE__
    #include <mach/mach_time.h>

inline std::chrono::steady_clock::time_point log_fast_timestamp()
{
    static struct
    {
        mach_timebase_info_data_t timebase;
        bool initialized = false;
    } info;

    if (!info.initialized)
    {
        mach_timebase_info(&info.timebase);
        info.initialized = true;
    }

    uint64_t mach_time = mach_absolute_time();
    uint64_t nanos     = mach_time * info.timebase.numer / info.timebase.denom;

    auto duration = std::chrono::nanoseconds(nanos);
    return std::chrono::steady_clock::time_point(duration);
}

#elif defined(__linux__)
    #include <time.h>

inline std::chrono::steady_clock::time_point log_fast_timestamp()
{
    struct timespec ts;
    // Use CLOCK_MONOTONIC for microsecond precision
    clock_gettime(CLOCK_MONOTONIC, &ts);

    auto duration = std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
    return std::chrono::steady_clock::time_point(duration);
}

#elif defined(_WIN32)
    #include <windows.h>

inline std::chrono::steady_clock::time_point log_fast_timestamp()
{
    // GetTickCount64 is fast but only millisecond precision
    // For microsecond precision, use QueryPerformanceCounter (slower)
    uint64_t ticks = GetTickCount64();
    auto duration  = std::chrono::milliseconds(ticks);
    return std::chrono::steady_clock::time_point(duration);
}

#else
  // Fallback to standard chrono
inline std::chrono::steady_clock::time_point log_fast_timestamp() { return std::chrono::steady_clock::now(); }
#endif
