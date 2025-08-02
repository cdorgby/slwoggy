#pragma once

/**
 * @file slwoggy.hpp
 * @brief Single-header amalgamation of the slwoggy logging library
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 *
 * This file is an amalgamation of all slwoggy headers including dependencies.
 * To use: simply #include "slwoggy.hpp" in your project.
 *
 * Version: v0.0.1-dirty
 * Generated on: 2025-08-02 08:24:22
 */

// Default to __FILE__ if SOURCE_FILE_NAME is not defined
// This allows the amalgamated header to work without CMake
#ifndef SOURCE_FILE_NAME
#define SOURCE_FILE_NAME __FILE__
#endif
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
// #include "moodycamel/concurrentqueue.h"
// Provides a C++11 implementation of a multi-producer, multi-consumer lock-free queue.
// An overview, including benchmark results, is provided here:
//     http://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c++
// The full design is also described in excruciating detail at:
//    http://moodycamel.com/blog/2014/detailed-design-of-a-lock-free-queue

// Simplified BSD license:
// Copyright (c) 2013-2020, Cameron Desrochers.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// - Redistributions of source code must retain the above copyright notice, this list of
// conditions and the following disclaimer.
// - Redistributions in binary form must reproduce the above copyright notice, this list of
// conditions and the following disclaimer in the documentation and/or other materials
// provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
// TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Also dual-licensed under the Boost Software License (see LICENSE.md)



#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
// Disable -Wconversion warnings (spuriously triggered when Traits::size_t and
// Traits::index_t are set to < 32 bits, causing integer promotion, causing warnings
// upon assigning any computed values)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"

#ifdef MCDBGQ_USE_RELACY
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#endif
#endif

#if defined(_MSC_VER) && (!defined(_HAS_CXX17) || !_HAS_CXX17)
// VS2019 with /W4 warns about constant conditional expressions but unless /std=c++17 or higher
// does not support `if constexpr`, so we have no choice but to simply disable the warning
#pragma warning(push)
#pragma warning(disable: 4127)  // conditional expression is constant
#endif

#if defined(__APPLE__)
#include "TargetConditionals.h"
#endif

#ifdef MCDBGQ_USE_RELACY
#include "relacy/relacy_std.hpp"
#include "relacy_shims.h"
// We only use malloc/free anyway, and the delete macro messes up `= delete` method declarations.
// We'll override the default trait malloc ourselves without a macro.
#undef new
#undef delete
#undef malloc
#undef free
#else
#include <atomic>		// Requires C++11. Sorry VS2010.
#include <cassert>
#endif
#include <cstddef>              // for max_align_t
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <algorithm>
#include <utility>
#include <limits>
#include <climits>		// for CHAR_BIT
#include <array>
#include <thread>		// partly for __WINPTHREADS_VERSION if on MinGW-w64 w/ POSIX threading
#include <mutex>        // used for thread exit synchronization

// Platform-specific definitions of a numeric thread ID type and an invalid value
namespace moodycamel { namespace details {
	template<typename thread_id_t> struct thread_id_converter {
		typedef thread_id_t thread_id_numeric_size_t;
		typedef thread_id_t thread_id_hash_t;
		static thread_id_hash_t prehash(thread_id_t const& x) { return x; }
	};
} }
#if defined(MCDBGQ_USE_RELACY)
namespace moodycamel { namespace details {
	typedef std::uint32_t thread_id_t;
	static const thread_id_t invalid_thread_id  = 0xFFFFFFFFU;
	static const thread_id_t invalid_thread_id2 = 0xFFFFFFFEU;
	static inline thread_id_t thread_id() { return rl::thread_index(); }
} }
#elif defined(_WIN32) || defined(__WINDOWS__) || defined(__WIN32__)
// No sense pulling in windows.h in a header, we'll manually declare the function
// we use and rely on backwards-compatibility for this not to break
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);
namespace moodycamel { namespace details {
	static_assert(sizeof(unsigned long) == sizeof(std::uint32_t), "Expected size of unsigned long to be 32 bits on Windows");
	typedef std::uint32_t thread_id_t;
	static const thread_id_t invalid_thread_id  = 0;			// See http://blogs.msdn.com/b/oldnewthing/archive/2004/02/23/78395.aspx
	static const thread_id_t invalid_thread_id2 = 0xFFFFFFFFU;	// Not technically guaranteed to be invalid, but is never used in practice. Note that all Win32 thread IDs are presently multiples of 4.
	static inline thread_id_t thread_id() { return static_cast<thread_id_t>(::GetCurrentThreadId()); }
} }
#elif defined(__arm__) || defined(_M_ARM) || defined(__aarch64__) || (defined(__APPLE__) && TARGET_OS_IPHONE) || defined(__MVS__) || defined(MOODYCAMEL_NO_THREAD_LOCAL)
namespace moodycamel { namespace details {
	static_assert(sizeof(std::thread::id) == 4 || sizeof(std::thread::id) == 8, "std::thread::id is expected to be either 4 or 8 bytes");
	
	typedef std::thread::id thread_id_t;
	static const thread_id_t invalid_thread_id;         // Default ctor creates invalid ID

	// Note we don't define a invalid_thread_id2 since std::thread::id doesn't have one; it's
	// only used if MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED is defined anyway, which it won't
	// be.
	static inline thread_id_t thread_id() { return std::this_thread::get_id(); }

	template<std::size_t> struct thread_id_size { };
	template<> struct thread_id_size<4> { typedef std::uint32_t numeric_t; };
	template<> struct thread_id_size<8> { typedef std::uint64_t numeric_t; };

	template<> struct thread_id_converter<thread_id_t> {
		typedef thread_id_size<sizeof(thread_id_t)>::numeric_t thread_id_numeric_size_t;
#ifndef __APPLE__
		typedef std::size_t thread_id_hash_t;
#else
		typedef thread_id_numeric_size_t thread_id_hash_t;
#endif

		static thread_id_hash_t prehash(thread_id_t const& x)
		{
#ifndef __APPLE__
			return std::hash<std::thread::id>()(x);
#else
			return *reinterpret_cast<thread_id_hash_t const*>(&x);
#endif
		}
	};
} }
#else
// Use a nice trick from this answer: http://stackoverflow.com/a/8438730/21475
// In order to get a numeric thread ID in a platform-independent way, we use a thread-local
// static variable's address as a thread identifier :-)
#if defined(__GNUC__) || defined(__INTEL_COMPILER)
#define MOODYCAMEL_THREADLOCAL __thread
#elif defined(_MSC_VER)
#define MOODYCAMEL_THREADLOCAL __declspec(thread)
#else
// Assume C++11 compliant compiler
#define MOODYCAMEL_THREADLOCAL thread_local
#endif
namespace moodycamel { namespace details {
	typedef std::uintptr_t thread_id_t;
	static const thread_id_t invalid_thread_id  = 0;		// Address can't be nullptr
	static const thread_id_t invalid_thread_id2 = 1;		// Member accesses off a null pointer are also generally invalid. Plus it's not aligned.
	inline thread_id_t thread_id() { static MOODYCAMEL_THREADLOCAL int x; return reinterpret_cast<thread_id_t>(&x); }
} }
#endif

// Constexpr if
#ifndef MOODYCAMEL_CONSTEXPR_IF
#if (defined(_MSC_VER) && defined(_HAS_CXX17) && _HAS_CXX17) || __cplusplus > 201402L
#define MOODYCAMEL_CONSTEXPR_IF if constexpr
#define MOODYCAMEL_MAYBE_UNUSED [[maybe_unused]]
#else
#define MOODYCAMEL_CONSTEXPR_IF if
#define MOODYCAMEL_MAYBE_UNUSED
#endif
#endif

// Exceptions
#ifndef MOODYCAMEL_EXCEPTIONS_ENABLED
#if (defined(_MSC_VER) && defined(_CPPUNWIND)) || (defined(__GNUC__) && defined(__EXCEPTIONS)) || (!defined(_MSC_VER) && !defined(__GNUC__))
#define MOODYCAMEL_EXCEPTIONS_ENABLED
#endif
#endif
#ifdef MOODYCAMEL_EXCEPTIONS_ENABLED
#define MOODYCAMEL_TRY try
#define MOODYCAMEL_CATCH(...) catch(__VA_ARGS__)
#define MOODYCAMEL_RETHROW throw
#define MOODYCAMEL_THROW(expr) throw (expr)
#else
#define MOODYCAMEL_TRY MOODYCAMEL_CONSTEXPR_IF (true)
#define MOODYCAMEL_CATCH(...) else MOODYCAMEL_CONSTEXPR_IF (false)
#define MOODYCAMEL_RETHROW
#define MOODYCAMEL_THROW(expr)
#endif

#ifndef MOODYCAMEL_NOEXCEPT
#if !defined(MOODYCAMEL_EXCEPTIONS_ENABLED)
#define MOODYCAMEL_NOEXCEPT
#define MOODYCAMEL_NOEXCEPT_CTOR(type, valueType, expr) true
#define MOODYCAMEL_NOEXCEPT_ASSIGN(type, valueType, expr) true
#elif defined(_MSC_VER) && defined(_NOEXCEPT) && _MSC_VER < 1800
// VS2012's std::is_nothrow_[move_]constructible is broken and returns true when it shouldn't :-(
// We have to assume *all* non-trivial constructors may throw on VS2012!
#define MOODYCAMEL_NOEXCEPT _NOEXCEPT
#define MOODYCAMEL_NOEXCEPT_CTOR(type, valueType, expr) (std::is_rvalue_reference<valueType>::value && std::is_move_constructible<type>::value ? std::is_trivially_move_constructible<type>::value : std::is_trivially_copy_constructible<type>::value)
#define MOODYCAMEL_NOEXCEPT_ASSIGN(type, valueType, expr) ((std::is_rvalue_reference<valueType>::value && std::is_move_assignable<type>::value ? std::is_trivially_move_assignable<type>::value || std::is_nothrow_move_assignable<type>::value : std::is_trivially_copy_assignable<type>::value || std::is_nothrow_copy_assignable<type>::value) && MOODYCAMEL_NOEXCEPT_CTOR(type, valueType, expr))
#elif defined(_MSC_VER) && defined(_NOEXCEPT) && _MSC_VER < 1900
#define MOODYCAMEL_NOEXCEPT _NOEXCEPT
#define MOODYCAMEL_NOEXCEPT_CTOR(type, valueType, expr) (std::is_rvalue_reference<valueType>::value && std::is_move_constructible<type>::value ? std::is_trivially_move_constructible<type>::value || std::is_nothrow_move_constructible<type>::value : std::is_trivially_copy_constructible<type>::value || std::is_nothrow_copy_constructible<type>::value)
#define MOODYCAMEL_NOEXCEPT_ASSIGN(type, valueType, expr) ((std::is_rvalue_reference<valueType>::value && std::is_move_assignable<type>::value ? std::is_trivially_move_assignable<type>::value || std::is_nothrow_move_assignable<type>::value : std::is_trivially_copy_assignable<type>::value || std::is_nothrow_copy_assignable<type>::value) && MOODYCAMEL_NOEXCEPT_CTOR(type, valueType, expr))
#else
#define MOODYCAMEL_NOEXCEPT noexcept
#define MOODYCAMEL_NOEXCEPT_CTOR(type, valueType, expr) noexcept(expr)
#define MOODYCAMEL_NOEXCEPT_ASSIGN(type, valueType, expr) noexcept(expr)
#endif
#endif

#ifndef MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
#ifdef MCDBGQ_USE_RELACY
#define MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
#else
// VS2013 doesn't support `thread_local`, and MinGW-w64 w/ POSIX threading has a crippling bug: http://sourceforge.net/p/mingw-w64/bugs/445
// g++ <=4.7 doesn't support thread_local either.
// Finally, iOS/ARM doesn't have support for it either, and g++/ARM allows it to compile but it's unconfirmed to actually work
#if (!defined(_MSC_VER) || _MSC_VER >= 1900) && (!defined(__MINGW32__) && !defined(__MINGW64__) || !defined(__WINPTHREADS_VERSION)) && (!defined(__GNUC__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)) && (!defined(__APPLE__) || !TARGET_OS_IPHONE) && !defined(__arm__) && !defined(_M_ARM) && !defined(__aarch64__) && !defined(__MVS__)
// Assume `thread_local` is fully supported in all other C++11 compilers/platforms
#define MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED    // tentatively enabled for now; years ago several users report having problems with it on
#endif
#endif
#endif

// VS2012 doesn't support deleted functions. 
// In this case, we declare the function normally but don't define it. A link error will be generated if the function is called.
#ifndef MOODYCAMEL_DELETE_FUNCTION
#if defined(_MSC_VER) && _MSC_VER < 1800
#define MOODYCAMEL_DELETE_FUNCTION
#else
#define MOODYCAMEL_DELETE_FUNCTION = delete
#endif
#endif

namespace moodycamel { namespace details {
#ifndef MOODYCAMEL_ALIGNAS
// VS2013 doesn't support alignas or alignof, and align() requires a constant literal
#if defined(_MSC_VER) && _MSC_VER <= 1800
#define MOODYCAMEL_ALIGNAS(alignment) __declspec(align(alignment))
#define MOODYCAMEL_ALIGNOF(obj) __alignof(obj)
#define MOODYCAMEL_ALIGNED_TYPE_LIKE(T, obj) typename details::Vs2013Aligned<std::alignment_of<obj>::value, T>::type
	template<int Align, typename T> struct Vs2013Aligned { };  // default, unsupported alignment
	template<typename T> struct Vs2013Aligned<1, T> { typedef __declspec(align(1)) T type; };
	template<typename T> struct Vs2013Aligned<2, T> { typedef __declspec(align(2)) T type; };
	template<typename T> struct Vs2013Aligned<4, T> { typedef __declspec(align(4)) T type; };
	template<typename T> struct Vs2013Aligned<8, T> { typedef __declspec(align(8)) T type; };
	template<typename T> struct Vs2013Aligned<16, T> { typedef __declspec(align(16)) T type; };
	template<typename T> struct Vs2013Aligned<32, T> { typedef __declspec(align(32)) T type; };
	template<typename T> struct Vs2013Aligned<64, T> { typedef __declspec(align(64)) T type; };
	template<typename T> struct Vs2013Aligned<128, T> { typedef __declspec(align(128)) T type; };
	template<typename T> struct Vs2013Aligned<256, T> { typedef __declspec(align(256)) T type; };
#else
	template<typename T> struct identity { typedef T type; };
#define MOODYCAMEL_ALIGNAS(alignment) alignas(alignment)
#define MOODYCAMEL_ALIGNOF(obj) alignof(obj)
#define MOODYCAMEL_ALIGNED_TYPE_LIKE(T, obj) alignas(alignof(obj)) typename details::identity<T>::type
#endif
#endif
} }


// TSAN can false report races in lock-free code.  To enable TSAN to be used from projects that use this one,
// we can apply per-function compile-time suppression.
// See https://clang.llvm.org/docs/ThreadSanitizer.html#has-feature-thread-sanitizer
#define MOODYCAMEL_NO_TSAN
#if defined(__has_feature)
 #if __has_feature(thread_sanitizer)
  #undef MOODYCAMEL_NO_TSAN
  #define MOODYCAMEL_NO_TSAN __attribute__((no_sanitize("thread")))
 #endif // TSAN
#endif // TSAN

// Compiler-specific likely/unlikely hints
namespace moodycamel { namespace details {
#if defined(__GNUC__)
	static inline bool (likely)(bool x) { return __builtin_expect((x), true); }
	static inline bool (unlikely)(bool x) { return __builtin_expect((x), false); }
#else
	static inline bool (likely)(bool x) { return x; }
	static inline bool (unlikely)(bool x) { return x; }
#endif
} }

#ifdef MOODYCAMEL_QUEUE_INTERNAL_DEBUG
#include "internal/concurrentqueue_internal_debug.h"
#endif

namespace moodycamel {
namespace details {
	template<typename T>
	struct const_numeric_max {
		static_assert(std::is_integral<T>::value, "const_numeric_max can only be used with integers");
		static const T value = std::numeric_limits<T>::is_signed
			? (static_cast<T>(1) << (sizeof(T) * CHAR_BIT - 1)) - static_cast<T>(1)
			: static_cast<T>(-1);
	};

#if defined(__GLIBCXX__)
	typedef ::max_align_t std_max_align_t;      // libstdc++ forgot to add it to std:: for a while
#else
	typedef std::max_align_t std_max_align_t;   // Others (e.g. MSVC) insist it can *only* be accessed via std::
#endif

	// Some platforms have incorrectly set max_align_t to a type with <8 bytes alignment even while supporting
	// 8-byte aligned scalar values (*cough* 32-bit iOS). Work around this with our own union. See issue #64.
	typedef union {
		std_max_align_t x;
		long long y;
		void* z;
	} max_align_t;
}

// Default traits for the ConcurrentQueue. To change some of the
// traits without re-implementing all of them, inherit from this
// struct and shadow the declarations you wish to be different;
// since the traits are used as a template type parameter, the
// shadowed declarations will be used where defined, and the defaults
// otherwise.
struct ConcurrentQueueDefaultTraits
{
	// General-purpose size type. std::size_t is strongly recommended.
	typedef std::size_t size_t;
	
	// The type used for the enqueue and dequeue indices. Must be at least as
	// large as size_t. Should be significantly larger than the number of elements
	// you expect to hold at once, especially if you have a high turnover rate;
	// for example, on 32-bit x86, if you expect to have over a hundred million
	// elements or pump several million elements through your queue in a very
	// short space of time, using a 32-bit type *may* trigger a race condition.
	// A 64-bit int type is recommended in that case, and in practice will
	// prevent a race condition no matter the usage of the queue. Note that
	// whether the queue is lock-free with a 64-int type depends on the whether
	// std::atomic<std::uint64_t> is lock-free, which is platform-specific.
	typedef std::size_t index_t;
	
	// Internally, all elements are enqueued and dequeued from multi-element
	// blocks; this is the smallest controllable unit. If you expect few elements
	// but many producers, a smaller block size should be favoured. For few producers
	// and/or many elements, a larger block size is preferred. A sane default
	// is provided. Must be a power of 2.
	static const size_t BLOCK_SIZE = 32;
	
	// For explicit producers (i.e. when using a producer token), the block is
	// checked for being empty by iterating through a list of flags, one per element.
	// For large block sizes, this is too inefficient, and switching to an atomic
	// counter-based approach is faster. The switch is made for block sizes strictly
	// larger than this threshold.
	static const size_t EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD = 32;
	
	// How many full blocks can be expected for a single explicit producer? This should
	// reflect that number's maximum for optimal performance. Must be a power of 2.
	static const size_t EXPLICIT_INITIAL_INDEX_SIZE = 32;
	
	// How many full blocks can be expected for a single implicit producer? This should
	// reflect that number's maximum for optimal performance. Must be a power of 2.
	static const size_t IMPLICIT_INITIAL_INDEX_SIZE = 32;
	
	// The initial size of the hash table mapping thread IDs to implicit producers.
	// Note that the hash is resized every time it becomes half full.
	// Must be a power of two, and either 0 or at least 1. If 0, implicit production
	// (using the enqueue methods without an explicit producer token) is disabled.
	static const size_t INITIAL_IMPLICIT_PRODUCER_HASH_SIZE = 32;
	
	// Controls the number of items that an explicit consumer (i.e. one with a token)
	// must consume before it causes all consumers to rotate and move on to the next
	// internal queue.
	static const std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = 256;
	
	// The maximum number of elements (inclusive) that can be enqueued to a sub-queue.
	// Enqueue operations that would cause this limit to be surpassed will fail. Note
	// that this limit is enforced at the block level (for performance reasons), i.e.
	// it's rounded up to the nearest block size.
	static const size_t MAX_SUBQUEUE_SIZE = details::const_numeric_max<size_t>::value;

	// The number of times to spin before sleeping when waiting on a semaphore.
	// Recommended values are on the order of 1000-10000 unless the number of
	// consumer threads exceeds the number of idle cores (in which case try 0-100).
	// Only affects instances of the BlockingConcurrentQueue.
	static const int MAX_SEMA_SPINS = 10000;

	// Whether to recycle dynamically-allocated blocks into an internal free list or
	// not. If false, only pre-allocated blocks (controlled by the constructor
	// arguments) will be recycled, and all others will be `free`d back to the heap.
	// Note that blocks consumed by explicit producers are only freed on destruction
	// of the queue (not following destruction of the token) regardless of this trait.
	static const bool RECYCLE_ALLOCATED_BLOCKS = false;

	
#ifndef MCDBGQ_USE_RELACY
	// Memory allocation can be customized if needed.
	// malloc should return nullptr on failure, and handle alignment like std::malloc.
#if defined(malloc) || defined(free)
	// Gah, this is 2015, stop defining macros that break standard code already!
	// Work around malloc/free being special macros:
	static inline void* WORKAROUND_malloc(size_t size) { return malloc(size); }
	static inline void WORKAROUND_free(void* ptr) { return free(ptr); }
	static inline void* (malloc)(size_t size) { return WORKAROUND_malloc(size); }
	static inline void (free)(void* ptr) { return WORKAROUND_free(ptr); }
#else
	static inline void* malloc(size_t size) { return std::malloc(size); }
	static inline void free(void* ptr) { return std::free(ptr); }
#endif
#else
	// Debug versions when running under the Relacy race detector (ignore
	// these in user code)
	static inline void* malloc(size_t size) { return rl::rl_malloc(size, $); }
	static inline void free(void* ptr) { return rl::rl_free(ptr, $); }
#endif
};


// When producing or consuming many elements, the most efficient way is to:
//    1) Use one of the bulk-operation methods of the queue with a token
//    2) Failing that, use the bulk-operation methods without a token
//    3) Failing that, create a token and use that with the single-item methods
//    4) Failing that, use the single-parameter methods of the queue
// Having said that, don't create tokens willy-nilly -- ideally there should be
// a maximum of one token per thread (of each kind).
struct ProducerToken;
struct ConsumerToken;

template<typename T, typename Traits> class ConcurrentQueue;
template<typename T, typename Traits> class BlockingConcurrentQueue;
class ConcurrentQueueTests;


namespace details
{
	struct ConcurrentQueueProducerTypelessBase
	{
		ConcurrentQueueProducerTypelessBase* next;
		std::atomic<bool> inactive;
		ProducerToken* token;
		
		ConcurrentQueueProducerTypelessBase()
			: next(nullptr), inactive(false), token(nullptr)
		{
		}
	};
	
	template<bool use32> struct _hash_32_or_64 {
		static inline std::uint32_t hash(std::uint32_t h)
		{
			// MurmurHash3 finalizer -- see https://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.cpp
			// Since the thread ID is already unique, all we really want to do is propagate that
			// uniqueness evenly across all the bits, so that we can use a subset of the bits while
			// reducing collisions significantly
			h ^= h >> 16;
			h *= 0x85ebca6b;
			h ^= h >> 13;
			h *= 0xc2b2ae35;
			return h ^ (h >> 16);
		}
	};
	template<> struct _hash_32_or_64<1> {
		static inline std::uint64_t hash(std::uint64_t h)
		{
			h ^= h >> 33;
			h *= 0xff51afd7ed558ccd;
			h ^= h >> 33;
			h *= 0xc4ceb9fe1a85ec53;
			return h ^ (h >> 33);
		}
	};
	template<std::size_t size> struct hash_32_or_64 : public _hash_32_or_64<(size > 4)> {  };
	
	static inline size_t hash_thread_id(thread_id_t id)
	{
		static_assert(sizeof(thread_id_t) <= 8, "Expected a platform where thread IDs are at most 64-bit values");
		return static_cast<size_t>(hash_32_or_64<sizeof(thread_id_converter<thread_id_t>::thread_id_hash_t)>::hash(
			thread_id_converter<thread_id_t>::prehash(id)));
	}
	
	template<typename T>
	static inline bool circular_less_than(T a, T b)
	{
		static_assert(std::is_integral<T>::value && !std::numeric_limits<T>::is_signed, "circular_less_than is intended to be used only with unsigned integer types");
		return static_cast<T>(a - b) > static_cast<T>(static_cast<T>(1) << (static_cast<T>(sizeof(T) * CHAR_BIT - 1)));
		// Note: extra parens around rhs of operator<< is MSVC bug: https://developercommunity2.visualstudio.com/t/C4554-triggers-when-both-lhs-and-rhs-is/10034931
		//       silencing the bug requires #pragma warning(disable: 4554) around the calling code and has no effect when done here.
	}
	
	template<typename U>
	static inline char* align_for(char* ptr)
	{
		const std::size_t alignment = std::alignment_of<U>::value;
		return ptr + (alignment - (reinterpret_cast<std::uintptr_t>(ptr) % alignment)) % alignment;
	}

	template<typename T>
	static inline T ceil_to_pow_2(T x)
	{
		static_assert(std::is_integral<T>::value && !std::numeric_limits<T>::is_signed, "ceil_to_pow_2 is intended to be used only with unsigned integer types");

		// Adapted from http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
		--x;
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		for (std::size_t i = 1; i < sizeof(T); i <<= 1) {
			x |= x >> (i << 3);
		}
		++x;
		return x;
	}
	
	template<typename T>
	static inline void swap_relaxed(std::atomic<T>& left, std::atomic<T>& right)
	{
		T temp = left.load(std::memory_order_relaxed);
		left.store(right.load(std::memory_order_relaxed), std::memory_order_relaxed);
		right.store(temp, std::memory_order_relaxed);
	}
	
	template<typename T>
	static inline T const& nomove(T const& x)
	{
		return x;
	}
	
	template<bool Enable>
	struct nomove_if
	{
		template<typename T>
		static inline T const& eval(T const& x)
		{
			return x;
		}
	};
	
	template<>
	struct nomove_if<false>
	{
		template<typename U>
		static inline auto eval(U&& x)
			-> decltype(std::forward<U>(x))
		{
			return std::forward<U>(x);
		}
	};
	
	template<typename It>
	static inline auto deref_noexcept(It& it) MOODYCAMEL_NOEXCEPT -> decltype(*it)
	{
		return *it;
	}
	
#if defined(__clang__) || !defined(__GNUC__) || __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
	template<typename T> struct is_trivially_destructible : std::is_trivially_destructible<T> { };
#else
	template<typename T> struct is_trivially_destructible : std::has_trivial_destructor<T> { };
#endif
	
#ifdef MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
#ifdef MCDBGQ_USE_RELACY
	typedef RelacyThreadExitListener ThreadExitListener;
	typedef RelacyThreadExitNotifier ThreadExitNotifier;
#else
	class ThreadExitNotifier;

	struct ThreadExitListener
	{
		typedef void (*callback_t)(void*);
		callback_t callback;
		void* userData;
		
		ThreadExitListener* next;		// reserved for use by the ThreadExitNotifier
		ThreadExitNotifier* chain;		// reserved for use by the ThreadExitNotifier
	};

	class ThreadExitNotifier
	{
	public:
		static void subscribe(ThreadExitListener* listener)
		{
			auto& tlsInst = instance();
			std::lock_guard<std::mutex> guard(mutex());
			listener->next = tlsInst.tail;
			listener->chain = &tlsInst;
			tlsInst.tail = listener;
		}
		
		static void unsubscribe(ThreadExitListener* listener)
		{
			std::lock_guard<std::mutex> guard(mutex());
			if (!listener->chain) {
				return;  // race with ~ThreadExitNotifier
			}
			auto& tlsInst = *listener->chain;
			listener->chain = nullptr;
			ThreadExitListener** prev = &tlsInst.tail;
			for (auto ptr = tlsInst.tail; ptr != nullptr; ptr = ptr->next) {
				if (ptr == listener) {
					*prev = ptr->next;
					break;
				}
				prev = &ptr->next;
			}
		}
		
	private:
		ThreadExitNotifier() : tail(nullptr) { }
		ThreadExitNotifier(ThreadExitNotifier const&) MOODYCAMEL_DELETE_FUNCTION;
		ThreadExitNotifier& operator=(ThreadExitNotifier const&) MOODYCAMEL_DELETE_FUNCTION;
		
		~ThreadExitNotifier()
		{
			// This thread is about to exit, let everyone know!
			assert(this == &instance() && "If this assert fails, you likely have a buggy compiler! Change the preprocessor conditions such that MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED is no longer defined.");
			std::lock_guard<std::mutex> guard(mutex());
			for (auto ptr = tail; ptr != nullptr; ptr = ptr->next) {
				ptr->chain = nullptr;
				ptr->callback(ptr->userData);
			}
		}
		
		// Thread-local
		static inline ThreadExitNotifier& instance()
		{
			static thread_local ThreadExitNotifier notifier;
			return notifier;
		}

		static inline std::mutex& mutex()
		{
			// Must be static because the ThreadExitNotifier could be destroyed while unsubscribe is called
			static std::mutex mutex;
			return mutex;
		}
		
	private:
		ThreadExitListener* tail;
	};
#endif
#endif
	
	template<typename T> struct static_is_lock_free_num { enum { value = 0 }; };
	template<> struct static_is_lock_free_num<signed char> { enum { value = ATOMIC_CHAR_LOCK_FREE }; };
	template<> struct static_is_lock_free_num<short> { enum { value = ATOMIC_SHORT_LOCK_FREE }; };
	template<> struct static_is_lock_free_num<int> { enum { value = ATOMIC_INT_LOCK_FREE }; };
	template<> struct static_is_lock_free_num<long> { enum { value = ATOMIC_LONG_LOCK_FREE }; };
	template<> struct static_is_lock_free_num<long long> { enum { value = ATOMIC_LLONG_LOCK_FREE }; };
	template<typename T> struct static_is_lock_free : static_is_lock_free_num<typename std::make_signed<T>::type> {  };
	template<> struct static_is_lock_free<bool> { enum { value = ATOMIC_BOOL_LOCK_FREE }; };
	template<typename U> struct static_is_lock_free<U*> { enum { value = ATOMIC_POINTER_LOCK_FREE }; };
}


struct ProducerToken
{
	template<typename T, typename Traits>
	explicit ProducerToken(ConcurrentQueue<T, Traits>& queue);
	
	template<typename T, typename Traits>
	explicit ProducerToken(BlockingConcurrentQueue<T, Traits>& queue);
	
	ProducerToken(ProducerToken&& other) MOODYCAMEL_NOEXCEPT
		: producer(other.producer)
	{
		other.producer = nullptr;
		if (producer != nullptr) {
			producer->token = this;
		}
	}
	
	inline ProducerToken& operator=(ProducerToken&& other) MOODYCAMEL_NOEXCEPT
	{
		swap(other);
		return *this;
	}
	
	void swap(ProducerToken& other) MOODYCAMEL_NOEXCEPT
	{
		std::swap(producer, other.producer);
		if (producer != nullptr) {
			producer->token = this;
		}
		if (other.producer != nullptr) {
			other.producer->token = &other;
		}
	}
	
	// A token is always valid unless:
	//     1) Memory allocation failed during construction
	//     2) It was moved via the move constructor
	//        (Note: assignment does a swap, leaving both potentially valid)
	//     3) The associated queue was destroyed
	// Note that if valid() returns true, that only indicates
	// that the token is valid for use with a specific queue,
	// but not which one; that's up to the user to track.
	inline bool valid() const { return producer != nullptr; }
	
	~ProducerToken()
	{
		if (producer != nullptr) {
			producer->token = nullptr;
			producer->inactive.store(true, std::memory_order_release);
		}
	}
	
	// Disable copying and assignment
	ProducerToken(ProducerToken const&) MOODYCAMEL_DELETE_FUNCTION;
	ProducerToken& operator=(ProducerToken const&) MOODYCAMEL_DELETE_FUNCTION;
	
private:
	template<typename T, typename Traits> friend class ConcurrentQueue;
	friend class ConcurrentQueueTests;
	
protected:
	details::ConcurrentQueueProducerTypelessBase* producer;
};


struct ConsumerToken
{
	template<typename T, typename Traits>
	explicit ConsumerToken(ConcurrentQueue<T, Traits>& q);
	
	template<typename T, typename Traits>
	explicit ConsumerToken(BlockingConcurrentQueue<T, Traits>& q);
	
	ConsumerToken(ConsumerToken&& other) MOODYCAMEL_NOEXCEPT
		: initialOffset(other.initialOffset), lastKnownGlobalOffset(other.lastKnownGlobalOffset), itemsConsumedFromCurrent(other.itemsConsumedFromCurrent), currentProducer(other.currentProducer), desiredProducer(other.desiredProducer)
	{
	}
	
	inline ConsumerToken& operator=(ConsumerToken&& other) MOODYCAMEL_NOEXCEPT
	{
		swap(other);
		return *this;
	}
	
	void swap(ConsumerToken& other) MOODYCAMEL_NOEXCEPT
	{
		std::swap(initialOffset, other.initialOffset);
		std::swap(lastKnownGlobalOffset, other.lastKnownGlobalOffset);
		std::swap(itemsConsumedFromCurrent, other.itemsConsumedFromCurrent);
		std::swap(currentProducer, other.currentProducer);
		std::swap(desiredProducer, other.desiredProducer);
	}
	
	// Disable copying and assignment
	ConsumerToken(ConsumerToken const&) MOODYCAMEL_DELETE_FUNCTION;
	ConsumerToken& operator=(ConsumerToken const&) MOODYCAMEL_DELETE_FUNCTION;

private:
	template<typename T, typename Traits> friend class ConcurrentQueue;
	friend class ConcurrentQueueTests;
	
private: // but shared with ConcurrentQueue
	std::uint32_t initialOffset;
	std::uint32_t lastKnownGlobalOffset;
	std::uint32_t itemsConsumedFromCurrent;
	details::ConcurrentQueueProducerTypelessBase* currentProducer;
	details::ConcurrentQueueProducerTypelessBase* desiredProducer;
};

// Need to forward-declare this swap because it's in a namespace.
// See http://stackoverflow.com/questions/4492062/why-does-a-c-friend-class-need-a-forward-declaration-only-in-other-namespaces
template<typename T, typename Traits>
inline void swap(typename ConcurrentQueue<T, Traits>::ImplicitProducerKVP& a, typename ConcurrentQueue<T, Traits>::ImplicitProducerKVP& b) MOODYCAMEL_NOEXCEPT;


template<typename T, typename Traits = ConcurrentQueueDefaultTraits>
class ConcurrentQueue
{
public:
	typedef ::moodycamel::ProducerToken producer_token_t;
	typedef ::moodycamel::ConsumerToken consumer_token_t;
	
	typedef typename Traits::index_t index_t;
	typedef typename Traits::size_t size_t;
	
	static const size_t BLOCK_SIZE = static_cast<size_t>(Traits::BLOCK_SIZE);
	static const size_t EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD = static_cast<size_t>(Traits::EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD);
	static const size_t EXPLICIT_INITIAL_INDEX_SIZE = static_cast<size_t>(Traits::EXPLICIT_INITIAL_INDEX_SIZE);
	static const size_t IMPLICIT_INITIAL_INDEX_SIZE = static_cast<size_t>(Traits::IMPLICIT_INITIAL_INDEX_SIZE);
	static const size_t INITIAL_IMPLICIT_PRODUCER_HASH_SIZE = static_cast<size_t>(Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE);
	static const std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = static_cast<std::uint32_t>(Traits::EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE);
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4307)		// + integral constant overflow (that's what the ternary expression is for!)
#pragma warning(disable: 4309)		// static_cast: Truncation of constant value
#endif
	static const size_t MAX_SUBQUEUE_SIZE = (details::const_numeric_max<size_t>::value - static_cast<size_t>(Traits::MAX_SUBQUEUE_SIZE) < BLOCK_SIZE) ? details::const_numeric_max<size_t>::value : ((static_cast<size_t>(Traits::MAX_SUBQUEUE_SIZE) + (BLOCK_SIZE - 1)) / BLOCK_SIZE * BLOCK_SIZE);
#ifdef _MSC_VER
#pragma warning(pop)
#endif

	static_assert(!std::numeric_limits<size_t>::is_signed && std::is_integral<size_t>::value, "Traits::size_t must be an unsigned integral type");
	static_assert(!std::numeric_limits<index_t>::is_signed && std::is_integral<index_t>::value, "Traits::index_t must be an unsigned integral type");
	static_assert(sizeof(index_t) >= sizeof(size_t), "Traits::index_t must be at least as wide as Traits::size_t");
	static_assert((BLOCK_SIZE > 1) && !(BLOCK_SIZE & (BLOCK_SIZE - 1)), "Traits::BLOCK_SIZE must be a power of 2 (and at least 2)");
	static_assert((EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD > 1) && !(EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD & (EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD - 1)), "Traits::EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD must be a power of 2 (and greater than 1)");
	static_assert((EXPLICIT_INITIAL_INDEX_SIZE > 1) && !(EXPLICIT_INITIAL_INDEX_SIZE & (EXPLICIT_INITIAL_INDEX_SIZE - 1)), "Traits::EXPLICIT_INITIAL_INDEX_SIZE must be a power of 2 (and greater than 1)");
	static_assert((IMPLICIT_INITIAL_INDEX_SIZE > 1) && !(IMPLICIT_INITIAL_INDEX_SIZE & (IMPLICIT_INITIAL_INDEX_SIZE - 1)), "Traits::IMPLICIT_INITIAL_INDEX_SIZE must be a power of 2 (and greater than 1)");
	static_assert((INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0) || !(INITIAL_IMPLICIT_PRODUCER_HASH_SIZE & (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE - 1)), "Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE must be a power of 2");
	static_assert(INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0 || INITIAL_IMPLICIT_PRODUCER_HASH_SIZE >= 1, "Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE must be at least 1 (or 0 to disable implicit enqueueing)");

public:
	// Creates a queue with at least `capacity` element slots; note that the
	// actual number of elements that can be inserted without additional memory
	// allocation depends on the number of producers and the block size (e.g. if
	// the block size is equal to `capacity`, only a single block will be allocated
	// up-front, which means only a single producer will be able to enqueue elements
	// without an extra allocation -- blocks aren't shared between producers).
	// This method is not thread safe -- it is up to the user to ensure that the
	// queue is fully constructed before it starts being used by other threads (this
	// includes making the memory effects of construction visible, possibly with a
	// memory barrier).
	explicit ConcurrentQueue(size_t capacity = 32 * BLOCK_SIZE)
		: producerListTail(nullptr),
		producerCount(0),
		initialBlockPoolIndex(0),
		nextExplicitConsumerId(0),
		globalExplicitConsumerOffset(0)
	{
		implicitProducerHashResizeInProgress.clear(std::memory_order_relaxed);
		populate_initial_implicit_producer_hash();
		populate_initial_block_list(capacity / BLOCK_SIZE + ((capacity & (BLOCK_SIZE - 1)) == 0 ? 0 : 1));
		
#ifdef MOODYCAMEL_QUEUE_INTERNAL_DEBUG
		// Track all the producers using a fully-resolved typed list for
		// each kind; this makes it possible to debug them starting from
		// the root queue object (otherwise wacky casts are needed that
		// don't compile in the debugger's expression evaluator).
		explicitProducers.store(nullptr, std::memory_order_relaxed);
		implicitProducers.store(nullptr, std::memory_order_relaxed);
#endif
	}
	
	// Computes the correct amount of pre-allocated blocks for you based
	// on the minimum number of elements you want available at any given
	// time, and the maximum concurrent number of each type of producer.
	ConcurrentQueue(size_t minCapacity, size_t maxExplicitProducers, size_t maxImplicitProducers)
		: producerListTail(nullptr),
		producerCount(0),
		initialBlockPoolIndex(0),
		nextExplicitConsumerId(0),
		globalExplicitConsumerOffset(0)
	{
		implicitProducerHashResizeInProgress.clear(std::memory_order_relaxed);
		populate_initial_implicit_producer_hash();
		size_t blocks = (((minCapacity + BLOCK_SIZE - 1) / BLOCK_SIZE) - 1) * (maxExplicitProducers + 1) + 2 * (maxExplicitProducers + maxImplicitProducers);
		populate_initial_block_list(blocks);
		
#ifdef MOODYCAMEL_QUEUE_INTERNAL_DEBUG
		explicitProducers.store(nullptr, std::memory_order_relaxed);
		implicitProducers.store(nullptr, std::memory_order_relaxed);
#endif
	}
	
	// Note: The queue should not be accessed concurrently while it's
	// being deleted. It's up to the user to synchronize this.
	// This method is not thread safe.
	~ConcurrentQueue()
	{
		// Destroy producers
		auto ptr = producerListTail.load(std::memory_order_relaxed);
		while (ptr != nullptr) {
			auto next = ptr->next_prod();
			if (ptr->token != nullptr) {
				ptr->token->producer = nullptr;
			}
			destroy(ptr);
			ptr = next;
		}
		
		// Destroy implicit producer hash tables
		MOODYCAMEL_CONSTEXPR_IF (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE != 0) {
			auto hash = implicitProducerHash.load(std::memory_order_relaxed);
			while (hash != nullptr) {
				auto prev = hash->prev;
				if (prev != nullptr) {		// The last hash is part of this object and was not allocated dynamically
					for (size_t i = 0; i != hash->capacity; ++i) {
						hash->entries[i].~ImplicitProducerKVP();
					}
					hash->~ImplicitProducerHash();
					(Traits::free)(hash);
				}
				hash = prev;
			}
		}
		
		// Destroy global free list
		auto block = freeList.head_unsafe();
		while (block != nullptr) {
			auto next = block->freeListNext.load(std::memory_order_relaxed);
			if (block->dynamicallyAllocated) {
				destroy(block);
			}
			block = next;
		}
		
		// Destroy initial free list
		destroy_array(initialBlockPool, initialBlockPoolSize);
	}

	// Disable copying and copy assignment
	ConcurrentQueue(ConcurrentQueue const&) MOODYCAMEL_DELETE_FUNCTION;
	ConcurrentQueue& operator=(ConcurrentQueue const&) MOODYCAMEL_DELETE_FUNCTION;
	
	// Moving is supported, but note that it is *not* a thread-safe operation.
	// Nobody can use the queue while it's being moved, and the memory effects
	// of that move must be propagated to other threads before they can use it.
	// Note: When a queue is moved, its tokens are still valid but can only be
	// used with the destination queue (i.e. semantically they are moved along
	// with the queue itself).
	ConcurrentQueue(ConcurrentQueue&& other) MOODYCAMEL_NOEXCEPT
		: producerListTail(other.producerListTail.load(std::memory_order_relaxed)),
		producerCount(other.producerCount.load(std::memory_order_relaxed)),
		initialBlockPoolIndex(other.initialBlockPoolIndex.load(std::memory_order_relaxed)),
		initialBlockPool(other.initialBlockPool),
		initialBlockPoolSize(other.initialBlockPoolSize),
		freeList(std::move(other.freeList)),
		nextExplicitConsumerId(other.nextExplicitConsumerId.load(std::memory_order_relaxed)),
		globalExplicitConsumerOffset(other.globalExplicitConsumerOffset.load(std::memory_order_relaxed))
	{
		// Move the other one into this, and leave the other one as an empty queue
		implicitProducerHashResizeInProgress.clear(std::memory_order_relaxed);
		populate_initial_implicit_producer_hash();
		swap_implicit_producer_hashes(other);
		
		other.producerListTail.store(nullptr, std::memory_order_relaxed);
		other.producerCount.store(0, std::memory_order_relaxed);
		other.nextExplicitConsumerId.store(0, std::memory_order_relaxed);
		other.globalExplicitConsumerOffset.store(0, std::memory_order_relaxed);
		
#ifdef MOODYCAMEL_QUEUE_INTERNAL_DEBUG
		explicitProducers.store(other.explicitProducers.load(std::memory_order_relaxed), std::memory_order_relaxed);
		other.explicitProducers.store(nullptr, std::memory_order_relaxed);
		implicitProducers.store(other.implicitProducers.load(std::memory_order_relaxed), std::memory_order_relaxed);
		other.implicitProducers.store(nullptr, std::memory_order_relaxed);
#endif
		
		other.initialBlockPoolIndex.store(0, std::memory_order_relaxed);
		other.initialBlockPoolSize = 0;
		other.initialBlockPool = nullptr;
		
		reown_producers();
	}
	
	inline ConcurrentQueue& operator=(ConcurrentQueue&& other) MOODYCAMEL_NOEXCEPT
	{
		return swap_internal(other);
	}
	
	// Swaps this queue's state with the other's. Not thread-safe.
	// Swapping two queues does not invalidate their tokens, however
	// the tokens that were created for one queue must be used with
	// only the swapped queue (i.e. the tokens are tied to the
	// queue's movable state, not the object itself).
	inline void swap(ConcurrentQueue& other) MOODYCAMEL_NOEXCEPT
	{
		swap_internal(other);
	}
	
private:
	ConcurrentQueue& swap_internal(ConcurrentQueue& other)
	{
		if (this == &other) {
			return *this;
		}
		
		details::swap_relaxed(producerListTail, other.producerListTail);
		details::swap_relaxed(producerCount, other.producerCount);
		details::swap_relaxed(initialBlockPoolIndex, other.initialBlockPoolIndex);
		std::swap(initialBlockPool, other.initialBlockPool);
		std::swap(initialBlockPoolSize, other.initialBlockPoolSize);
		freeList.swap(other.freeList);
		details::swap_relaxed(nextExplicitConsumerId, other.nextExplicitConsumerId);
		details::swap_relaxed(globalExplicitConsumerOffset, other.globalExplicitConsumerOffset);
		
		swap_implicit_producer_hashes(other);
		
		reown_producers();
		other.reown_producers();
		
#ifdef MOODYCAMEL_QUEUE_INTERNAL_DEBUG
		details::swap_relaxed(explicitProducers, other.explicitProducers);
		details::swap_relaxed(implicitProducers, other.implicitProducers);
#endif
		
		return *this;
	}
	
public:
	// Enqueues a single item (by copying it).
	// Allocates memory if required. Only fails if memory allocation fails (or implicit
	// production is disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE is 0,
	// or Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Thread-safe.
	inline bool enqueue(T const& item)
	{
		MOODYCAMEL_CONSTEXPR_IF (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0) return false;
		else return inner_enqueue<CanAlloc>(item);
	}
	
	// Enqueues a single item (by moving it, if possible).
	// Allocates memory if required. Only fails if memory allocation fails (or implicit
	// production is disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE is 0,
	// or Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Thread-safe.
	inline bool enqueue(T&& item)
	{
		MOODYCAMEL_CONSTEXPR_IF (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0) return false;
		else return inner_enqueue<CanAlloc>(std::move(item));
	}
	
	// Enqueues a single item (by copying it) using an explicit producer token.
	// Allocates memory if required. Only fails if memory allocation fails (or
	// Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Thread-safe.
	inline bool enqueue(producer_token_t const& token, T const& item)
	{
		return inner_enqueue<CanAlloc>(token, item);
	}
	
	// Enqueues a single item (by moving it, if possible) using an explicit producer token.
	// Allocates memory if required. Only fails if memory allocation fails (or
	// Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Thread-safe.
	inline bool enqueue(producer_token_t const& token, T&& item)
	{
		return inner_enqueue<CanAlloc>(token, std::move(item));
	}
	
	// Enqueues several items.
	// Allocates memory if required. Only fails if memory allocation fails (or
	// implicit production is disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE
	// is 0, or Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Note: Use std::make_move_iterator if the elements should be moved instead of copied.
	// Thread-safe.
	template<typename It>
	bool enqueue_bulk(It itemFirst, size_t count)
	{
		MOODYCAMEL_CONSTEXPR_IF (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0) return false;
		else return inner_enqueue_bulk<CanAlloc>(itemFirst, count);
	}
	
	// Enqueues several items using an explicit producer token.
	// Allocates memory if required. Only fails if memory allocation fails
	// (or Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Note: Use std::make_move_iterator if the elements should be moved
	// instead of copied.
	// Thread-safe.
	template<typename It>
	bool enqueue_bulk(producer_token_t const& token, It itemFirst, size_t count)
	{
		return inner_enqueue_bulk<CanAlloc>(token, itemFirst, count);
	}
	
	// Enqueues a single item (by copying it).
	// Does not allocate memory. Fails if not enough room to enqueue (or implicit
	// production is disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE
	// is 0).
	// Thread-safe.
	inline bool try_enqueue(T const& item)
	{
		MOODYCAMEL_CONSTEXPR_IF (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0) return false;
		else return inner_enqueue<CannotAlloc>(item);
	}
	
	// Enqueues a single item (by moving it, if possible).
	// Does not allocate memory (except for one-time implicit producer).
	// Fails if not enough room to enqueue (or implicit production is
	// disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE is 0).
	// Thread-safe.
	inline bool try_enqueue(T&& item)
	{
		MOODYCAMEL_CONSTEXPR_IF (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0) return false;
		else return inner_enqueue<CannotAlloc>(std::move(item));
	}
	
	// Enqueues a single item (by copying it) using an explicit producer token.
	// Does not allocate memory. Fails if not enough room to enqueue.
	// Thread-safe.
	inline bool try_enqueue(producer_token_t const& token, T const& item)
	{
		return inner_enqueue<CannotAlloc>(token, item);
	}
	
	// Enqueues a single item (by moving it, if possible) using an explicit producer token.
	// Does not allocate memory. Fails if not enough room to enqueue.
	// Thread-safe.
	inline bool try_enqueue(producer_token_t const& token, T&& item)
	{
		return inner_enqueue<CannotAlloc>(token, std::move(item));
	}
	
	// Enqueues several items.
	// Does not allocate memory (except for one-time implicit producer).
	// Fails if not enough room to enqueue (or implicit production is
	// disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE is 0).
	// Note: Use std::make_move_iterator if the elements should be moved
	// instead of copied.
	// Thread-safe.
	template<typename It>
	bool try_enqueue_bulk(It itemFirst, size_t count)
	{
		MOODYCAMEL_CONSTEXPR_IF (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0) return false;
		else return inner_enqueue_bulk<CannotAlloc>(itemFirst, count);
	}
	
	// Enqueues several items using an explicit producer token.
	// Does not allocate memory. Fails if not enough room to enqueue.
	// Note: Use std::make_move_iterator if the elements should be moved
	// instead of copied.
	// Thread-safe.
	template<typename It>
	bool try_enqueue_bulk(producer_token_t const& token, It itemFirst, size_t count)
	{
		return inner_enqueue_bulk<CannotAlloc>(token, itemFirst, count);
	}
	
	
	
	// Attempts to dequeue from the queue.
	// Returns false if all producer streams appeared empty at the time they
	// were checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename U>
	bool try_dequeue(U& item)
	{
		// Instead of simply trying each producer in turn (which could cause needless contention on the first
		// producer), we score them heuristically.
		size_t nonEmptyCount = 0;
		ProducerBase* best = nullptr;
		size_t bestSize = 0;
		for (auto ptr = producerListTail.load(std::memory_order_acquire); nonEmptyCount < 3 && ptr != nullptr; ptr = ptr->next_prod()) {
			auto size = ptr->size_approx();
			if (size > 0) {
				if (size > bestSize) {
					bestSize = size;
					best = ptr;
				}
				++nonEmptyCount;
			}
		}
		
		// If there was at least one non-empty queue but it appears empty at the time
		// we try to dequeue from it, we need to make sure every queue's been tried
		if (nonEmptyCount > 0) {
			if ((details::likely)(best->dequeue(item))) {
				return true;
			}
			for (auto ptr = producerListTail.load(std::memory_order_acquire); ptr != nullptr; ptr = ptr->next_prod()) {
				if (ptr != best && ptr->dequeue(item)) {
					return true;
				}
			}
		}
		return false;
	}
	
	// Attempts to dequeue from the queue.
	// Returns false if all producer streams appeared empty at the time they
	// were checked (so, the queue is likely but not guaranteed to be empty).
	// This differs from the try_dequeue(item) method in that this one does
	// not attempt to reduce contention by interleaving the order that producer
	// streams are dequeued from. So, using this method can reduce overall throughput
	// under contention, but will give more predictable results in single-threaded
	// consumer scenarios. This is mostly only useful for internal unit tests.
	// Never allocates. Thread-safe.
	template<typename U>
	bool try_dequeue_non_interleaved(U& item)
	{
		for (auto ptr = producerListTail.load(std::memory_order_acquire); ptr != nullptr; ptr = ptr->next_prod()) {
			if (ptr->dequeue(item)) {
				return true;
			}
		}
		return false;
	}
	
	// Attempts to dequeue from the queue using an explicit consumer token.
	// Returns false if all producer streams appeared empty at the time they
	// were checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename U>
	bool try_dequeue(consumer_token_t& token, U& item)
	{
		// The idea is roughly as follows:
		// Every 256 items from one producer, make everyone rotate (increase the global offset) -> this means the highest efficiency consumer dictates the rotation speed of everyone else, more or less
		// If you see that the global offset has changed, you must reset your consumption counter and move to your designated place
		// If there's no items where you're supposed to be, keep moving until you find a producer with some items
		// If the global offset has not changed but you've run out of items to consume, move over from your current position until you find an producer with something in it
		
		if (token.desiredProducer == nullptr || token.lastKnownGlobalOffset != globalExplicitConsumerOffset.load(std::memory_order_relaxed)) {
			if (!update_current_producer_after_rotation(token)) {
				return false;
			}
		}
		
		// If there was at least one non-empty queue but it appears empty at the time
		// we try to dequeue from it, we need to make sure every queue's been tried
		if (static_cast<ProducerBase*>(token.currentProducer)->dequeue(item)) {
			if (++token.itemsConsumedFromCurrent == EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE) {
				globalExplicitConsumerOffset.fetch_add(1, std::memory_order_relaxed);
			}
			return true;
		}
		
		auto tail = producerListTail.load(std::memory_order_acquire);
		auto ptr = static_cast<ProducerBase*>(token.currentProducer)->next_prod();
		if (ptr == nullptr) {
			ptr = tail;
		}
		while (ptr != static_cast<ProducerBase*>(token.currentProducer)) {
			if (ptr->dequeue(item)) {
				token.currentProducer = ptr;
				token.itemsConsumedFromCurrent = 1;
				return true;
			}
			ptr = ptr->next_prod();
			if (ptr == nullptr) {
				ptr = tail;
			}
		}
		return false;
	}
	
	// Attempts to dequeue several elements from the queue.
	// Returns the number of items actually dequeued.
	// Returns 0 if all producer streams appeared empty at the time they
	// were checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename It>
	size_t try_dequeue_bulk(It itemFirst, size_t max)
	{
		size_t count = 0;
		for (auto ptr = producerListTail.load(std::memory_order_acquire); ptr != nullptr; ptr = ptr->next_prod()) {
			count += ptr->dequeue_bulk(itemFirst, max - count);
			if (count == max) {
				break;
			}
		}
		return count;
	}
	
	// Attempts to dequeue several elements from the queue using an explicit consumer token.
	// Returns the number of items actually dequeued.
	// Returns 0 if all producer streams appeared empty at the time they
	// were checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename It>
	size_t try_dequeue_bulk(consumer_token_t& token, It itemFirst, size_t max)
	{
		if (token.desiredProducer == nullptr || token.lastKnownGlobalOffset != globalExplicitConsumerOffset.load(std::memory_order_relaxed)) {
			if (!update_current_producer_after_rotation(token)) {
				return 0;
			}
		}
		
		size_t count = static_cast<ProducerBase*>(token.currentProducer)->dequeue_bulk(itemFirst, max);
		if (count == max) {
			if ((token.itemsConsumedFromCurrent += static_cast<std::uint32_t>(max)) >= EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE) {
				globalExplicitConsumerOffset.fetch_add(1, std::memory_order_relaxed);
			}
			return max;
		}
		token.itemsConsumedFromCurrent += static_cast<std::uint32_t>(count);
		max -= count;
		
		auto tail = producerListTail.load(std::memory_order_acquire);
		auto ptr = static_cast<ProducerBase*>(token.currentProducer)->next_prod();
		if (ptr == nullptr) {
			ptr = tail;
		}
		while (ptr != static_cast<ProducerBase*>(token.currentProducer)) {
			auto dequeued = ptr->dequeue_bulk(itemFirst, max);
			count += dequeued;
			if (dequeued != 0) {
				token.currentProducer = ptr;
				token.itemsConsumedFromCurrent = static_cast<std::uint32_t>(dequeued);
			}
			if (dequeued == max) {
				break;
			}
			max -= dequeued;
			ptr = ptr->next_prod();
			if (ptr == nullptr) {
				ptr = tail;
			}
		}
		return count;
	}
	
	
	
	// Attempts to dequeue from a specific producer's inner queue.
	// If you happen to know which producer you want to dequeue from, this
	// is significantly faster than using the general-case try_dequeue methods.
	// Returns false if the producer's queue appeared empty at the time it
	// was checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename U>
	inline bool try_dequeue_from_producer(producer_token_t const& producer, U& item)
	{
		return static_cast<ExplicitProducer*>(producer.producer)->dequeue(item);
	}
	
	// Attempts to dequeue several elements from a specific producer's inner queue.
	// Returns the number of items actually dequeued.
	// If you happen to know which producer you want to dequeue from, this
	// is significantly faster than using the general-case try_dequeue methods.
	// Returns 0 if the producer's queue appeared empty at the time it
	// was checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename It>
	inline size_t try_dequeue_bulk_from_producer(producer_token_t const& producer, It itemFirst, size_t max)
	{
		return static_cast<ExplicitProducer*>(producer.producer)->dequeue_bulk(itemFirst, max);
	}
	
	
	// Returns an estimate of the total number of elements currently in the queue. This
	// estimate is only accurate if the queue has completely stabilized before it is called
	// (i.e. all enqueue and dequeue operations have completed and their memory effects are
	// visible on the calling thread, and no further operations start while this method is
	// being called).
	// Thread-safe.
	size_t size_approx() const
	{
		size_t size = 0;
		for (auto ptr = producerListTail.load(std::memory_order_acquire); ptr != nullptr; ptr = ptr->next_prod()) {
			size += ptr->size_approx();
		}
		return size;
	}
	
	
	// Returns true if the underlying atomic variables used by
	// the queue are lock-free (they should be on most platforms).
	// Thread-safe.
	static constexpr bool is_lock_free()
	{
		return
			details::static_is_lock_free<bool>::value == 2 &&
			details::static_is_lock_free<size_t>::value == 2 &&
			details::static_is_lock_free<std::uint32_t>::value == 2 &&
			details::static_is_lock_free<index_t>::value == 2 &&
			details::static_is_lock_free<void*>::value == 2 &&
			details::static_is_lock_free<typename details::thread_id_converter<details::thread_id_t>::thread_id_numeric_size_t>::value == 2;
	}


private:
	friend struct ProducerToken;
	friend struct ConsumerToken;
	struct ExplicitProducer;
	friend struct ExplicitProducer;
	struct ImplicitProducer;
	friend struct ImplicitProducer;
	friend class ConcurrentQueueTests;
		
	enum AllocationMode { CanAlloc, CannotAlloc };
	
	
	///////////////////////////////
	// Queue methods
	///////////////////////////////
	
	template<AllocationMode canAlloc, typename U>
	inline bool inner_enqueue(producer_token_t const& token, U&& element)
	{
		return static_cast<ExplicitProducer*>(token.producer)->ConcurrentQueue::ExplicitProducer::template enqueue<canAlloc>(std::forward<U>(element));
	}
	
	template<AllocationMode canAlloc, typename U>
	inline bool inner_enqueue(U&& element)
	{
		auto producer = get_or_add_implicit_producer();
		return producer == nullptr ? false : producer->ConcurrentQueue::ImplicitProducer::template enqueue<canAlloc>(std::forward<U>(element));
	}
	
	template<AllocationMode canAlloc, typename It>
	inline bool inner_enqueue_bulk(producer_token_t const& token, It itemFirst, size_t count)
	{
		return static_cast<ExplicitProducer*>(token.producer)->ConcurrentQueue::ExplicitProducer::template enqueue_bulk<canAlloc>(itemFirst, count);
	}
	
	template<AllocationMode canAlloc, typename It>
	inline bool inner_enqueue_bulk(It itemFirst, size_t count)
	{
		auto producer = get_or_add_implicit_producer();
		return producer == nullptr ? false : producer->ConcurrentQueue::ImplicitProducer::template enqueue_bulk<canAlloc>(itemFirst, count);
	}
	
	inline bool update_current_producer_after_rotation(consumer_token_t& token)
	{
		// Ah, there's been a rotation, figure out where we should be!
		auto tail = producerListTail.load(std::memory_order_acquire);
		if (token.desiredProducer == nullptr && tail == nullptr) {
			return false;
		}
		auto prodCount = producerCount.load(std::memory_order_relaxed);
		auto globalOffset = globalExplicitConsumerOffset.load(std::memory_order_relaxed);
		if ((details::unlikely)(token.desiredProducer == nullptr)) {
			// Aha, first time we're dequeueing anything.
			// Figure out our local position
			// Note: offset is from start, not end, but we're traversing from end -- subtract from count first
			std::uint32_t offset = prodCount - 1 - (token.initialOffset % prodCount);
			token.desiredProducer = tail;
			for (std::uint32_t i = 0; i != offset; ++i) {
				token.desiredProducer = static_cast<ProducerBase*>(token.desiredProducer)->next_prod();
				if (token.desiredProducer == nullptr) {
					token.desiredProducer = tail;
				}
			}
		}
		
		std::uint32_t delta = globalOffset - token.lastKnownGlobalOffset;
		if (delta >= prodCount) {
			delta = delta % prodCount;
		}
		for (std::uint32_t i = 0; i != delta; ++i) {
			token.desiredProducer = static_cast<ProducerBase*>(token.desiredProducer)->next_prod();
			if (token.desiredProducer == nullptr) {
				token.desiredProducer = tail;
			}
		}
		
		token.lastKnownGlobalOffset = globalOffset;
		token.currentProducer = token.desiredProducer;
		token.itemsConsumedFromCurrent = 0;
		return true;
	}
	
	
	///////////////////////////
	// Free list
	///////////////////////////
	
	template <typename N>
	struct FreeListNode
	{
		FreeListNode() : freeListRefs(0), freeListNext(nullptr) { }
		
		std::atomic<std::uint32_t> freeListRefs;
		std::atomic<N*> freeListNext;
	};
	
	// A simple CAS-based lock-free free list. Not the fastest thing in the world under heavy contention, but
	// simple and correct (assuming nodes are never freed until after the free list is destroyed), and fairly
	// speedy under low contention.
	template<typename N>		// N must inherit FreeListNode or have the same fields (and initialization of them)
	struct FreeList
	{
		FreeList() : freeListHead(nullptr) { }
		FreeList(FreeList&& other) : freeListHead(other.freeListHead.load(std::memory_order_relaxed)) { other.freeListHead.store(nullptr, std::memory_order_relaxed); }
		void swap(FreeList& other) { details::swap_relaxed(freeListHead, other.freeListHead); }
		
		FreeList(FreeList const&) MOODYCAMEL_DELETE_FUNCTION;
		FreeList& operator=(FreeList const&) MOODYCAMEL_DELETE_FUNCTION;
		
		inline void add(N* node)
		{
#ifdef MCDBGQ_NOLOCKFREE_FREELIST
			debug::DebugLock lock(mutex);
#endif		
			// We know that the should-be-on-freelist bit is 0 at this point, so it's safe to
			// set it using a fetch_add
			if (node->freeListRefs.fetch_add(SHOULD_BE_ON_FREELIST, std::memory_order_acq_rel) == 0) {
				// Oh look! We were the last ones referencing this node, and we know
				// we want to add it to the free list, so let's do it!
		 		add_knowing_refcount_is_zero(node);
			}
		}
		
		inline N* try_get()
		{
#ifdef MCDBGQ_NOLOCKFREE_FREELIST
			debug::DebugLock lock(mutex);
#endif		
			auto head = freeListHead.load(std::memory_order_acquire);
			while (head != nullptr) {
				auto prevHead = head;
				auto refs = head->freeListRefs.load(std::memory_order_relaxed);
				if ((refs & REFS_MASK) == 0 || !head->freeListRefs.compare_exchange_strong(refs, refs + 1, std::memory_order_acquire)) {
					head = freeListHead.load(std::memory_order_acquire);
					continue;
				}
				
				// Good, reference count has been incremented (it wasn't at zero), which means we can read the
				// next and not worry about it changing between now and the time we do the CAS
				auto next = head->freeListNext.load(std::memory_order_relaxed);
				if (freeListHead.compare_exchange_strong(head, next, std::memory_order_acquire, std::memory_order_relaxed)) {
					// Yay, got the node. This means it was on the list, which means shouldBeOnFreeList must be false no
					// matter the refcount (because nobody else knows it's been taken off yet, it can't have been put back on).
					assert((head->freeListRefs.load(std::memory_order_relaxed) & SHOULD_BE_ON_FREELIST) == 0);
					
					// Decrease refcount twice, once for our ref, and once for the list's ref
					head->freeListRefs.fetch_sub(2, std::memory_order_release);
					return head;
				}
				
				// OK, the head must have changed on us, but we still need to decrease the refcount we increased.
				// Note that we don't need to release any memory effects, but we do need to ensure that the reference
				// count decrement happens-after the CAS on the head.
				refs = prevHead->freeListRefs.fetch_sub(1, std::memory_order_acq_rel);
				if (refs == SHOULD_BE_ON_FREELIST + 1) {
					add_knowing_refcount_is_zero(prevHead);
				}
			}
			
			return nullptr;
		}
		
		// Useful for traversing the list when there's no contention (e.g. to destroy remaining nodes)
		N* head_unsafe() const { return freeListHead.load(std::memory_order_relaxed); }
		
	private:
		inline void add_knowing_refcount_is_zero(N* node)
		{
			// Since the refcount is zero, and nobody can increase it once it's zero (except us, and we run
			// only one copy of this method per node at a time, i.e. the single thread case), then we know
			// we can safely change the next pointer of the node; however, once the refcount is back above
			// zero, then other threads could increase it (happens under heavy contention, when the refcount
			// goes to zero in between a load and a refcount increment of a node in try_get, then back up to
			// something non-zero, then the refcount increment is done by the other thread) -- so, if the CAS
			// to add the node to the actual list fails, decrease the refcount and leave the add operation to
			// the next thread who puts the refcount back at zero (which could be us, hence the loop).
			auto head = freeListHead.load(std::memory_order_relaxed);
			while (true) {
				node->freeListNext.store(head, std::memory_order_relaxed);
				node->freeListRefs.store(1, std::memory_order_release);
				if (!freeListHead.compare_exchange_strong(head, node, std::memory_order_release, std::memory_order_relaxed)) {
					// Hmm, the add failed, but we can only try again when the refcount goes back to zero
					if (node->freeListRefs.fetch_add(SHOULD_BE_ON_FREELIST - 1, std::memory_order_acq_rel) == 1) {
						continue;
					}
				}
				return;
			}
		}
		
	private:
		// Implemented like a stack, but where node order doesn't matter (nodes are inserted out of order under contention)
		std::atomic<N*> freeListHead;
	
	static const std::uint32_t REFS_MASK = 0x7FFFFFFF;
	static const std::uint32_t SHOULD_BE_ON_FREELIST = 0x80000000;
		
#ifdef MCDBGQ_NOLOCKFREE_FREELIST
		debug::DebugMutex mutex;
#endif
	};
	
	
	///////////////////////////
	// Block
	///////////////////////////
	
	enum InnerQueueContext { implicit_context = 0, explicit_context = 1 };
	
	struct Block
	{
		Block()
			: next(nullptr), elementsCompletelyDequeued(0), freeListRefs(0), freeListNext(nullptr), dynamicallyAllocated(true)
		{
#ifdef MCDBGQ_TRACKMEM
			owner = nullptr;
#endif
		}
		
		template<InnerQueueContext context>
		inline bool is_empty() const
		{
			MOODYCAMEL_CONSTEXPR_IF (context == explicit_context && BLOCK_SIZE <= EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD) {
				// Check flags
				for (size_t i = 0; i < BLOCK_SIZE; ++i) {
					if (!emptyFlags[i].load(std::memory_order_relaxed)) {
						return false;
					}
				}
				
				// Aha, empty; make sure we have all other memory effects that happened before the empty flags were set
				std::atomic_thread_fence(std::memory_order_acquire);
				return true;
			}
			else {
				// Check counter
				if (elementsCompletelyDequeued.load(std::memory_order_relaxed) == BLOCK_SIZE) {
					std::atomic_thread_fence(std::memory_order_acquire);
					return true;
				}
				assert(elementsCompletelyDequeued.load(std::memory_order_relaxed) <= BLOCK_SIZE);
				return false;
			}
		}
		
		// Returns true if the block is now empty (does not apply in explicit context)
		template<InnerQueueContext context>
		inline bool set_empty(MOODYCAMEL_MAYBE_UNUSED index_t i)
		{
			MOODYCAMEL_CONSTEXPR_IF (context == explicit_context && BLOCK_SIZE <= EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD) {
				// Set flag
				assert(!emptyFlags[BLOCK_SIZE - 1 - static_cast<size_t>(i & static_cast<index_t>(BLOCK_SIZE - 1))].load(std::memory_order_relaxed));
				emptyFlags[BLOCK_SIZE - 1 - static_cast<size_t>(i & static_cast<index_t>(BLOCK_SIZE - 1))].store(true, std::memory_order_release);
				return false;
			}
			else {
				// Increment counter
				auto prevVal = elementsCompletelyDequeued.fetch_add(1, std::memory_order_acq_rel);
				assert(prevVal < BLOCK_SIZE);
				return prevVal == BLOCK_SIZE - 1;
			}
		}
		
		// Sets multiple contiguous item statuses to 'empty' (assumes no wrapping and count > 0).
		// Returns true if the block is now empty (does not apply in explicit context).
		template<InnerQueueContext context>
		inline bool set_many_empty(MOODYCAMEL_MAYBE_UNUSED index_t i, size_t count)
		{
			MOODYCAMEL_CONSTEXPR_IF (context == explicit_context && BLOCK_SIZE <= EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD) {
				// Set flags
				std::atomic_thread_fence(std::memory_order_release);
				i = BLOCK_SIZE - 1 - static_cast<size_t>(i & static_cast<index_t>(BLOCK_SIZE - 1)) - count + 1;
				for (size_t j = 0; j != count; ++j) {
					assert(!emptyFlags[i + j].load(std::memory_order_relaxed));
					emptyFlags[i + j].store(true, std::memory_order_relaxed);
				}
				return false;
			}
			else {
				// Increment counter
				auto prevVal = elementsCompletelyDequeued.fetch_add(count, std::memory_order_acq_rel);
				assert(prevVal + count <= BLOCK_SIZE);
				return prevVal + count == BLOCK_SIZE;
			}
		}
		
		template<InnerQueueContext context>
		inline void set_all_empty()
		{
			MOODYCAMEL_CONSTEXPR_IF (context == explicit_context && BLOCK_SIZE <= EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD) {
				// Set all flags
				for (size_t i = 0; i != BLOCK_SIZE; ++i) {
					emptyFlags[i].store(true, std::memory_order_relaxed);
				}
			}
			else {
				// Reset counter
				elementsCompletelyDequeued.store(BLOCK_SIZE, std::memory_order_relaxed);
			}
		}
		
		template<InnerQueueContext context>
		inline void reset_empty()
		{
			MOODYCAMEL_CONSTEXPR_IF (context == explicit_context && BLOCK_SIZE <= EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD) {
				// Reset flags
				for (size_t i = 0; i != BLOCK_SIZE; ++i) {
					emptyFlags[i].store(false, std::memory_order_relaxed);
				}
			}
			else {
				// Reset counter
				elementsCompletelyDequeued.store(0, std::memory_order_relaxed);
			}
		}
		
		inline T* operator[](index_t idx) MOODYCAMEL_NOEXCEPT { return static_cast<T*>(static_cast<void*>(elements)) + static_cast<size_t>(idx & static_cast<index_t>(BLOCK_SIZE - 1)); }
		inline T const* operator[](index_t idx) const MOODYCAMEL_NOEXCEPT { return static_cast<T const*>(static_cast<void const*>(elements)) + static_cast<size_t>(idx & static_cast<index_t>(BLOCK_SIZE - 1)); }
		
	private:
		static_assert(std::alignment_of<T>::value <= sizeof(T), "The queue does not support types with an alignment greater than their size at this time");
		MOODYCAMEL_ALIGNED_TYPE_LIKE(char[sizeof(T) * BLOCK_SIZE], T) elements;
	public:
		Block* next;
		std::atomic<size_t> elementsCompletelyDequeued;
		std::atomic<bool> emptyFlags[BLOCK_SIZE <= EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD ? BLOCK_SIZE : 1];
	public:
		std::atomic<std::uint32_t> freeListRefs;
		std::atomic<Block*> freeListNext;
		bool dynamicallyAllocated;		// Perhaps a better name for this would be 'isNotPartOfInitialBlockPool'
		
#ifdef MCDBGQ_TRACKMEM
		void* owner;
#endif
	};
	static_assert(std::alignment_of<Block>::value >= std::alignment_of<T>::value, "Internal error: Blocks must be at least as aligned as the type they are wrapping");


#ifdef MCDBGQ_TRACKMEM
public:
	struct MemStats;
private:
#endif
	
	///////////////////////////
	// Producer base
	///////////////////////////
	
	struct ProducerBase : public details::ConcurrentQueueProducerTypelessBase
	{
		ProducerBase(ConcurrentQueue* parent_, bool isExplicit_) :
			tailIndex(0),
			headIndex(0),
			dequeueOptimisticCount(0),
			dequeueOvercommit(0),
			tailBlock(nullptr),
			isExplicit(isExplicit_),
			parent(parent_)
		{
		}
		
		virtual ~ProducerBase() { }
		
		template<typename U>
		inline bool dequeue(U& element)
		{
			if (isExplicit) {
				return static_cast<ExplicitProducer*>(this)->dequeue(element);
			}
			else {
				return static_cast<ImplicitProducer*>(this)->dequeue(element);
			}
		}
		
		template<typename It>
		inline size_t dequeue_bulk(It& itemFirst, size_t max)
		{
			if (isExplicit) {
				return static_cast<ExplicitProducer*>(this)->dequeue_bulk(itemFirst, max);
			}
			else {
				return static_cast<ImplicitProducer*>(this)->dequeue_bulk(itemFirst, max);
			}
		}
		
		inline ProducerBase* next_prod() const { return static_cast<ProducerBase*>(next); }
		
		inline size_t size_approx() const
		{
			auto tail = tailIndex.load(std::memory_order_relaxed);
			auto head = headIndex.load(std::memory_order_relaxed);
			return details::circular_less_than(head, tail) ? static_cast<size_t>(tail - head) : 0;
		}
		
		inline index_t getTail() const { return tailIndex.load(std::memory_order_relaxed); }
	protected:
		std::atomic<index_t> tailIndex;		// Where to enqueue to next
		std::atomic<index_t> headIndex;		// Where to dequeue from next
		
		std::atomic<index_t> dequeueOptimisticCount;
		std::atomic<index_t> dequeueOvercommit;
		
		Block* tailBlock;
		
	public:
		bool isExplicit;
		ConcurrentQueue* parent;
		
	protected:
#ifdef MCDBGQ_TRACKMEM
		friend struct MemStats;
#endif
	};
	
	
	///////////////////////////
	// Explicit queue
	///////////////////////////
		
	struct ExplicitProducer : public ProducerBase
	{
		explicit ExplicitProducer(ConcurrentQueue* parent_) :
			ProducerBase(parent_, true),
			blockIndex(nullptr),
			pr_blockIndexSlotsUsed(0),
			pr_blockIndexSize(EXPLICIT_INITIAL_INDEX_SIZE >> 1),
			pr_blockIndexFront(0),
			pr_blockIndexEntries(nullptr),
			pr_blockIndexRaw(nullptr)
		{
			size_t poolBasedIndexSize = details::ceil_to_pow_2(parent_->initialBlockPoolSize) >> 1;
			if (poolBasedIndexSize > pr_blockIndexSize) {
				pr_blockIndexSize = poolBasedIndexSize;
			}
			
			new_block_index(0);		// This creates an index with double the number of current entries, i.e. EXPLICIT_INITIAL_INDEX_SIZE
		}
		
		~ExplicitProducer()
		{
			// Destruct any elements not yet dequeued.
			// Since we're in the destructor, we can assume all elements
			// are either completely dequeued or completely not (no halfways).
			if (this->tailBlock != nullptr) {		// Note this means there must be a block index too
				// First find the block that's partially dequeued, if any
				Block* halfDequeuedBlock = nullptr;
				if ((this->headIndex.load(std::memory_order_relaxed) & static_cast<index_t>(BLOCK_SIZE - 1)) != 0) {
					// The head's not on a block boundary, meaning a block somewhere is partially dequeued
					// (or the head block is the tail block and was fully dequeued, but the head/tail are still not on a boundary)
					size_t i = (pr_blockIndexFront - pr_blockIndexSlotsUsed) & (pr_blockIndexSize - 1);
					while (details::circular_less_than<index_t>(pr_blockIndexEntries[i].base + BLOCK_SIZE, this->headIndex.load(std::memory_order_relaxed))) {
						i = (i + 1) & (pr_blockIndexSize - 1);
					}
					assert(details::circular_less_than<index_t>(pr_blockIndexEntries[i].base, this->headIndex.load(std::memory_order_relaxed)));
					halfDequeuedBlock = pr_blockIndexEntries[i].block;
				}
				
				// Start at the head block (note the first line in the loop gives us the head from the tail on the first iteration)
				auto block = this->tailBlock;
				do {
					block = block->next;
					if (block->ConcurrentQueue::Block::template is_empty<explicit_context>()) {
						continue;
					}
					
					size_t i = 0;	// Offset into block
					if (block == halfDequeuedBlock) {
						i = static_cast<size_t>(this->headIndex.load(std::memory_order_relaxed) & static_cast<index_t>(BLOCK_SIZE - 1));
					}
					
					// Walk through all the items in the block; if this is the tail block, we need to stop when we reach the tail index
					auto lastValidIndex = (this->tailIndex.load(std::memory_order_relaxed) & static_cast<index_t>(BLOCK_SIZE - 1)) == 0 ? BLOCK_SIZE : static_cast<size_t>(this->tailIndex.load(std::memory_order_relaxed) & static_cast<index_t>(BLOCK_SIZE - 1));
					while (i != BLOCK_SIZE && (block != this->tailBlock || i != lastValidIndex)) {
						(*block)[i++]->~T();
					}
				} while (block != this->tailBlock);
			}
			
			// Destroy all blocks that we own
			if (this->tailBlock != nullptr) {
				auto block = this->tailBlock;
				do {
					auto nextBlock = block->next;
					this->parent->add_block_to_free_list(block);
					block = nextBlock;
				} while (block != this->tailBlock);
			}
			
			// Destroy the block indices
			auto header = static_cast<BlockIndexHeader*>(pr_blockIndexRaw);
			while (header != nullptr) {
				auto prev = static_cast<BlockIndexHeader*>(header->prev);
				header->~BlockIndexHeader();
				(Traits::free)(header);
				header = prev;
			}
		}
		
		template<AllocationMode allocMode, typename U>
		inline bool enqueue(U&& element)
		{
			index_t currentTailIndex = this->tailIndex.load(std::memory_order_relaxed);
			index_t newTailIndex = 1 + currentTailIndex;
			if ((currentTailIndex & static_cast<index_t>(BLOCK_SIZE - 1)) == 0) {
				// We reached the end of a block, start a new one
				auto startBlock = this->tailBlock;
				auto originalBlockIndexSlotsUsed = pr_blockIndexSlotsUsed;
				if (this->tailBlock != nullptr && this->tailBlock->next->ConcurrentQueue::Block::template is_empty<explicit_context>()) {
					// We can re-use the block ahead of us, it's empty!					
					this->tailBlock = this->tailBlock->next;
					this->tailBlock->ConcurrentQueue::Block::template reset_empty<explicit_context>();
					
					// We'll put the block on the block index (guaranteed to be room since we're conceptually removing the
					// last block from it first -- except instead of removing then adding, we can just overwrite).
					// Note that there must be a valid block index here, since even if allocation failed in the ctor,
					// it would have been re-attempted when adding the first block to the queue; since there is such
					// a block, a block index must have been successfully allocated.
				}
				else {
					// Whatever head value we see here is >= the last value we saw here (relatively),
					// and <= its current value. Since we have the most recent tail, the head must be
					// <= to it.
					auto head = this->headIndex.load(std::memory_order_relaxed);
					assert(!details::circular_less_than<index_t>(currentTailIndex, head));
					if (!details::circular_less_than<index_t>(head, currentTailIndex + BLOCK_SIZE)
						|| (MAX_SUBQUEUE_SIZE != details::const_numeric_max<size_t>::value && (MAX_SUBQUEUE_SIZE == 0 || MAX_SUBQUEUE_SIZE - BLOCK_SIZE < currentTailIndex - head))) {
						// We can't enqueue in another block because there's not enough leeway -- the
						// tail could surpass the head by the time the block fills up! (Or we'll exceed
						// the size limit, if the second part of the condition was true.)
						return false;
					}
					// We're going to need a new block; check that the block index has room
					if (pr_blockIndexRaw == nullptr || pr_blockIndexSlotsUsed == pr_blockIndexSize) {
						// Hmm, the circular block index is already full -- we'll need
						// to allocate a new index. Note pr_blockIndexRaw can only be nullptr if
						// the initial allocation failed in the constructor.
						
						MOODYCAMEL_CONSTEXPR_IF (allocMode == CannotAlloc) {
							return false;
						}
						else if (!new_block_index(pr_blockIndexSlotsUsed)) {
							return false;
						}
					}
					
					// Insert a new block in the circular linked list
					auto newBlock = this->parent->ConcurrentQueue::template requisition_block<allocMode>();
					if (newBlock == nullptr) {
						return false;
					}
#ifdef MCDBGQ_TRACKMEM
					newBlock->owner = this;
#endif
					newBlock->ConcurrentQueue::Block::template reset_empty<explicit_context>();
					if (this->tailBlock == nullptr) {
						newBlock->next = newBlock;
					}
					else {
						newBlock->next = this->tailBlock->next;
						this->tailBlock->next = newBlock;
					}
					this->tailBlock = newBlock;
					++pr_blockIndexSlotsUsed;
				}

				MOODYCAMEL_CONSTEXPR_IF (!MOODYCAMEL_NOEXCEPT_CTOR(T, U, new (static_cast<T*>(nullptr)) T(std::forward<U>(element)))) {
					// The constructor may throw. We want the element not to appear in the queue in
					// that case (without corrupting the queue):
					MOODYCAMEL_TRY {
						new ((*this->tailBlock)[currentTailIndex]) T(std::forward<U>(element));
					}
					MOODYCAMEL_CATCH (...) {
						// Revert change to the current block, but leave the new block available
						// for next time
						pr_blockIndexSlotsUsed = originalBlockIndexSlotsUsed;
						this->tailBlock = startBlock == nullptr ? this->tailBlock : startBlock;
						MOODYCAMEL_RETHROW;
					}
				}
				else {
					(void)startBlock;
					(void)originalBlockIndexSlotsUsed;
				}
				
				// Add block to block index
				auto& entry = blockIndex.load(std::memory_order_relaxed)->entries[pr_blockIndexFront];
				entry.base = currentTailIndex;
				entry.block = this->tailBlock;
				blockIndex.load(std::memory_order_relaxed)->front.store(pr_blockIndexFront, std::memory_order_release);
				pr_blockIndexFront = (pr_blockIndexFront + 1) & (pr_blockIndexSize - 1);
				
				MOODYCAMEL_CONSTEXPR_IF (!MOODYCAMEL_NOEXCEPT_CTOR(T, U, new (static_cast<T*>(nullptr)) T(std::forward<U>(element)))) {
					this->tailIndex.store(newTailIndex, std::memory_order_release);
					return true;
				}
			}
			
			// Enqueue
			new ((*this->tailBlock)[currentTailIndex]) T(std::forward<U>(element));
			
			this->tailIndex.store(newTailIndex, std::memory_order_release);
			return true;
		}
		
		template<typename U>
		bool dequeue(U& element)
		{
			auto tail = this->tailIndex.load(std::memory_order_relaxed);
			auto overcommit = this->dequeueOvercommit.load(std::memory_order_relaxed);
			if (details::circular_less_than<index_t>(this->dequeueOptimisticCount.load(std::memory_order_relaxed) - overcommit, tail)) {
				// Might be something to dequeue, let's give it a try
				
				// Note that this if is purely for performance purposes in the common case when the queue is
				// empty and the values are eventually consistent -- we may enter here spuriously.
				
				// Note that whatever the values of overcommit and tail are, they are not going to change (unless we
				// change them) and must be the same value at this point (inside the if) as when the if condition was
				// evaluated.

				// We insert an acquire fence here to synchronize-with the release upon incrementing dequeueOvercommit below.
				// This ensures that whatever the value we got loaded into overcommit, the load of dequeueOptisticCount in
				// the fetch_add below will result in a value at least as recent as that (and therefore at least as large).
				// Note that I believe a compiler (signal) fence here would be sufficient due to the nature of fetch_add (all
				// read-modify-write operations are guaranteed to work on the latest value in the modification order), but
				// unfortunately that can't be shown to be correct using only the C++11 standard.
				// See http://stackoverflow.com/questions/18223161/what-are-the-c11-memory-ordering-guarantees-in-this-corner-case
				std::atomic_thread_fence(std::memory_order_acquire);
				
				// Increment optimistic counter, then check if it went over the boundary
				auto myDequeueCount = this->dequeueOptimisticCount.fetch_add(1, std::memory_order_relaxed);
				
				// Note that since dequeueOvercommit must be <= dequeueOptimisticCount (because dequeueOvercommit is only ever
				// incremented after dequeueOptimisticCount -- this is enforced in the `else` block below), and since we now
				// have a version of dequeueOptimisticCount that is at least as recent as overcommit (due to the release upon
				// incrementing dequeueOvercommit and the acquire above that synchronizes with it), overcommit <= myDequeueCount.
				// However, we can't assert this since both dequeueOptimisticCount and dequeueOvercommit may (independently)
				// overflow; in such a case, though, the logic still holds since the difference between the two is maintained.
				
				// Note that we reload tail here in case it changed; it will be the same value as before or greater, since
				// this load is sequenced after (happens after) the earlier load above. This is supported by read-read
				// coherency (as defined in the standard), explained here: http://en.cppreference.com/w/cpp/atomic/memory_order
				tail = this->tailIndex.load(std::memory_order_acquire);
				if ((details::likely)(details::circular_less_than<index_t>(myDequeueCount - overcommit, tail))) {
					// Guaranteed to be at least one element to dequeue!
					
					// Get the index. Note that since there's guaranteed to be at least one element, this
					// will never exceed tail. We need to do an acquire-release fence here since it's possible
					// that whatever condition got us to this point was for an earlier enqueued element (that
					// we already see the memory effects for), but that by the time we increment somebody else
					// has incremented it, and we need to see the memory effects for *that* element, which is
					// in such a case is necessarily visible on the thread that incremented it in the first
					// place with the more current condition (they must have acquired a tail that is at least
					// as recent).
					auto index = this->headIndex.fetch_add(1, std::memory_order_acq_rel);
					
					
					// Determine which block the element is in
					
					auto localBlockIndex = blockIndex.load(std::memory_order_acquire);
					auto localBlockIndexHead = localBlockIndex->front.load(std::memory_order_acquire);
					
					// We need to be careful here about subtracting and dividing because of index wrap-around.
					// When an index wraps, we need to preserve the sign of the offset when dividing it by the
					// block size (in order to get a correct signed block count offset in all cases):
					auto headBase = localBlockIndex->entries[localBlockIndexHead].base;
					auto blockBaseIndex = index & ~static_cast<index_t>(BLOCK_SIZE - 1);
					auto offset = static_cast<size_t>(static_cast<typename std::make_signed<index_t>::type>(blockBaseIndex - headBase) / static_cast<typename std::make_signed<index_t>::type>(BLOCK_SIZE));
					auto block = localBlockIndex->entries[(localBlockIndexHead + offset) & (localBlockIndex->size - 1)].block;
					
					// Dequeue
					auto& el = *((*block)[index]);
					if (!MOODYCAMEL_NOEXCEPT_ASSIGN(T, T&&, element = std::move(el))) {
						// Make sure the element is still fully dequeued and destroyed even if the assignment
						// throws
						struct Guard {
							Block* block;
							index_t index;
							
							~Guard()
							{
								(*block)[index]->~T();
								block->ConcurrentQueue::Block::template set_empty<explicit_context>(index);
							}
						} guard = { block, index };

						element = std::move(el); // NOLINT
					}
					else {
						element = std::move(el); // NOLINT
						el.~T(); // NOLINT
						block->ConcurrentQueue::Block::template set_empty<explicit_context>(index);
					}
					
					return true;
				}
				else {
					// Wasn't anything to dequeue after all; make the effective dequeue count eventually consistent
					this->dequeueOvercommit.fetch_add(1, std::memory_order_release);		// Release so that the fetch_add on dequeueOptimisticCount is guaranteed to happen before this write
				}
			}
		
			return false;
		}
		
		template<AllocationMode allocMode, typename It>
		bool MOODYCAMEL_NO_TSAN enqueue_bulk(It itemFirst, size_t count)
		{
			// First, we need to make sure we have enough room to enqueue all of the elements;
			// this means pre-allocating blocks and putting them in the block index (but only if
			// all the allocations succeeded).
			index_t startTailIndex = this->tailIndex.load(std::memory_order_relaxed);
			auto startBlock = this->tailBlock;
			auto originalBlockIndexFront = pr_blockIndexFront;
			auto originalBlockIndexSlotsUsed = pr_blockIndexSlotsUsed;
			
			Block* firstAllocatedBlock = nullptr;
			
			// Figure out how many blocks we'll need to allocate, and do so
			size_t blockBaseDiff = ((startTailIndex + count - 1) & ~static_cast<index_t>(BLOCK_SIZE - 1)) - ((startTailIndex - 1) & ~static_cast<index_t>(BLOCK_SIZE - 1));
			index_t currentTailIndex = (startTailIndex - 1) & ~static_cast<index_t>(BLOCK_SIZE - 1);
			if (blockBaseDiff > 0) {
				// Allocate as many blocks as possible from ahead
				while (blockBaseDiff > 0 && this->tailBlock != nullptr && this->tailBlock->next != firstAllocatedBlock && this->tailBlock->next->ConcurrentQueue::Block::template is_empty<explicit_context>()) {
					blockBaseDiff -= static_cast<index_t>(BLOCK_SIZE);
					currentTailIndex += static_cast<index_t>(BLOCK_SIZE);
					
					this->tailBlock = this->tailBlock->next;
					firstAllocatedBlock = firstAllocatedBlock == nullptr ? this->tailBlock : firstAllocatedBlock;
					
					auto& entry = blockIndex.load(std::memory_order_relaxed)->entries[pr_blockIndexFront];
					entry.base = currentTailIndex;
					entry.block = this->tailBlock;
					pr_blockIndexFront = (pr_blockIndexFront + 1) & (pr_blockIndexSize - 1);
				}
				
				// Now allocate as many blocks as necessary from the block pool
				while (blockBaseDiff > 0) {
					blockBaseDiff -= static_cast<index_t>(BLOCK_SIZE);
					currentTailIndex += static_cast<index_t>(BLOCK_SIZE);
					
					auto head = this->headIndex.load(std::memory_order_relaxed);
					assert(!details::circular_less_than<index_t>(currentTailIndex, head));
					bool full = !details::circular_less_than<index_t>(head, currentTailIndex + BLOCK_SIZE) || (MAX_SUBQUEUE_SIZE != details::const_numeric_max<size_t>::value && (MAX_SUBQUEUE_SIZE == 0 || MAX_SUBQUEUE_SIZE - BLOCK_SIZE < currentTailIndex - head));
					if (pr_blockIndexRaw == nullptr || pr_blockIndexSlotsUsed == pr_blockIndexSize || full) {
						MOODYCAMEL_CONSTEXPR_IF (allocMode == CannotAlloc) {
							// Failed to allocate, undo changes (but keep injected blocks)
							pr_blockIndexFront = originalBlockIndexFront;
							pr_blockIndexSlotsUsed = originalBlockIndexSlotsUsed;
							this->tailBlock = startBlock == nullptr ? firstAllocatedBlock : startBlock;
							return false;
						}
						else if (full || !new_block_index(originalBlockIndexSlotsUsed)) {
							// Failed to allocate, undo changes (but keep injected blocks)
							pr_blockIndexFront = originalBlockIndexFront;
							pr_blockIndexSlotsUsed = originalBlockIndexSlotsUsed;
							this->tailBlock = startBlock == nullptr ? firstAllocatedBlock : startBlock;
							return false;
						}
						
						// pr_blockIndexFront is updated inside new_block_index, so we need to
						// update our fallback value too (since we keep the new index even if we
						// later fail)
						originalBlockIndexFront = originalBlockIndexSlotsUsed;
					}
					
					// Insert a new block in the circular linked list
					auto newBlock = this->parent->ConcurrentQueue::template requisition_block<allocMode>();
					if (newBlock == nullptr) {
						pr_blockIndexFront = originalBlockIndexFront;
						pr_blockIndexSlotsUsed = originalBlockIndexSlotsUsed;
						this->tailBlock = startBlock == nullptr ? firstAllocatedBlock : startBlock;
						return false;
					}
					
#ifdef MCDBGQ_TRACKMEM
					newBlock->owner = this;
#endif
					newBlock->ConcurrentQueue::Block::template set_all_empty<explicit_context>();
					if (this->tailBlock == nullptr) {
						newBlock->next = newBlock;
					}
					else {
						newBlock->next = this->tailBlock->next;
						this->tailBlock->next = newBlock;
					}
					this->tailBlock = newBlock;
					firstAllocatedBlock = firstAllocatedBlock == nullptr ? this->tailBlock : firstAllocatedBlock;
					
					++pr_blockIndexSlotsUsed;
					
					auto& entry = blockIndex.load(std::memory_order_relaxed)->entries[pr_blockIndexFront];
					entry.base = currentTailIndex;
					entry.block = this->tailBlock;
					pr_blockIndexFront = (pr_blockIndexFront + 1) & (pr_blockIndexSize - 1);
				}
				
				// Excellent, all allocations succeeded. Reset each block's emptiness before we fill them up, and
				// publish the new block index front
				auto block = firstAllocatedBlock;
				while (true) {
					block->ConcurrentQueue::Block::template reset_empty<explicit_context>();
					if (block == this->tailBlock) {
						break;
					}
					block = block->next;
				}
				
				MOODYCAMEL_CONSTEXPR_IF (MOODYCAMEL_NOEXCEPT_CTOR(T, decltype(*itemFirst), new (static_cast<T*>(nullptr)) T(details::deref_noexcept(itemFirst)))) {
					blockIndex.load(std::memory_order_relaxed)->front.store((pr_blockIndexFront - 1) & (pr_blockIndexSize - 1), std::memory_order_release);
				}
			}
			
			// Enqueue, one block at a time
			index_t newTailIndex = startTailIndex + static_cast<index_t>(count);
			currentTailIndex = startTailIndex;
			auto endBlock = this->tailBlock;
			this->tailBlock = startBlock;
			assert((startTailIndex & static_cast<index_t>(BLOCK_SIZE - 1)) != 0 || firstAllocatedBlock != nullptr || count == 0);
			if ((startTailIndex & static_cast<index_t>(BLOCK_SIZE - 1)) == 0 && firstAllocatedBlock != nullptr) {
				this->tailBlock = firstAllocatedBlock;
			}
			while (true) {
				index_t stopIndex = (currentTailIndex & ~static_cast<index_t>(BLOCK_SIZE - 1)) + static_cast<index_t>(BLOCK_SIZE);
				if (details::circular_less_than<index_t>(newTailIndex, stopIndex)) {
					stopIndex = newTailIndex;
				}
				MOODYCAMEL_CONSTEXPR_IF (MOODYCAMEL_NOEXCEPT_CTOR(T, decltype(*itemFirst), new (static_cast<T*>(nullptr)) T(details::deref_noexcept(itemFirst)))) {
					while (currentTailIndex != stopIndex) {
						new ((*this->tailBlock)[currentTailIndex++]) T(*itemFirst++);
					}
				}
				else {
					MOODYCAMEL_TRY {
						while (currentTailIndex != stopIndex) {
							// Must use copy constructor even if move constructor is available
							// because we may have to revert if there's an exception.
							// Sorry about the horrible templated next line, but it was the only way
							// to disable moving *at compile time*, which is important because a type
							// may only define a (noexcept) move constructor, and so calls to the
							// cctor will not compile, even if they are in an if branch that will never
							// be executed
							new ((*this->tailBlock)[currentTailIndex]) T(details::nomove_if<!MOODYCAMEL_NOEXCEPT_CTOR(T, decltype(*itemFirst), new (static_cast<T*>(nullptr)) T(details::deref_noexcept(itemFirst)))>::eval(*itemFirst));
							++currentTailIndex;
							++itemFirst;
						}
					}
					MOODYCAMEL_CATCH (...) {
						// Oh dear, an exception's been thrown -- destroy the elements that
						// were enqueued so far and revert the entire bulk operation (we'll keep
						// any allocated blocks in our linked list for later, though).
						auto constructedStopIndex = currentTailIndex;
						auto lastBlockEnqueued = this->tailBlock;
						
						pr_blockIndexFront = originalBlockIndexFront;
						pr_blockIndexSlotsUsed = originalBlockIndexSlotsUsed;
						this->tailBlock = startBlock == nullptr ? firstAllocatedBlock : startBlock;
						
						if (!details::is_trivially_destructible<T>::value) {
							auto block = startBlock;
							if ((startTailIndex & static_cast<index_t>(BLOCK_SIZE - 1)) == 0) {
								block = firstAllocatedBlock;
							}
							currentTailIndex = startTailIndex;
							while (true) {
								stopIndex = (currentTailIndex & ~static_cast<index_t>(BLOCK_SIZE - 1)) + static_cast<index_t>(BLOCK_SIZE);
								if (details::circular_less_than<index_t>(constructedStopIndex, stopIndex)) {
									stopIndex = constructedStopIndex;
								}
								while (currentTailIndex != stopIndex) {
									(*block)[currentTailIndex++]->~T();
								}
								if (block == lastBlockEnqueued) {
									break;
								}
								block = block->next;
							}
						}
						MOODYCAMEL_RETHROW;
					}
				}
				
				if (this->tailBlock == endBlock) {
					assert(currentTailIndex == newTailIndex);
					break;
				}
				this->tailBlock = this->tailBlock->next;
			}
			
			MOODYCAMEL_CONSTEXPR_IF (!MOODYCAMEL_NOEXCEPT_CTOR(T, decltype(*itemFirst), new (static_cast<T*>(nullptr)) T(details::deref_noexcept(itemFirst)))) {
				if (firstAllocatedBlock != nullptr)
					blockIndex.load(std::memory_order_relaxed)->front.store((pr_blockIndexFront - 1) & (pr_blockIndexSize - 1), std::memory_order_release);
			}
			
			this->tailIndex.store(newTailIndex, std::memory_order_release);
			return true;
		}
		
		template<typename It>
		size_t dequeue_bulk(It& itemFirst, size_t max)
		{
			auto tail = this->tailIndex.load(std::memory_order_relaxed);
			auto overcommit = this->dequeueOvercommit.load(std::memory_order_relaxed);
			auto desiredCount = static_cast<size_t>(tail - (this->dequeueOptimisticCount.load(std::memory_order_relaxed) - overcommit));
			if (details::circular_less_than<size_t>(0, desiredCount)) {
				desiredCount = desiredCount < max ? desiredCount : max;
				std::atomic_thread_fence(std::memory_order_acquire);
				
				auto myDequeueCount = this->dequeueOptimisticCount.fetch_add(desiredCount, std::memory_order_relaxed);
				
				tail = this->tailIndex.load(std::memory_order_acquire);
				auto actualCount = static_cast<size_t>(tail - (myDequeueCount - overcommit));
				if (details::circular_less_than<size_t>(0, actualCount)) {
					actualCount = desiredCount < actualCount ? desiredCount : actualCount;
					if (actualCount < desiredCount) {
						this->dequeueOvercommit.fetch_add(desiredCount - actualCount, std::memory_order_release);
					}
					
					// Get the first index. Note that since there's guaranteed to be at least actualCount elements, this
					// will never exceed tail.
					auto firstIndex = this->headIndex.fetch_add(actualCount, std::memory_order_acq_rel);
					
					// Determine which block the first element is in
					auto localBlockIndex = blockIndex.load(std::memory_order_acquire);
					auto localBlockIndexHead = localBlockIndex->front.load(std::memory_order_acquire);
					
					auto headBase = localBlockIndex->entries[localBlockIndexHead].base;
					auto firstBlockBaseIndex = firstIndex & ~static_cast<index_t>(BLOCK_SIZE - 1);
					auto offset = static_cast<size_t>(static_cast<typename std::make_signed<index_t>::type>(firstBlockBaseIndex - headBase) / static_cast<typename std::make_signed<index_t>::type>(BLOCK_SIZE));
					auto indexIndex = (localBlockIndexHead + offset) & (localBlockIndex->size - 1);
					
					// Iterate the blocks and dequeue
					auto index = firstIndex;
					do {
						auto firstIndexInBlock = index;
						index_t endIndex = (index & ~static_cast<index_t>(BLOCK_SIZE - 1)) + static_cast<index_t>(BLOCK_SIZE);
						endIndex = details::circular_less_than<index_t>(firstIndex + static_cast<index_t>(actualCount), endIndex) ? firstIndex + static_cast<index_t>(actualCount) : endIndex;
						auto block = localBlockIndex->entries[indexIndex].block;
						if (MOODYCAMEL_NOEXCEPT_ASSIGN(T, T&&, details::deref_noexcept(itemFirst) = std::move((*(*block)[index])))) {
							while (index != endIndex) {
								auto& el = *((*block)[index]);
								*itemFirst++ = std::move(el);
								el.~T();
								++index;
							}
						}
						else {
							MOODYCAMEL_TRY {
								while (index != endIndex) {
									auto& el = *((*block)[index]);
									*itemFirst = std::move(el);
									++itemFirst;
									el.~T();
									++index;
								}
							}
							MOODYCAMEL_CATCH (...) {
								// It's too late to revert the dequeue, but we can make sure that all
								// the dequeued objects are properly destroyed and the block index
								// (and empty count) are properly updated before we propagate the exception
								do {
									block = localBlockIndex->entries[indexIndex].block;
									while (index != endIndex) {
										(*block)[index++]->~T();
									}
									block->ConcurrentQueue::Block::template set_many_empty<explicit_context>(firstIndexInBlock, static_cast<size_t>(endIndex - firstIndexInBlock));
									indexIndex = (indexIndex + 1) & (localBlockIndex->size - 1);
									
									firstIndexInBlock = index;
									endIndex = (index & ~static_cast<index_t>(BLOCK_SIZE - 1)) + static_cast<index_t>(BLOCK_SIZE);
									endIndex = details::circular_less_than<index_t>(firstIndex + static_cast<index_t>(actualCount), endIndex) ? firstIndex + static_cast<index_t>(actualCount) : endIndex;
								} while (index != firstIndex + actualCount);
								
								MOODYCAMEL_RETHROW;
							}
						}
						block->ConcurrentQueue::Block::template set_many_empty<explicit_context>(firstIndexInBlock, static_cast<size_t>(endIndex - firstIndexInBlock));
						indexIndex = (indexIndex + 1) & (localBlockIndex->size - 1);
					} while (index != firstIndex + actualCount);
					
					return actualCount;
				}
				else {
					// Wasn't anything to dequeue after all; make the effective dequeue count eventually consistent
					this->dequeueOvercommit.fetch_add(desiredCount, std::memory_order_release);
				}
			}
			
			return 0;
		}
		
	private:
		struct BlockIndexEntry
		{
			index_t base;
			Block* block;
		};
		
		struct BlockIndexHeader
		{
			size_t size;
			std::atomic<size_t> front;		// Current slot (not next, like pr_blockIndexFront)
			BlockIndexEntry* entries;
			void* prev;
		};
		
		
		bool new_block_index(size_t numberOfFilledSlotsToExpose)
		{
			auto prevBlockSizeMask = pr_blockIndexSize - 1;
			
			// Create the new block
			pr_blockIndexSize <<= 1;
			auto newRawPtr = static_cast<char*>((Traits::malloc)(sizeof(BlockIndexHeader) + std::alignment_of<BlockIndexEntry>::value - 1 + sizeof(BlockIndexEntry) * pr_blockIndexSize));
			if (newRawPtr == nullptr) {
				pr_blockIndexSize >>= 1;		// Reset to allow graceful retry
				return false;
			}
			
			auto newBlockIndexEntries = reinterpret_cast<BlockIndexEntry*>(details::align_for<BlockIndexEntry>(newRawPtr + sizeof(BlockIndexHeader)));
			
			// Copy in all the old indices, if any
			size_t j = 0;
			if (pr_blockIndexSlotsUsed != 0) {
				auto i = (pr_blockIndexFront - pr_blockIndexSlotsUsed) & prevBlockSizeMask;
				do {
					newBlockIndexEntries[j++] = pr_blockIndexEntries[i];
					i = (i + 1) & prevBlockSizeMask;
				} while (i != pr_blockIndexFront);
			}
			
			// Update everything
			auto header = new (newRawPtr) BlockIndexHeader;
			header->size = pr_blockIndexSize;
			header->front.store(numberOfFilledSlotsToExpose - 1, std::memory_order_relaxed);
			header->entries = newBlockIndexEntries;
			header->prev = pr_blockIndexRaw;		// we link the new block to the old one so we can free it later
			
			pr_blockIndexFront = j;
			pr_blockIndexEntries = newBlockIndexEntries;
			pr_blockIndexRaw = newRawPtr;
			blockIndex.store(header, std::memory_order_release);
			
			return true;
		}
		
	private:
		std::atomic<BlockIndexHeader*> blockIndex;
		
		// To be used by producer only -- consumer must use the ones in referenced by blockIndex
		size_t pr_blockIndexSlotsUsed;
		size_t pr_blockIndexSize;
		size_t pr_blockIndexFront;		// Next slot (not current)
		BlockIndexEntry* pr_blockIndexEntries;
		void* pr_blockIndexRaw;
		
#ifdef MOODYCAMEL_QUEUE_INTERNAL_DEBUG
	public:
		ExplicitProducer* nextExplicitProducer;
	private:
#endif
		
#ifdef MCDBGQ_TRACKMEM
		friend struct MemStats;
#endif
	};
	
	
	//////////////////////////////////
	// Implicit queue
	//////////////////////////////////
	
	struct ImplicitProducer : public ProducerBase
	{			
		ImplicitProducer(ConcurrentQueue* parent_) :
			ProducerBase(parent_, false),
			nextBlockIndexCapacity(IMPLICIT_INITIAL_INDEX_SIZE),
			blockIndex(nullptr)
		{
			new_block_index();
		}
		
		~ImplicitProducer()
		{
			// Note that since we're in the destructor we can assume that all enqueue/dequeue operations
			// completed already; this means that all undequeued elements are placed contiguously across
			// contiguous blocks, and that only the first and last remaining blocks can be only partially
			// empty (all other remaining blocks must be completely full).
			
#ifdef MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
			// Unregister ourselves for thread termination notification
			if (!this->inactive.load(std::memory_order_relaxed)) {
				details::ThreadExitNotifier::unsubscribe(&threadExitListener);
			}
#endif
			
			// Destroy all remaining elements!
			auto tail = this->tailIndex.load(std::memory_order_relaxed);
			auto index = this->headIndex.load(std::memory_order_relaxed);
			Block* block = nullptr;
			assert(index == tail || details::circular_less_than(index, tail));
			bool forceFreeLastBlock = index != tail;		// If we enter the loop, then the last (tail) block will not be freed
			while (index != tail) {
				if ((index & static_cast<index_t>(BLOCK_SIZE - 1)) == 0 || block == nullptr) {
					if (block != nullptr) {
						// Free the old block
						this->parent->add_block_to_free_list(block);
					}
					
					block = get_block_index_entry_for_index(index)->value.load(std::memory_order_relaxed);
				}
				
				((*block)[index])->~T();
				++index;
			}
			// Even if the queue is empty, there's still one block that's not on the free list
			// (unless the head index reached the end of it, in which case the tail will be poised
			// to create a new block).
			if (this->tailBlock != nullptr && (forceFreeLastBlock || (tail & static_cast<index_t>(BLOCK_SIZE - 1)) != 0)) {
				this->parent->add_block_to_free_list(this->tailBlock);
			}
			
			// Destroy block index
			auto localBlockIndex = blockIndex.load(std::memory_order_relaxed);
			if (localBlockIndex != nullptr) {
				for (size_t i = 0; i != localBlockIndex->capacity; ++i) {
					localBlockIndex->index[i]->~BlockIndexEntry();
				}
				do {
					auto prev = localBlockIndex->prev;
					localBlockIndex->~BlockIndexHeader();
					(Traits::free)(localBlockIndex);
					localBlockIndex = prev;
				} while (localBlockIndex != nullptr);
			}
		}
		
		template<AllocationMode allocMode, typename U>
		inline bool enqueue(U&& element)
		{
			index_t currentTailIndex = this->tailIndex.load(std::memory_order_relaxed);
			index_t newTailIndex = 1 + currentTailIndex;
			if ((currentTailIndex & static_cast<index_t>(BLOCK_SIZE - 1)) == 0) {
				// We reached the end of a block, start a new one
				auto head = this->headIndex.load(std::memory_order_relaxed);
				assert(!details::circular_less_than<index_t>(currentTailIndex, head));
				if (!details::circular_less_than<index_t>(head, currentTailIndex + BLOCK_SIZE) || (MAX_SUBQUEUE_SIZE != details::const_numeric_max<size_t>::value && (MAX_SUBQUEUE_SIZE == 0 || MAX_SUBQUEUE_SIZE - BLOCK_SIZE < currentTailIndex - head))) {
					return false;
				}
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODBLOCKINDEX
				debug::DebugLock lock(mutex);
#endif
				// Find out where we'll be inserting this block in the block index
				BlockIndexEntry* idxEntry;
				if (!insert_block_index_entry<allocMode>(idxEntry, currentTailIndex)) {
					return false;
				}
				
				// Get ahold of a new block
				auto newBlock = this->parent->ConcurrentQueue::template requisition_block<allocMode>();
				if (newBlock == nullptr) {
					rewind_block_index_tail();
					idxEntry->value.store(nullptr, std::memory_order_relaxed);
					return false;
				}
#ifdef MCDBGQ_TRACKMEM
				newBlock->owner = this;
#endif
				newBlock->ConcurrentQueue::Block::template reset_empty<implicit_context>();

				MOODYCAMEL_CONSTEXPR_IF (!MOODYCAMEL_NOEXCEPT_CTOR(T, U, new (static_cast<T*>(nullptr)) T(std::forward<U>(element)))) {
					// May throw, try to insert now before we publish the fact that we have this new block
					MOODYCAMEL_TRY {
						new ((*newBlock)[currentTailIndex]) T(std::forward<U>(element));
					}
					MOODYCAMEL_CATCH (...) {
						rewind_block_index_tail();
						idxEntry->value.store(nullptr, std::memory_order_relaxed);
						this->parent->add_block_to_free_list(newBlock);
						MOODYCAMEL_RETHROW;
					}
				}
				
				// Insert the new block into the index
				idxEntry->value.store(newBlock, std::memory_order_relaxed);
				
				this->tailBlock = newBlock;
				
				MOODYCAMEL_CONSTEXPR_IF (!MOODYCAMEL_NOEXCEPT_CTOR(T, U, new (static_cast<T*>(nullptr)) T(std::forward<U>(element)))) {
					this->tailIndex.store(newTailIndex, std::memory_order_release);
					return true;
				}
			}
			
			// Enqueue
			new ((*this->tailBlock)[currentTailIndex]) T(std::forward<U>(element));
			
			this->tailIndex.store(newTailIndex, std::memory_order_release);
			return true;
		}
		
		template<typename U>
		bool dequeue(U& element)
		{
			// See ExplicitProducer::dequeue for rationale and explanation
			index_t tail = this->tailIndex.load(std::memory_order_relaxed);
			index_t overcommit = this->dequeueOvercommit.load(std::memory_order_relaxed);
			if (details::circular_less_than<index_t>(this->dequeueOptimisticCount.load(std::memory_order_relaxed) - overcommit, tail)) {
				std::atomic_thread_fence(std::memory_order_acquire);
				
				index_t myDequeueCount = this->dequeueOptimisticCount.fetch_add(1, std::memory_order_relaxed);
				tail = this->tailIndex.load(std::memory_order_acquire);
				if ((details::likely)(details::circular_less_than<index_t>(myDequeueCount - overcommit, tail))) {
					index_t index = this->headIndex.fetch_add(1, std::memory_order_acq_rel);
					
					// Determine which block the element is in
					auto entry = get_block_index_entry_for_index(index);
					
					// Dequeue
					auto block = entry->value.load(std::memory_order_relaxed);
					auto& el = *((*block)[index]);
					
					if (!MOODYCAMEL_NOEXCEPT_ASSIGN(T, T&&, element = std::move(el))) {
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODBLOCKINDEX
						// Note: Acquiring the mutex with every dequeue instead of only when a block
						// is released is very sub-optimal, but it is, after all, purely debug code.
						debug::DebugLock lock(producer->mutex);
#endif
						struct Guard {
							Block* block;
							index_t index;
							BlockIndexEntry* entry;
							ConcurrentQueue* parent;
							
							~Guard()
							{
								(*block)[index]->~T();
								if (block->ConcurrentQueue::Block::template set_empty<implicit_context>(index)) {
									entry->value.store(nullptr, std::memory_order_relaxed);
									parent->add_block_to_free_list(block);
								}
							}
						} guard = { block, index, entry, this->parent };

						element = std::move(el); // NOLINT
					}
					else {
						element = std::move(el); // NOLINT
						el.~T(); // NOLINT

						if (block->ConcurrentQueue::Block::template set_empty<implicit_context>(index)) {
							{
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODBLOCKINDEX
								debug::DebugLock lock(mutex);
#endif
								// Add the block back into the global free pool (and remove from block index)
								entry->value.store(nullptr, std::memory_order_relaxed);
							}
							this->parent->add_block_to_free_list(block);		// releases the above store
						}
					}
					
					return true;
				}
				else {
					this->dequeueOvercommit.fetch_add(1, std::memory_order_release);
				}
			}
		
			return false;
		}
		
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4706)  // assignment within conditional expression
#endif
		template<AllocationMode allocMode, typename It>
		bool enqueue_bulk(It itemFirst, size_t count)
		{
			// First, we need to make sure we have enough room to enqueue all of the elements;
			// this means pre-allocating blocks and putting them in the block index (but only if
			// all the allocations succeeded).
			
			// Note that the tailBlock we start off with may not be owned by us any more;
			// this happens if it was filled up exactly to the top (setting tailIndex to
			// the first index of the next block which is not yet allocated), then dequeued
			// completely (putting it on the free list) before we enqueue again.
			
			index_t startTailIndex = this->tailIndex.load(std::memory_order_relaxed);
			auto startBlock = this->tailBlock;
			Block* firstAllocatedBlock = nullptr;
			auto endBlock = this->tailBlock;
			
			// Figure out how many blocks we'll need to allocate, and do so
			size_t blockBaseDiff = ((startTailIndex + count - 1) & ~static_cast<index_t>(BLOCK_SIZE - 1)) - ((startTailIndex - 1) & ~static_cast<index_t>(BLOCK_SIZE - 1));
			index_t currentTailIndex = (startTailIndex - 1) & ~static_cast<index_t>(BLOCK_SIZE - 1);
			if (blockBaseDiff > 0) {
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODBLOCKINDEX
				debug::DebugLock lock(mutex);
#endif
				do {
					blockBaseDiff -= static_cast<index_t>(BLOCK_SIZE);
					currentTailIndex += static_cast<index_t>(BLOCK_SIZE);
					
					// Find out where we'll be inserting this block in the block index
					BlockIndexEntry* idxEntry = nullptr;  // initialization here unnecessary but compiler can't always tell
					Block* newBlock;
					bool indexInserted = false;
					auto head = this->headIndex.load(std::memory_order_relaxed);
					assert(!details::circular_less_than<index_t>(currentTailIndex, head));
					bool full = !details::circular_less_than<index_t>(head, currentTailIndex + BLOCK_SIZE) || (MAX_SUBQUEUE_SIZE != details::const_numeric_max<size_t>::value && (MAX_SUBQUEUE_SIZE == 0 || MAX_SUBQUEUE_SIZE - BLOCK_SIZE < currentTailIndex - head));

					if (full || !(indexInserted = insert_block_index_entry<allocMode>(idxEntry, currentTailIndex)) || (newBlock = this->parent->ConcurrentQueue::template requisition_block<allocMode>()) == nullptr) {
						// Index allocation or block allocation failed; revert any other allocations
						// and index insertions done so far for this operation
						if (indexInserted) {
							rewind_block_index_tail();
							idxEntry->value.store(nullptr, std::memory_order_relaxed);
						}
						currentTailIndex = (startTailIndex - 1) & ~static_cast<index_t>(BLOCK_SIZE - 1);
						for (auto block = firstAllocatedBlock; block != nullptr; block = block->next) {
							currentTailIndex += static_cast<index_t>(BLOCK_SIZE);
							idxEntry = get_block_index_entry_for_index(currentTailIndex);
							idxEntry->value.store(nullptr, std::memory_order_relaxed);
							rewind_block_index_tail();
						}
						this->parent->add_blocks_to_free_list(firstAllocatedBlock);
						this->tailBlock = startBlock;
						
						return false;
					}
					
#ifdef MCDBGQ_TRACKMEM
					newBlock->owner = this;
#endif
					newBlock->ConcurrentQueue::Block::template reset_empty<implicit_context>();
					newBlock->next = nullptr;
					
					// Insert the new block into the index
					idxEntry->value.store(newBlock, std::memory_order_relaxed);
					
					// Store the chain of blocks so that we can undo if later allocations fail,
					// and so that we can find the blocks when we do the actual enqueueing
					if ((startTailIndex & static_cast<index_t>(BLOCK_SIZE - 1)) != 0 || firstAllocatedBlock != nullptr) {
						assert(this->tailBlock != nullptr);
						this->tailBlock->next = newBlock;
					}
					this->tailBlock = newBlock;
					endBlock = newBlock;
					firstAllocatedBlock = firstAllocatedBlock == nullptr ? newBlock : firstAllocatedBlock;
				} while (blockBaseDiff > 0);
			}
			
			// Enqueue, one block at a time
			index_t newTailIndex = startTailIndex + static_cast<index_t>(count);
			currentTailIndex = startTailIndex;
			this->tailBlock = startBlock;
			assert((startTailIndex & static_cast<index_t>(BLOCK_SIZE - 1)) != 0 || firstAllocatedBlock != nullptr || count == 0);
			if ((startTailIndex & static_cast<index_t>(BLOCK_SIZE - 1)) == 0 && firstAllocatedBlock != nullptr) {
				this->tailBlock = firstAllocatedBlock;
			}
			while (true) {
				index_t stopIndex = (currentTailIndex & ~static_cast<index_t>(BLOCK_SIZE - 1)) + static_cast<index_t>(BLOCK_SIZE);
				if (details::circular_less_than<index_t>(newTailIndex, stopIndex)) {
					stopIndex = newTailIndex;
				}
				MOODYCAMEL_CONSTEXPR_IF (MOODYCAMEL_NOEXCEPT_CTOR(T, decltype(*itemFirst), new (static_cast<T*>(nullptr)) T(details::deref_noexcept(itemFirst)))) {
					while (currentTailIndex != stopIndex) {
						new ((*this->tailBlock)[currentTailIndex++]) T(*itemFirst++);
					}
				}
				else {
					MOODYCAMEL_TRY {
						while (currentTailIndex != stopIndex) {
							new ((*this->tailBlock)[currentTailIndex]) T(details::nomove_if<!MOODYCAMEL_NOEXCEPT_CTOR(T, decltype(*itemFirst), new (static_cast<T*>(nullptr)) T(details::deref_noexcept(itemFirst)))>::eval(*itemFirst));
							++currentTailIndex;
							++itemFirst;
						}
					}
					MOODYCAMEL_CATCH (...) {
						auto constructedStopIndex = currentTailIndex;
						auto lastBlockEnqueued = this->tailBlock;
						
						if (!details::is_trivially_destructible<T>::value) {
							auto block = startBlock;
							if ((startTailIndex & static_cast<index_t>(BLOCK_SIZE - 1)) == 0) {
								block = firstAllocatedBlock;
							}
							currentTailIndex = startTailIndex;
							while (true) {
								stopIndex = (currentTailIndex & ~static_cast<index_t>(BLOCK_SIZE - 1)) + static_cast<index_t>(BLOCK_SIZE);
								if (details::circular_less_than<index_t>(constructedStopIndex, stopIndex)) {
									stopIndex = constructedStopIndex;
								}
								while (currentTailIndex != stopIndex) {
									(*block)[currentTailIndex++]->~T();
								}
								if (block == lastBlockEnqueued) {
									break;
								}
								block = block->next;
							}
						}
						
						currentTailIndex = (startTailIndex - 1) & ~static_cast<index_t>(BLOCK_SIZE - 1);
						for (auto block = firstAllocatedBlock; block != nullptr; block = block->next) {
							currentTailIndex += static_cast<index_t>(BLOCK_SIZE);
							auto idxEntry = get_block_index_entry_for_index(currentTailIndex);
							idxEntry->value.store(nullptr, std::memory_order_relaxed);
							rewind_block_index_tail();
						}
						this->parent->add_blocks_to_free_list(firstAllocatedBlock);
						this->tailBlock = startBlock;
						MOODYCAMEL_RETHROW;
					}
				}
				
				if (this->tailBlock == endBlock) {
					assert(currentTailIndex == newTailIndex);
					break;
				}
				this->tailBlock = this->tailBlock->next;
			}
			this->tailIndex.store(newTailIndex, std::memory_order_release);
			return true;
		}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
		
		template<typename It>
		size_t dequeue_bulk(It& itemFirst, size_t max)
		{
			auto tail = this->tailIndex.load(std::memory_order_relaxed);
			auto overcommit = this->dequeueOvercommit.load(std::memory_order_relaxed);
			auto desiredCount = static_cast<size_t>(tail - (this->dequeueOptimisticCount.load(std::memory_order_relaxed) - overcommit));
			if (details::circular_less_than<size_t>(0, desiredCount)) {
				desiredCount = desiredCount < max ? desiredCount : max;
				std::atomic_thread_fence(std::memory_order_acquire);
				
				auto myDequeueCount = this->dequeueOptimisticCount.fetch_add(desiredCount, std::memory_order_relaxed);
				
				tail = this->tailIndex.load(std::memory_order_acquire);
				auto actualCount = static_cast<size_t>(tail - (myDequeueCount - overcommit));
				if (details::circular_less_than<size_t>(0, actualCount)) {
					actualCount = desiredCount < actualCount ? desiredCount : actualCount;
					if (actualCount < desiredCount) {
						this->dequeueOvercommit.fetch_add(desiredCount - actualCount, std::memory_order_release);
					}
					
					// Get the first index. Note that since there's guaranteed to be at least actualCount elements, this
					// will never exceed tail.
					auto firstIndex = this->headIndex.fetch_add(actualCount, std::memory_order_acq_rel);
					
					// Iterate the blocks and dequeue
					auto index = firstIndex;
					BlockIndexHeader* localBlockIndex;
					auto indexIndex = get_block_index_index_for_index(index, localBlockIndex);
					do {
						auto blockStartIndex = index;
						index_t endIndex = (index & ~static_cast<index_t>(BLOCK_SIZE - 1)) + static_cast<index_t>(BLOCK_SIZE);
						endIndex = details::circular_less_than<index_t>(firstIndex + static_cast<index_t>(actualCount), endIndex) ? firstIndex + static_cast<index_t>(actualCount) : endIndex;
						
						auto entry = localBlockIndex->index[indexIndex];
						auto block = entry->value.load(std::memory_order_relaxed);
						if (MOODYCAMEL_NOEXCEPT_ASSIGN(T, T&&, details::deref_noexcept(itemFirst) = std::move((*(*block)[index])))) {
							while (index != endIndex) {
								auto& el = *((*block)[index]);
								*itemFirst++ = std::move(el);
								el.~T();
								++index;
							}
						}
						else {
							MOODYCAMEL_TRY {
								while (index != endIndex) {
									auto& el = *((*block)[index]);
									*itemFirst = std::move(el);
									++itemFirst;
									el.~T();
									++index;
								}
							}
							MOODYCAMEL_CATCH (...) {
								do {
									entry = localBlockIndex->index[indexIndex];
									block = entry->value.load(std::memory_order_relaxed);
									while (index != endIndex) {
										(*block)[index++]->~T();
									}
									
									if (block->ConcurrentQueue::Block::template set_many_empty<implicit_context>(blockStartIndex, static_cast<size_t>(endIndex - blockStartIndex))) {
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODBLOCKINDEX
										debug::DebugLock lock(mutex);
#endif
										entry->value.store(nullptr, std::memory_order_relaxed);
										this->parent->add_block_to_free_list(block);
									}
									indexIndex = (indexIndex + 1) & (localBlockIndex->capacity - 1);
									
									blockStartIndex = index;
									endIndex = (index & ~static_cast<index_t>(BLOCK_SIZE - 1)) + static_cast<index_t>(BLOCK_SIZE);
									endIndex = details::circular_less_than<index_t>(firstIndex + static_cast<index_t>(actualCount), endIndex) ? firstIndex + static_cast<index_t>(actualCount) : endIndex;
								} while (index != firstIndex + actualCount);
								
								MOODYCAMEL_RETHROW;
							}
						}
						if (block->ConcurrentQueue::Block::template set_many_empty<implicit_context>(blockStartIndex, static_cast<size_t>(endIndex - blockStartIndex))) {
							{
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODBLOCKINDEX
								debug::DebugLock lock(mutex);
#endif
								// Note that the set_many_empty above did a release, meaning that anybody who acquires the block
								// we're about to free can use it safely since our writes (and reads!) will have happened-before then.
								entry->value.store(nullptr, std::memory_order_relaxed);
							}
							this->parent->add_block_to_free_list(block);		// releases the above store
						}
						indexIndex = (indexIndex + 1) & (localBlockIndex->capacity - 1);
					} while (index != firstIndex + actualCount);
					
					return actualCount;
				}
				else {
					this->dequeueOvercommit.fetch_add(desiredCount, std::memory_order_release);
				}
			}
			
			return 0;
		}
		
	private:
		// The block size must be > 1, so any number with the low bit set is an invalid block base index
		static const index_t INVALID_BLOCK_BASE = 1;
		
		struct BlockIndexEntry
		{
			std::atomic<index_t> key;
			std::atomic<Block*> value;
		};
		
		struct BlockIndexHeader
		{
			size_t capacity;
			std::atomic<size_t> tail;
			BlockIndexEntry* entries;
			BlockIndexEntry** index;
			BlockIndexHeader* prev;
		};
		
		template<AllocationMode allocMode>
		inline bool insert_block_index_entry(BlockIndexEntry*& idxEntry, index_t blockStartIndex)
		{
			auto localBlockIndex = blockIndex.load(std::memory_order_relaxed);		// We're the only writer thread, relaxed is OK
			if (localBlockIndex == nullptr) {
				return false;  // this can happen if new_block_index failed in the constructor
			}
			size_t newTail = (localBlockIndex->tail.load(std::memory_order_relaxed) + 1) & (localBlockIndex->capacity - 1);
			idxEntry = localBlockIndex->index[newTail];
			if (idxEntry->key.load(std::memory_order_relaxed) == INVALID_BLOCK_BASE ||
				idxEntry->value.load(std::memory_order_relaxed) == nullptr) {
				
				idxEntry->key.store(blockStartIndex, std::memory_order_relaxed);
				localBlockIndex->tail.store(newTail, std::memory_order_release);
				return true;
			}
			
			// No room in the old block index, try to allocate another one!
			MOODYCAMEL_CONSTEXPR_IF (allocMode == CannotAlloc) {
				return false;
			}
			else if (!new_block_index()) {
				return false;
			}
			else {
				localBlockIndex = blockIndex.load(std::memory_order_relaxed);
				newTail = (localBlockIndex->tail.load(std::memory_order_relaxed) + 1) & (localBlockIndex->capacity - 1);
				idxEntry = localBlockIndex->index[newTail];
				assert(idxEntry->key.load(std::memory_order_relaxed) == INVALID_BLOCK_BASE);
				idxEntry->key.store(blockStartIndex, std::memory_order_relaxed);
				localBlockIndex->tail.store(newTail, std::memory_order_release);
				return true;
			}
		}
		
		inline void rewind_block_index_tail()
		{
			auto localBlockIndex = blockIndex.load(std::memory_order_relaxed);
			localBlockIndex->tail.store((localBlockIndex->tail.load(std::memory_order_relaxed) - 1) & (localBlockIndex->capacity - 1), std::memory_order_relaxed);
		}
		
		inline BlockIndexEntry* get_block_index_entry_for_index(index_t index) const
		{
			BlockIndexHeader* localBlockIndex;
			auto idx = get_block_index_index_for_index(index, localBlockIndex);
			return localBlockIndex->index[idx];
		}
		
		inline size_t get_block_index_index_for_index(index_t index, BlockIndexHeader*& localBlockIndex) const
		{
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODBLOCKINDEX
			debug::DebugLock lock(mutex);
#endif
			index &= ~static_cast<index_t>(BLOCK_SIZE - 1);
			localBlockIndex = blockIndex.load(std::memory_order_acquire);
			auto tail = localBlockIndex->tail.load(std::memory_order_acquire);
			auto tailBase = localBlockIndex->index[tail]->key.load(std::memory_order_relaxed);
			assert(tailBase != INVALID_BLOCK_BASE);
			// Note: Must use division instead of shift because the index may wrap around, causing a negative
			// offset, whose negativity we want to preserve
			auto offset = static_cast<size_t>(static_cast<typename std::make_signed<index_t>::type>(index - tailBase) / static_cast<typename std::make_signed<index_t>::type>(BLOCK_SIZE));
			size_t idx = (tail + offset) & (localBlockIndex->capacity - 1);
			assert(localBlockIndex->index[idx]->key.load(std::memory_order_relaxed) == index && localBlockIndex->index[idx]->value.load(std::memory_order_relaxed) != nullptr);
			return idx;
		}
		
		bool new_block_index()
		{
			auto prev = blockIndex.load(std::memory_order_relaxed);
			size_t prevCapacity = prev == nullptr ? 0 : prev->capacity;
			auto entryCount = prev == nullptr ? nextBlockIndexCapacity : prevCapacity;
			auto raw = static_cast<char*>((Traits::malloc)(
				sizeof(BlockIndexHeader) +
				std::alignment_of<BlockIndexEntry>::value - 1 + sizeof(BlockIndexEntry) * entryCount +
				std::alignment_of<BlockIndexEntry*>::value - 1 + sizeof(BlockIndexEntry*) * nextBlockIndexCapacity));
			if (raw == nullptr) {
				return false;
			}
			
			auto header = new (raw) BlockIndexHeader;
			auto entries = reinterpret_cast<BlockIndexEntry*>(details::align_for<BlockIndexEntry>(raw + sizeof(BlockIndexHeader)));
			auto index = reinterpret_cast<BlockIndexEntry**>(details::align_for<BlockIndexEntry*>(reinterpret_cast<char*>(entries) + sizeof(BlockIndexEntry) * entryCount));
			if (prev != nullptr) {
				auto prevTail = prev->tail.load(std::memory_order_relaxed);
				auto prevPos = prevTail;
				size_t i = 0;
				do {
					prevPos = (prevPos + 1) & (prev->capacity - 1);
					index[i++] = prev->index[prevPos];
				} while (prevPos != prevTail);
				assert(i == prevCapacity);
			}
			for (size_t i = 0; i != entryCount; ++i) {
				new (entries + i) BlockIndexEntry;
				entries[i].key.store(INVALID_BLOCK_BASE, std::memory_order_relaxed);
				index[prevCapacity + i] = entries + i;
			}
			header->prev = prev;
			header->entries = entries;
			header->index = index;
			header->capacity = nextBlockIndexCapacity;
			header->tail.store((prevCapacity - 1) & (nextBlockIndexCapacity - 1), std::memory_order_relaxed);
			
			blockIndex.store(header, std::memory_order_release);
			
			nextBlockIndexCapacity <<= 1;
			
			return true;
		}
		
	private:
		size_t nextBlockIndexCapacity;
		std::atomic<BlockIndexHeader*> blockIndex;

#ifdef MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
	public:
		details::ThreadExitListener threadExitListener;
	private:
#endif
		
#ifdef MOODYCAMEL_QUEUE_INTERNAL_DEBUG
	public:
		ImplicitProducer* nextImplicitProducer;
	private:
#endif

#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODBLOCKINDEX
		mutable debug::DebugMutex mutex;
#endif
#ifdef MCDBGQ_TRACKMEM
		friend struct MemStats;
#endif
	};
	
	
	//////////////////////////////////
	// Block pool manipulation
	//////////////////////////////////
	
	void populate_initial_block_list(size_t blockCount)
	{
		initialBlockPoolSize = blockCount;
		if (initialBlockPoolSize == 0) {
			initialBlockPool = nullptr;
			return;
		}
		
		initialBlockPool = create_array<Block>(blockCount);
		if (initialBlockPool == nullptr) {
			initialBlockPoolSize = 0;
		}
		for (size_t i = 0; i < initialBlockPoolSize; ++i) {
			initialBlockPool[i].dynamicallyAllocated = false;
		}
	}
	
	inline Block* try_get_block_from_initial_pool()
	{
		if (initialBlockPoolIndex.load(std::memory_order_relaxed) >= initialBlockPoolSize) {
			return nullptr;
		}
		
		auto index = initialBlockPoolIndex.fetch_add(1, std::memory_order_relaxed);
		
		return index < initialBlockPoolSize ? (initialBlockPool + index) : nullptr;
	}
	
	inline void add_block_to_free_list(Block* block)
	{
#ifdef MCDBGQ_TRACKMEM
		block->owner = nullptr;
#endif
		if (!Traits::RECYCLE_ALLOCATED_BLOCKS && block->dynamicallyAllocated) {
			destroy(block);
		}
		else {
			freeList.add(block);
		}
	}
	
	inline void add_blocks_to_free_list(Block* block)
	{
		while (block != nullptr) {
			auto next = block->next;
			add_block_to_free_list(block);
			block = next;
		}
	}
	
	inline Block* try_get_block_from_free_list()
	{
		return freeList.try_get();
	}
	
	// Gets a free block from one of the memory pools, or allocates a new one (if applicable)
	template<AllocationMode canAlloc>
	Block* requisition_block()
	{
		auto block = try_get_block_from_initial_pool();
		if (block != nullptr) {
			return block;
		}
		
		block = try_get_block_from_free_list();
		if (block != nullptr) {
			return block;
		}
		
		MOODYCAMEL_CONSTEXPR_IF (canAlloc == CanAlloc) {
			return create<Block>();
		}
		else {
			return nullptr;
		}
	}
	

#ifdef MCDBGQ_TRACKMEM
	public:
		struct MemStats {
			size_t allocatedBlocks;
			size_t usedBlocks;
			size_t freeBlocks;
			size_t ownedBlocksExplicit;
			size_t ownedBlocksImplicit;
			size_t implicitProducers;
			size_t explicitProducers;
			size_t elementsEnqueued;
			size_t blockClassBytes;
			size_t queueClassBytes;
			size_t implicitBlockIndexBytes;
			size_t explicitBlockIndexBytes;
			
			friend class ConcurrentQueue;
			
		private:
			static MemStats getFor(ConcurrentQueue* q)
			{
				MemStats stats = { 0 };
				
				stats.elementsEnqueued = q->size_approx();
			
				auto block = q->freeList.head_unsafe();
				while (block != nullptr) {
					++stats.allocatedBlocks;
					++stats.freeBlocks;
					block = block->freeListNext.load(std::memory_order_relaxed);
				}
				
				for (auto ptr = q->producerListTail.load(std::memory_order_acquire); ptr != nullptr; ptr = ptr->next_prod()) {
					bool implicit = dynamic_cast<ImplicitProducer*>(ptr) != nullptr;
					stats.implicitProducers += implicit ? 1 : 0;
					stats.explicitProducers += implicit ? 0 : 1;
					
					if (implicit) {
						auto prod = static_cast<ImplicitProducer*>(ptr);
						stats.queueClassBytes += sizeof(ImplicitProducer);
						auto head = prod->headIndex.load(std::memory_order_relaxed);
						auto tail = prod->tailIndex.load(std::memory_order_relaxed);
						auto hash = prod->blockIndex.load(std::memory_order_relaxed);
						if (hash != nullptr) {
							for (size_t i = 0; i != hash->capacity; ++i) {
								if (hash->index[i]->key.load(std::memory_order_relaxed) != ImplicitProducer::INVALID_BLOCK_BASE && hash->index[i]->value.load(std::memory_order_relaxed) != nullptr) {
									++stats.allocatedBlocks;
									++stats.ownedBlocksImplicit;
								}
							}
							stats.implicitBlockIndexBytes += hash->capacity * sizeof(typename ImplicitProducer::BlockIndexEntry);
							for (; hash != nullptr; hash = hash->prev) {
								stats.implicitBlockIndexBytes += sizeof(typename ImplicitProducer::BlockIndexHeader) + hash->capacity * sizeof(typename ImplicitProducer::BlockIndexEntry*);
							}
						}
						for (; details::circular_less_than<index_t>(head, tail); head += BLOCK_SIZE) {
							//auto block = prod->get_block_index_entry_for_index(head);
							++stats.usedBlocks;
						}
					}
					else {
						auto prod = static_cast<ExplicitProducer*>(ptr);
						stats.queueClassBytes += sizeof(ExplicitProducer);
						auto tailBlock = prod->tailBlock;
						bool wasNonEmpty = false;
						if (tailBlock != nullptr) {
							auto block = tailBlock;
							do {
								++stats.allocatedBlocks;
								if (!block->ConcurrentQueue::Block::template is_empty<explicit_context>() || wasNonEmpty) {
									++stats.usedBlocks;
									wasNonEmpty = wasNonEmpty || block != tailBlock;
								}
								++stats.ownedBlocksExplicit;
								block = block->next;
							} while (block != tailBlock);
						}
						auto index = prod->blockIndex.load(std::memory_order_relaxed);
						while (index != nullptr) {
							stats.explicitBlockIndexBytes += sizeof(typename ExplicitProducer::BlockIndexHeader) + index->size * sizeof(typename ExplicitProducer::BlockIndexEntry);
							index = static_cast<typename ExplicitProducer::BlockIndexHeader*>(index->prev);
						}
					}
				}
				
				auto freeOnInitialPool = q->initialBlockPoolIndex.load(std::memory_order_relaxed) >= q->initialBlockPoolSize ? 0 : q->initialBlockPoolSize - q->initialBlockPoolIndex.load(std::memory_order_relaxed);
				stats.allocatedBlocks += freeOnInitialPool;
				stats.freeBlocks += freeOnInitialPool;
				
				stats.blockClassBytes = sizeof(Block) * stats.allocatedBlocks;
				stats.queueClassBytes += sizeof(ConcurrentQueue);
				
				return stats;
			}
		};
		
		// For debugging only. Not thread-safe.
		MemStats getMemStats()
		{
			return MemStats::getFor(this);
		}
	private:
		friend struct MemStats;
#endif
	
	
	//////////////////////////////////
	// Producer list manipulation
	//////////////////////////////////	
	
	ProducerBase* recycle_or_create_producer(bool isExplicit)
	{
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODHASH
		debug::DebugLock lock(implicitProdMutex);
#endif
		// Try to re-use one first
		for (auto ptr = producerListTail.load(std::memory_order_acquire); ptr != nullptr; ptr = ptr->next_prod()) {
			if (ptr->inactive.load(std::memory_order_relaxed) && ptr->isExplicit == isExplicit) {
				bool expected = true;
				if (ptr->inactive.compare_exchange_strong(expected, /* desired */ false, std::memory_order_acquire, std::memory_order_relaxed)) {
					// We caught one! It's been marked as activated, the caller can have it
					return ptr;
				}
			}
		}

		return add_producer(isExplicit ? static_cast<ProducerBase*>(create<ExplicitProducer>(this)) : create<ImplicitProducer>(this));
	}
	
	ProducerBase* add_producer(ProducerBase* producer)
	{
		// Handle failed memory allocation
		if (producer == nullptr) {
			return nullptr;
		}
		
		producerCount.fetch_add(1, std::memory_order_relaxed);
		
		// Add it to the lock-free list
		auto prevTail = producerListTail.load(std::memory_order_relaxed);
		do {
			producer->next = prevTail;
		} while (!producerListTail.compare_exchange_weak(prevTail, producer, std::memory_order_release, std::memory_order_relaxed));
		
#ifdef MOODYCAMEL_QUEUE_INTERNAL_DEBUG
		if (producer->isExplicit) {
			auto prevTailExplicit = explicitProducers.load(std::memory_order_relaxed);
			do {
				static_cast<ExplicitProducer*>(producer)->nextExplicitProducer = prevTailExplicit;
			} while (!explicitProducers.compare_exchange_weak(prevTailExplicit, static_cast<ExplicitProducer*>(producer), std::memory_order_release, std::memory_order_relaxed));
		}
		else {
			auto prevTailImplicit = implicitProducers.load(std::memory_order_relaxed);
			do {
				static_cast<ImplicitProducer*>(producer)->nextImplicitProducer = prevTailImplicit;
			} while (!implicitProducers.compare_exchange_weak(prevTailImplicit, static_cast<ImplicitProducer*>(producer), std::memory_order_release, std::memory_order_relaxed));
		}
#endif
		
		return producer;
	}
	
	void reown_producers()
	{
		// After another instance is moved-into/swapped-with this one, all the
		// producers we stole still think their parents are the other queue.
		// So fix them up!
		for (auto ptr = producerListTail.load(std::memory_order_relaxed); ptr != nullptr; ptr = ptr->next_prod()) {
			ptr->parent = this;
		}
	}
	
	
	//////////////////////////////////
	// Implicit producer hash
	//////////////////////////////////
	
	struct ImplicitProducerKVP
	{
		std::atomic<details::thread_id_t> key;
		ImplicitProducer* value;		// No need for atomicity since it's only read by the thread that sets it in the first place
		
		ImplicitProducerKVP() : value(nullptr) { }
		
		ImplicitProducerKVP(ImplicitProducerKVP&& other) MOODYCAMEL_NOEXCEPT
		{
			key.store(other.key.load(std::memory_order_relaxed), std::memory_order_relaxed);
			value = other.value;
		}
		
		inline ImplicitProducerKVP& operator=(ImplicitProducerKVP&& other) MOODYCAMEL_NOEXCEPT
		{
			swap(other);
			return *this;
		}
		
		inline void swap(ImplicitProducerKVP& other) MOODYCAMEL_NOEXCEPT
		{
			if (this != &other) {
				details::swap_relaxed(key, other.key);
				std::swap(value, other.value);
			}
		}
	};
	
	template<typename XT, typename XTraits>
	friend void moodycamel::swap(typename ConcurrentQueue<XT, XTraits>::ImplicitProducerKVP&, typename ConcurrentQueue<XT, XTraits>::ImplicitProducerKVP&) MOODYCAMEL_NOEXCEPT;
	
	struct ImplicitProducerHash
	{
		size_t capacity;
		ImplicitProducerKVP* entries;
		ImplicitProducerHash* prev;
	};
	
	inline void populate_initial_implicit_producer_hash()
	{
		MOODYCAMEL_CONSTEXPR_IF (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0) {
			return;
		}
		else {
			implicitProducerHashCount.store(0, std::memory_order_relaxed);
			auto hash = &initialImplicitProducerHash;
			hash->capacity = INITIAL_IMPLICIT_PRODUCER_HASH_SIZE;
			hash->entries = &initialImplicitProducerHashEntries[0];
			for (size_t i = 0; i != INITIAL_IMPLICIT_PRODUCER_HASH_SIZE; ++i) {
				initialImplicitProducerHashEntries[i].key.store(details::invalid_thread_id, std::memory_order_relaxed);
			}
			hash->prev = nullptr;
			implicitProducerHash.store(hash, std::memory_order_relaxed);
		}
	}
	
	void swap_implicit_producer_hashes(ConcurrentQueue& other)
	{
		MOODYCAMEL_CONSTEXPR_IF (INITIAL_IMPLICIT_PRODUCER_HASH_SIZE == 0) {
			return;
		}
		else {
			// Swap (assumes our implicit producer hash is initialized)
			initialImplicitProducerHashEntries.swap(other.initialImplicitProducerHashEntries);
			initialImplicitProducerHash.entries = &initialImplicitProducerHashEntries[0];
			other.initialImplicitProducerHash.entries = &other.initialImplicitProducerHashEntries[0];
			
			details::swap_relaxed(implicitProducerHashCount, other.implicitProducerHashCount);
			
			details::swap_relaxed(implicitProducerHash, other.implicitProducerHash);
			if (implicitProducerHash.load(std::memory_order_relaxed) == &other.initialImplicitProducerHash) {
				implicitProducerHash.store(&initialImplicitProducerHash, std::memory_order_relaxed);
			}
			else {
				ImplicitProducerHash* hash;
				for (hash = implicitProducerHash.load(std::memory_order_relaxed); hash->prev != &other.initialImplicitProducerHash; hash = hash->prev) {
					continue;
				}
				hash->prev = &initialImplicitProducerHash;
			}
			if (other.implicitProducerHash.load(std::memory_order_relaxed) == &initialImplicitProducerHash) {
				other.implicitProducerHash.store(&other.initialImplicitProducerHash, std::memory_order_relaxed);
			}
			else {
				ImplicitProducerHash* hash;
				for (hash = other.implicitProducerHash.load(std::memory_order_relaxed); hash->prev != &initialImplicitProducerHash; hash = hash->prev) {
					continue;
				}
				hash->prev = &other.initialImplicitProducerHash;
			}
		}
	}
	
	// Only fails (returns nullptr) if memory allocation fails
	ImplicitProducer* get_or_add_implicit_producer()
	{
		// Note that since the data is essentially thread-local (key is thread ID),
		// there's a reduced need for fences (memory ordering is already consistent
		// for any individual thread), except for the current table itself.
		
		// Start by looking for the thread ID in the current and all previous hash tables.
		// If it's not found, it must not be in there yet, since this same thread would
		// have added it previously to one of the tables that we traversed.
		
		// Code and algorithm adapted from http://preshing.com/20130605/the-worlds-simplest-lock-free-hash-table
		
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODHASH
		debug::DebugLock lock(implicitProdMutex);
#endif
		
		auto id = details::thread_id();
		auto hashedId = details::hash_thread_id(id);
		
		auto mainHash = implicitProducerHash.load(std::memory_order_acquire);
		assert(mainHash != nullptr);  // silence clang-tidy and MSVC warnings (hash cannot be null)
		for (auto hash = mainHash; hash != nullptr; hash = hash->prev) {
			// Look for the id in this hash
			auto index = hashedId;
			while (true) {		// Not an infinite loop because at least one slot is free in the hash table
				index &= hash->capacity - 1u;
				
				auto probedKey = hash->entries[index].key.load(std::memory_order_relaxed);
				if (probedKey == id) {
					// Found it! If we had to search several hashes deep, though, we should lazily add it
					// to the current main hash table to avoid the extended search next time.
					// Note there's guaranteed to be room in the current hash table since every subsequent
					// table implicitly reserves space for all previous tables (there's only one
					// implicitProducerHashCount).
					auto value = hash->entries[index].value;
					if (hash != mainHash) {
						index = hashedId;
						while (true) {
							index &= mainHash->capacity - 1u;
							auto empty = details::invalid_thread_id;
#ifdef MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
							auto reusable = details::invalid_thread_id2;
							if (mainHash->entries[index].key.compare_exchange_strong(empty,    id, std::memory_order_seq_cst, std::memory_order_relaxed) ||
								mainHash->entries[index].key.compare_exchange_strong(reusable, id, std::memory_order_seq_cst, std::memory_order_relaxed)) {
#else
							if (mainHash->entries[index].key.compare_exchange_strong(empty,    id, std::memory_order_seq_cst, std::memory_order_relaxed)) {
#endif
								mainHash->entries[index].value = value;
								break;
							}
							++index;
						}
					}
					
					return value;
				}
				if (probedKey == details::invalid_thread_id) {
					break;		// Not in this hash table
				}
				++index;
			}
		}
		
		// Insert!
		auto newCount = 1 + implicitProducerHashCount.fetch_add(1, std::memory_order_relaxed);
		while (true) {
			// NOLINTNEXTLINE(clang-analyzer-core.NullDereference)
			if (newCount >= (mainHash->capacity >> 1) && !implicitProducerHashResizeInProgress.test_and_set(std::memory_order_acquire)) {
				// We've acquired the resize lock, try to allocate a bigger hash table.
				// Note the acquire fence synchronizes with the release fence at the end of this block, and hence when
				// we reload implicitProducerHash it must be the most recent version (it only gets changed within this
				// locked block).
				mainHash = implicitProducerHash.load(std::memory_order_acquire);
				if (newCount >= (mainHash->capacity >> 1)) {
					size_t newCapacity = mainHash->capacity << 1;
					while (newCount >= (newCapacity >> 1)) {
						newCapacity <<= 1;
					}
					auto raw = static_cast<char*>((Traits::malloc)(sizeof(ImplicitProducerHash) + std::alignment_of<ImplicitProducerKVP>::value - 1 + sizeof(ImplicitProducerKVP) * newCapacity));
					if (raw == nullptr) {
						// Allocation failed
						implicitProducerHashCount.fetch_sub(1, std::memory_order_relaxed);
						implicitProducerHashResizeInProgress.clear(std::memory_order_relaxed);
						return nullptr;
					}
					
					auto newHash = new (raw) ImplicitProducerHash;
					newHash->capacity = static_cast<size_t>(newCapacity);
					newHash->entries = reinterpret_cast<ImplicitProducerKVP*>(details::align_for<ImplicitProducerKVP>(raw + sizeof(ImplicitProducerHash)));
					for (size_t i = 0; i != newCapacity; ++i) {
						new (newHash->entries + i) ImplicitProducerKVP;
						newHash->entries[i].key.store(details::invalid_thread_id, std::memory_order_relaxed);
					}
					newHash->prev = mainHash;
					implicitProducerHash.store(newHash, std::memory_order_release);
					implicitProducerHashResizeInProgress.clear(std::memory_order_release);
					mainHash = newHash;
				}
				else {
					implicitProducerHashResizeInProgress.clear(std::memory_order_release);
				}
			}
			
			// If it's < three-quarters full, add to the old one anyway so that we don't have to wait for the next table
			// to finish being allocated by another thread (and if we just finished allocating above, the condition will
			// always be true)
			if (newCount < (mainHash->capacity >> 1) + (mainHash->capacity >> 2)) {
				auto producer = static_cast<ImplicitProducer*>(recycle_or_create_producer(false));
				if (producer == nullptr) {
					implicitProducerHashCount.fetch_sub(1, std::memory_order_relaxed);
					return nullptr;
				}
				
#ifdef MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
				producer->threadExitListener.callback = &ConcurrentQueue::implicit_producer_thread_exited_callback;
				producer->threadExitListener.userData = producer;
				details::ThreadExitNotifier::subscribe(&producer->threadExitListener);
#endif
				
				auto index = hashedId;
				while (true) {
					index &= mainHash->capacity - 1u;
					auto empty = details::invalid_thread_id;
#ifdef MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
					auto reusable = details::invalid_thread_id2;
					if (mainHash->entries[index].key.compare_exchange_strong(reusable, id, std::memory_order_seq_cst, std::memory_order_relaxed)) {
						implicitProducerHashCount.fetch_sub(1, std::memory_order_relaxed);  // already counted as a used slot
						mainHash->entries[index].value = producer;
						break;
					}
#endif
					if (mainHash->entries[index].key.compare_exchange_strong(empty,    id, std::memory_order_seq_cst, std::memory_order_relaxed)) {
						mainHash->entries[index].value = producer;
						break;
					}
					++index;
				}
				return producer;
			}
			
			// Hmm, the old hash is quite full and somebody else is busy allocating a new one.
			// We need to wait for the allocating thread to finish (if it succeeds, we add, if not,
			// we try to allocate ourselves).
			mainHash = implicitProducerHash.load(std::memory_order_acquire);
		}
	}
	
#ifdef MOODYCAMEL_CPP11_THREAD_LOCAL_SUPPORTED
	void implicit_producer_thread_exited(ImplicitProducer* producer)
	{
		// Remove from hash
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODHASH
		debug::DebugLock lock(implicitProdMutex);
#endif
		auto hash = implicitProducerHash.load(std::memory_order_acquire);
		assert(hash != nullptr);		// The thread exit listener is only registered if we were added to a hash in the first place
		auto id = details::thread_id();
		auto hashedId = details::hash_thread_id(id);
		details::thread_id_t probedKey;
		
		// We need to traverse all the hashes just in case other threads aren't on the current one yet and are
		// trying to add an entry thinking there's a free slot (because they reused a producer)
		for (; hash != nullptr; hash = hash->prev) {
			auto index = hashedId;
			do {
				index &= hash->capacity - 1u;
				probedKey = id;
				if (hash->entries[index].key.compare_exchange_strong(probedKey, details::invalid_thread_id2, std::memory_order_seq_cst, std::memory_order_relaxed)) {
					break;
				}
				++index;
			} while (probedKey != details::invalid_thread_id);		// Can happen if the hash has changed but we weren't put back in it yet, or if we weren't added to this hash in the first place
		}
		
		// Mark the queue as being recyclable
		producer->inactive.store(true, std::memory_order_release);
	}
	
	static void implicit_producer_thread_exited_callback(void* userData)
	{
		auto producer = static_cast<ImplicitProducer*>(userData);
		auto queue = producer->parent;
		queue->implicit_producer_thread_exited(producer);
	}
#endif
	
	//////////////////////////////////
	// Utility functions
	//////////////////////////////////

	template<typename TAlign>
	static inline void* aligned_malloc(size_t size)
	{
		MOODYCAMEL_CONSTEXPR_IF (std::alignment_of<TAlign>::value <= std::alignment_of<details::max_align_t>::value)
			return (Traits::malloc)(size);
		else {
			size_t alignment = std::alignment_of<TAlign>::value;
			void* raw = (Traits::malloc)(size + alignment - 1 + sizeof(void*));
			if (!raw)
				return nullptr;
			char* ptr = details::align_for<TAlign>(reinterpret_cast<char*>(raw) + sizeof(void*));
			*(reinterpret_cast<void**>(ptr) - 1) = raw;
			return ptr;
		}
	}

	template<typename TAlign>
	static inline void aligned_free(void* ptr)
	{
		MOODYCAMEL_CONSTEXPR_IF (std::alignment_of<TAlign>::value <= std::alignment_of<details::max_align_t>::value)
			return (Traits::free)(ptr);
		else
			(Traits::free)(ptr ? *(reinterpret_cast<void**>(ptr) - 1) : nullptr);
	}

	template<typename U>
	static inline U* create_array(size_t count)
	{
		assert(count > 0);
		U* p = static_cast<U*>(aligned_malloc<U>(sizeof(U) * count));
		if (p == nullptr)
			return nullptr;

		for (size_t i = 0; i != count; ++i)
			new (p + i) U();
		return p;
	}

	template<typename U>
	static inline void destroy_array(U* p, size_t count)
	{
		if (p != nullptr) {
			assert(count > 0);
			for (size_t i = count; i != 0; )
				(p + --i)->~U();
		}
		aligned_free<U>(p);
	}

	template<typename U>
	static inline U* create()
	{
		void* p = aligned_malloc<U>(sizeof(U));
		return p != nullptr ? new (p) U : nullptr;
	}

	template<typename U, typename A1>
	static inline U* create(A1&& a1)
	{
		void* p = aligned_malloc<U>(sizeof(U));
		return p != nullptr ? new (p) U(std::forward<A1>(a1)) : nullptr;
	}

	template<typename U>
	static inline void destroy(U* p)
	{
		if (p != nullptr)
			p->~U();
		aligned_free<U>(p);
	}

private:
	std::atomic<ProducerBase*> producerListTail;
	std::atomic<std::uint32_t> producerCount;
	
	std::atomic<size_t> initialBlockPoolIndex;
	Block* initialBlockPool;
	size_t initialBlockPoolSize;
	
#ifndef MCDBGQ_USEDEBUGFREELIST
	FreeList<Block> freeList;
#else
	debug::DebugFreeList<Block> freeList;
#endif
	
	std::atomic<ImplicitProducerHash*> implicitProducerHash;
	std::atomic<size_t> implicitProducerHashCount;		// Number of slots logically used
	ImplicitProducerHash initialImplicitProducerHash;
	std::array<ImplicitProducerKVP, INITIAL_IMPLICIT_PRODUCER_HASH_SIZE> initialImplicitProducerHashEntries;
	std::atomic_flag implicitProducerHashResizeInProgress;
	
	std::atomic<std::uint32_t> nextExplicitConsumerId;
	std::atomic<std::uint32_t> globalExplicitConsumerOffset;
	
#ifdef MCDBGQ_NOLOCKFREE_IMPLICITPRODHASH
	debug::DebugMutex implicitProdMutex;
#endif
	
#ifdef MOODYCAMEL_QUEUE_INTERNAL_DEBUG
	std::atomic<ExplicitProducer*> explicitProducers;
	std::atomic<ImplicitProducer*> implicitProducers;
#endif
};


template<typename T, typename Traits>
ProducerToken::ProducerToken(ConcurrentQueue<T, Traits>& queue)
	: producer(queue.recycle_or_create_producer(true))
{
	if (producer != nullptr) {
		producer->token = this;
	}
}

template<typename T, typename Traits>
ProducerToken::ProducerToken(BlockingConcurrentQueue<T, Traits>& queue)
	: producer(reinterpret_cast<ConcurrentQueue<T, Traits>*>(&queue)->recycle_or_create_producer(true))
{
	if (producer != nullptr) {
		producer->token = this;
	}
}

template<typename T, typename Traits>
ConsumerToken::ConsumerToken(ConcurrentQueue<T, Traits>& queue)
	: itemsConsumedFromCurrent(0), currentProducer(nullptr), desiredProducer(nullptr)
{
	initialOffset = queue.nextExplicitConsumerId.fetch_add(1, std::memory_order_release);
	lastKnownGlobalOffset = static_cast<std::uint32_t>(-1);
}

template<typename T, typename Traits>
ConsumerToken::ConsumerToken(BlockingConcurrentQueue<T, Traits>& queue)
	: itemsConsumedFromCurrent(0), currentProducer(nullptr), desiredProducer(nullptr)
{
	initialOffset = reinterpret_cast<ConcurrentQueue<T, Traits>*>(&queue)->nextExplicitConsumerId.fetch_add(1, std::memory_order_release);
	lastKnownGlobalOffset = static_cast<std::uint32_t>(-1);
}

template<typename T, typename Traits>
inline void swap(ConcurrentQueue<T, Traits>& a, ConcurrentQueue<T, Traits>& b) MOODYCAMEL_NOEXCEPT
{
	a.swap(b);
}

inline void swap(ProducerToken& a, ProducerToken& b) MOODYCAMEL_NOEXCEPT
{
	a.swap(b);
}

inline void swap(ConsumerToken& a, ConsumerToken& b) MOODYCAMEL_NOEXCEPT
{
	a.swap(b);
}

template<typename T, typename Traits>
inline void swap(typename ConcurrentQueue<T, Traits>::ImplicitProducerKVP& a, typename ConcurrentQueue<T, Traits>::ImplicitProducerKVP& b) MOODYCAMEL_NOEXCEPT
{
	a.swap(b);
}

}

#if defined(_MSC_VER) && (!defined(_HAS_CXX17) || !_HAS_CXX17)
#pragma warning(pop)
#endif

#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
#pragma GCC diagnostic pop
#endif

// #include "moodycamel/blockingconcurrentqueue.h"
// Provides an efficient blocking version of moodycamel::ConcurrentQueue.
// ©2015-2020 Cameron Desrochers. Distributed under the terms of the simplified
// BSD license, available at the top of concurrentqueue.h.
// Also dual-licensed under the Boost Software License (see LICENSE.md)
// Uses Jeff Preshing's semaphore implementation (under the terms of its
// separate zlib license, see lightweightsemaphore.h).



// #include "concurrentqueue.h"

// #include "lightweightsemaphore.h"
// Provides an efficient implementation of a semaphore (LightweightSemaphore).
// This is an extension of Jeff Preshing's sempahore implementation (licensed 
// under the terms of its separate zlib license) that has been adapted and
// extended by Cameron Desrochers.



#include <cstddef> // For std::size_t
#include <atomic>
#include <type_traits> // For std::make_signed<T>

#if defined(_WIN32)
// Avoid including windows.h in a header; we only need a handful of
// items, so we'll redeclare them here (this is relatively safe since
// the API generally has to remain stable between Windows versions).
// I know this is an ugly hack but it still beats polluting the global
// namespace with thousands of generic names or adding a .cpp for nothing.
extern "C" {
	struct _SECURITY_ATTRIBUTES;
	__declspec(dllimport) void* __stdcall CreateSemaphoreW(_SECURITY_ATTRIBUTES* lpSemaphoreAttributes, long lInitialCount, long lMaximumCount, const wchar_t* lpName);
	__declspec(dllimport) int __stdcall CloseHandle(void* hObject);
	__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void* hHandle, unsigned long dwMilliseconds);
	__declspec(dllimport) int __stdcall ReleaseSemaphore(void* hSemaphore, long lReleaseCount, long* lpPreviousCount);
}
#elif defined(__MACH__)
#include <mach/mach.h>
#elif defined(__MVS__)
#include <zos-semaphore.h>
#elif defined(__unix__)
#include <semaphore.h>

#if defined(__GLIBC_PREREQ) && defined(_GNU_SOURCE)
#if __GLIBC_PREREQ(2,30)
#define MOODYCAMEL_LIGHTWEIGHTSEMAPHORE_MONOTONIC
#endif
#endif
#endif

namespace moodycamel
{
namespace details
{

// Code in the mpmc_sema namespace below is an adaptation of Jeff Preshing's
// portable + lightweight semaphore implementations, originally from
// https://github.com/preshing/cpp11-on-multicore/blob/master/common/sema.h
// LICENSE:
// Copyright (c) 2015 Jeff Preshing
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//	claim that you wrote the original software. If you use this software
//	in a product, an acknowledgement in the product documentation would be
//	appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//	misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
#if defined(_WIN32)
class Semaphore
{
private:
	void* m_hSema;
	
	Semaphore(const Semaphore& other) MOODYCAMEL_DELETE_FUNCTION;
	Semaphore& operator=(const Semaphore& other) MOODYCAMEL_DELETE_FUNCTION;

public:
	Semaphore(int initialCount = 0)
	{
		assert(initialCount >= 0);
		const long maxLong = 0x7fffffff;
		m_hSema = CreateSemaphoreW(nullptr, initialCount, maxLong, nullptr);
		assert(m_hSema);
	}

	~Semaphore()
	{
		CloseHandle(m_hSema);
	}

	bool wait()
	{
		const unsigned long infinite = 0xffffffff;
		return WaitForSingleObject(m_hSema, infinite) == 0;
	}
	
	bool try_wait()
	{
		return WaitForSingleObject(m_hSema, 0) == 0;
	}
	
	bool timed_wait(std::uint64_t usecs)
	{
		return WaitForSingleObject(m_hSema, (unsigned long)(usecs / 1000)) == 0;
	}

	void signal(int count = 1)
	{
		while (!ReleaseSemaphore(m_hSema, count, nullptr));
	}
};
#elif defined(__MACH__)
//---------------------------------------------------------
// Semaphore (Apple iOS and OSX)
// Can't use POSIX semaphores due to http://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html
//---------------------------------------------------------
class Semaphore
{
private:
	semaphore_t m_sema;

	Semaphore(const Semaphore& other) MOODYCAMEL_DELETE_FUNCTION;
	Semaphore& operator=(const Semaphore& other) MOODYCAMEL_DELETE_FUNCTION;

public:
	Semaphore(int initialCount = 0)
	{
		assert(initialCount >= 0);
		kern_return_t rc = semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialCount);
		assert(rc == KERN_SUCCESS);
		(void)rc;
	}

	~Semaphore()
	{
		semaphore_destroy(mach_task_self(), m_sema);
	}

	bool wait()
	{
		return semaphore_wait(m_sema) == KERN_SUCCESS;
	}
	
	bool try_wait()
	{
		return timed_wait(0);
	}
	
	bool timed_wait(std::uint64_t timeout_usecs)
	{
		mach_timespec_t ts;
		ts.tv_sec = static_cast<unsigned int>(timeout_usecs / 1000000);
		ts.tv_nsec = static_cast<int>((timeout_usecs % 1000000) * 1000);

		// added in OSX 10.10: https://developer.apple.com/library/prerelease/mac/documentation/General/Reference/APIDiffsMacOSX10_10SeedDiff/modules/Darwin.html
		kern_return_t rc = semaphore_timedwait(m_sema, ts);
		return rc == KERN_SUCCESS;
	}

	void signal()
	{
		while (semaphore_signal(m_sema) != KERN_SUCCESS);
	}

	void signal(int count)
	{
		while (count-- > 0)
		{
			while (semaphore_signal(m_sema) != KERN_SUCCESS);
		}
	}
};
#elif defined(__unix__) || defined(__MVS__)
//---------------------------------------------------------
// Semaphore (POSIX, Linux, zOS)
//---------------------------------------------------------
class Semaphore
{
private:
	sem_t m_sema;

	Semaphore(const Semaphore& other) MOODYCAMEL_DELETE_FUNCTION;
	Semaphore& operator=(const Semaphore& other) MOODYCAMEL_DELETE_FUNCTION;

public:
	Semaphore(int initialCount = 0)
	{
		assert(initialCount >= 0);
		int rc = sem_init(&m_sema, 0, static_cast<unsigned int>(initialCount));
		assert(rc == 0);
		(void)rc;
	}

	~Semaphore()
	{
		sem_destroy(&m_sema);
	}

	bool wait()
	{
		// http://stackoverflow.com/questions/2013181/gdb-causes-sem-wait-to-fail-with-eintr-error
		int rc;
		do {
			rc = sem_wait(&m_sema);
		} while (rc == -1 && errno == EINTR);
		return rc == 0;
	}

	bool try_wait()
	{
		int rc;
		do {
			rc = sem_trywait(&m_sema);
		} while (rc == -1 && errno == EINTR);
		return rc == 0;
	}

	bool timed_wait(std::uint64_t usecs)
	{
		struct timespec ts;
		const int usecs_in_1_sec = 1000000;
		const int nsecs_in_1_sec = 1000000000;
#ifdef MOODYCAMEL_LIGHTWEIGHTSEMAPHORE_MONOTONIC
		clock_gettime(CLOCK_MONOTONIC, &ts);
#else
		clock_gettime(CLOCK_REALTIME, &ts);
#endif
		ts.tv_sec += (time_t)(usecs / usecs_in_1_sec);
		ts.tv_nsec += (long)(usecs % usecs_in_1_sec) * 1000;
		// sem_timedwait bombs if you have more than 1e9 in tv_nsec
		// so we have to clean things up before passing it in
		if (ts.tv_nsec >= nsecs_in_1_sec) {
			ts.tv_nsec -= nsecs_in_1_sec;
			++ts.tv_sec;
		}

		int rc;
		do {
#ifdef MOODYCAMEL_LIGHTWEIGHTSEMAPHORE_MONOTONIC
			rc = sem_clockwait(&m_sema, CLOCK_MONOTONIC, &ts);
#else
			rc = sem_timedwait(&m_sema, &ts);
#endif
		} while (rc == -1 && errno == EINTR);
		return rc == 0;
	}

	void signal()
	{
		while (sem_post(&m_sema) == -1);
	}

	void signal(int count)
	{
		while (count-- > 0)
		{
			while (sem_post(&m_sema) == -1);
		}
	}
};
#else
#error Unsupported platform! (No semaphore wrapper available)
#endif

}	// end namespace details


//---------------------------------------------------------
// LightweightSemaphore
//---------------------------------------------------------
class LightweightSemaphore
{
public:
	typedef std::make_signed<std::size_t>::type ssize_t;

private:
	std::atomic<ssize_t> m_count;
	details::Semaphore m_sema;
	int m_maxSpins;

	bool waitWithPartialSpinning(std::int64_t timeout_usecs = -1)
	{
		ssize_t oldCount;
		int spin = m_maxSpins;
		while (--spin >= 0)
		{
			oldCount = m_count.load(std::memory_order_relaxed);
			if ((oldCount > 0) && m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order_acquire, std::memory_order_relaxed))
				return true;
			std::atomic_signal_fence(std::memory_order_acquire);	 // Prevent the compiler from collapsing the loop.
		}
		oldCount = m_count.fetch_sub(1, std::memory_order_acquire);
		if (oldCount > 0)
			return true;
		if (timeout_usecs < 0)
		{
			if (m_sema.wait())
				return true;
		}
		if (timeout_usecs > 0 && m_sema.timed_wait((std::uint64_t)timeout_usecs))
			return true;
		// At this point, we've timed out waiting for the semaphore, but the
		// count is still decremented indicating we may still be waiting on
		// it. So we have to re-adjust the count, but only if the semaphore
		// wasn't signaled enough times for us too since then. If it was, we
		// need to release the semaphore too.
		while (true)
		{
			oldCount = m_count.load(std::memory_order_acquire);
			if (oldCount >= 0 && m_sema.try_wait())
				return true;
			if (oldCount < 0 && m_count.compare_exchange_strong(oldCount, oldCount + 1, std::memory_order_relaxed, std::memory_order_relaxed))
				return false;
		}
	}

	ssize_t waitManyWithPartialSpinning(ssize_t max, std::int64_t timeout_usecs = -1)
	{
		assert(max > 0);
		ssize_t oldCount;
		int spin = m_maxSpins;
		while (--spin >= 0)
		{
			oldCount = m_count.load(std::memory_order_relaxed);
			if (oldCount > 0)
			{
				ssize_t newCount = oldCount > max ? oldCount - max : 0;
				if (m_count.compare_exchange_strong(oldCount, newCount, std::memory_order_acquire, std::memory_order_relaxed))
					return oldCount - newCount;
			}
			std::atomic_signal_fence(std::memory_order_acquire);
		}
		oldCount = m_count.fetch_sub(1, std::memory_order_acquire);
		if (oldCount <= 0)
		{
			if ((timeout_usecs == 0) || (timeout_usecs < 0 && !m_sema.wait()) || (timeout_usecs > 0 && !m_sema.timed_wait((std::uint64_t)timeout_usecs)))
			{
				while (true)
				{
					oldCount = m_count.load(std::memory_order_acquire);
					if (oldCount >= 0 && m_sema.try_wait())
						break;
					if (oldCount < 0 && m_count.compare_exchange_strong(oldCount, oldCount + 1, std::memory_order_relaxed, std::memory_order_relaxed))
						return 0;
				}
			}
		}
		if (max > 1)
			return 1 + tryWaitMany(max - 1);
		return 1;
	}

public:
	LightweightSemaphore(ssize_t initialCount = 0, int maxSpins = 10000) : m_count(initialCount), m_maxSpins(maxSpins)
	{
		assert(initialCount >= 0);
		assert(maxSpins >= 0);
	}

	bool tryWait()
	{
		ssize_t oldCount = m_count.load(std::memory_order_relaxed);
		while (oldCount > 0)
		{
			if (m_count.compare_exchange_weak(oldCount, oldCount - 1, std::memory_order_acquire, std::memory_order_relaxed))
				return true;
		}
		return false;
	}

	bool wait()
	{
		return tryWait() || waitWithPartialSpinning();
	}

	bool wait(std::int64_t timeout_usecs)
	{
		return tryWait() || waitWithPartialSpinning(timeout_usecs);
	}

	// Acquires between 0 and (greedily) max, inclusive
	ssize_t tryWaitMany(ssize_t max)
	{
		assert(max >= 0);
		ssize_t oldCount = m_count.load(std::memory_order_relaxed);
		while (oldCount > 0)
		{
			ssize_t newCount = oldCount > max ? oldCount - max : 0;
			if (m_count.compare_exchange_weak(oldCount, newCount, std::memory_order_acquire, std::memory_order_relaxed))
				return oldCount - newCount;
		}
		return 0;
	}

	// Acquires at least one, and (greedily) at most max
	ssize_t waitMany(ssize_t max, std::int64_t timeout_usecs)
	{
		assert(max >= 0);
		ssize_t result = tryWaitMany(max);
		if (result == 0 && max > 0)
			result = waitManyWithPartialSpinning(max, timeout_usecs);
		return result;
	}
	
	ssize_t waitMany(ssize_t max)
	{
		ssize_t result = waitMany(max, -1);
		assert(result > 0);
		return result;
	}

	void signal(ssize_t count = 1)
	{
		assert(count >= 0);
		ssize_t oldCount = m_count.fetch_add(count, std::memory_order_release);
		ssize_t toRelease = -oldCount < count ? -oldCount : count;
		if (toRelease > 0)
		{
			m_sema.signal((int)toRelease);
		}
	}
	
	std::size_t availableApprox() const
	{
		ssize_t count = m_count.load(std::memory_order_relaxed);
		return count > 0 ? static_cast<std::size_t>(count) : 0;
	}
};

}   // end namespace moodycamel


#include <type_traits>
#include <cerrno>
#include <memory>
#include <chrono>
#include <ctime>

namespace moodycamel
{
// This is a blocking version of the queue. It has an almost identical interface to
// the normal non-blocking version, with the addition of various wait_dequeue() methods
// and the removal of producer-specific dequeue methods.
template<typename T, typename Traits = ConcurrentQueueDefaultTraits>
class BlockingConcurrentQueue
{
private:
	typedef ::moodycamel::ConcurrentQueue<T, Traits> ConcurrentQueue;
	typedef ::moodycamel::LightweightSemaphore LightweightSemaphore;

public:
	typedef typename ConcurrentQueue::producer_token_t producer_token_t;
	typedef typename ConcurrentQueue::consumer_token_t consumer_token_t;
	
	typedef typename ConcurrentQueue::index_t index_t;
	typedef typename ConcurrentQueue::size_t size_t;
	typedef typename std::make_signed<size_t>::type ssize_t;
	
	static const size_t BLOCK_SIZE = ConcurrentQueue::BLOCK_SIZE;
	static const size_t EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD = ConcurrentQueue::EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD;
	static const size_t EXPLICIT_INITIAL_INDEX_SIZE = ConcurrentQueue::EXPLICIT_INITIAL_INDEX_SIZE;
	static const size_t IMPLICIT_INITIAL_INDEX_SIZE = ConcurrentQueue::IMPLICIT_INITIAL_INDEX_SIZE;
	static const size_t INITIAL_IMPLICIT_PRODUCER_HASH_SIZE = ConcurrentQueue::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE;
	static const std::uint32_t EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE = ConcurrentQueue::EXPLICIT_CONSUMER_CONSUMPTION_QUOTA_BEFORE_ROTATE;
	static const size_t MAX_SUBQUEUE_SIZE = ConcurrentQueue::MAX_SUBQUEUE_SIZE;
	
public:
	// Creates a queue with at least `capacity` element slots; note that the
	// actual number of elements that can be inserted without additional memory
	// allocation depends on the number of producers and the block size (e.g. if
	// the block size is equal to `capacity`, only a single block will be allocated
	// up-front, which means only a single producer will be able to enqueue elements
	// without an extra allocation -- blocks aren't shared between producers).
	// This method is not thread safe -- it is up to the user to ensure that the
	// queue is fully constructed before it starts being used by other threads (this
	// includes making the memory effects of construction visible, possibly with a
	// memory barrier).
	explicit BlockingConcurrentQueue(size_t capacity = 6 * BLOCK_SIZE)
		: inner(capacity), sema(create<LightweightSemaphore, ssize_t, int>(0, (int)Traits::MAX_SEMA_SPINS), &BlockingConcurrentQueue::template destroy<LightweightSemaphore>)
	{
		assert(reinterpret_cast<ConcurrentQueue*>((BlockingConcurrentQueue*)1) == &((BlockingConcurrentQueue*)1)->inner && "BlockingConcurrentQueue must have ConcurrentQueue as its first member");
		if (!sema) {
			MOODYCAMEL_THROW(std::bad_alloc());
		}
	}
	
	BlockingConcurrentQueue(size_t minCapacity, size_t maxExplicitProducers, size_t maxImplicitProducers)
		: inner(minCapacity, maxExplicitProducers, maxImplicitProducers), sema(create<LightweightSemaphore, ssize_t, int>(0, (int)Traits::MAX_SEMA_SPINS), &BlockingConcurrentQueue::template destroy<LightweightSemaphore>)
	{
		assert(reinterpret_cast<ConcurrentQueue*>((BlockingConcurrentQueue*)1) == &((BlockingConcurrentQueue*)1)->inner && "BlockingConcurrentQueue must have ConcurrentQueue as its first member");
		if (!sema) {
			MOODYCAMEL_THROW(std::bad_alloc());
		}
	}
	
	// Disable copying and copy assignment
	BlockingConcurrentQueue(BlockingConcurrentQueue const&) MOODYCAMEL_DELETE_FUNCTION;
	BlockingConcurrentQueue& operator=(BlockingConcurrentQueue const&) MOODYCAMEL_DELETE_FUNCTION;
	
	// Moving is supported, but note that it is *not* a thread-safe operation.
	// Nobody can use the queue while it's being moved, and the memory effects
	// of that move must be propagated to other threads before they can use it.
	// Note: When a queue is moved, its tokens are still valid but can only be
	// used with the destination queue (i.e. semantically they are moved along
	// with the queue itself).
	BlockingConcurrentQueue(BlockingConcurrentQueue&& other) MOODYCAMEL_NOEXCEPT
		: inner(std::move(other.inner)), sema(std::move(other.sema))
	{ }
	
	inline BlockingConcurrentQueue& operator=(BlockingConcurrentQueue&& other) MOODYCAMEL_NOEXCEPT
	{
		return swap_internal(other);
	}
	
	// Swaps this queue's state with the other's. Not thread-safe.
	// Swapping two queues does not invalidate their tokens, however
	// the tokens that were created for one queue must be used with
	// only the swapped queue (i.e. the tokens are tied to the
	// queue's movable state, not the object itself).
	inline void swap(BlockingConcurrentQueue& other) MOODYCAMEL_NOEXCEPT
	{
		swap_internal(other);
	}
	
private:
	BlockingConcurrentQueue& swap_internal(BlockingConcurrentQueue& other)
	{
		if (this == &other) {
			return *this;
		}
		
		inner.swap(other.inner);
		sema.swap(other.sema);
		return *this;
	}
	
public:
	// Enqueues a single item (by copying it).
	// Allocates memory if required. Only fails if memory allocation fails (or implicit
	// production is disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE is 0,
	// or Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Thread-safe.
	inline bool enqueue(T const& item)
	{
		if ((details::likely)(inner.enqueue(item))) {
			sema->signal();
			return true;
		}
		return false;
	}
	
	// Enqueues a single item (by moving it, if possible).
	// Allocates memory if required. Only fails if memory allocation fails (or implicit
	// production is disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE is 0,
	// or Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Thread-safe.
	inline bool enqueue(T&& item)
	{
		if ((details::likely)(inner.enqueue(std::move(item)))) {
			sema->signal();
			return true;
		}
		return false;
	}
	
	// Enqueues a single item (by copying it) using an explicit producer token.
	// Allocates memory if required. Only fails if memory allocation fails (or
	// Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Thread-safe.
	inline bool enqueue(producer_token_t const& token, T const& item)
	{
		if ((details::likely)(inner.enqueue(token, item))) {
			sema->signal();
			return true;
		}
		return false;
	}
	
	// Enqueues a single item (by moving it, if possible) using an explicit producer token.
	// Allocates memory if required. Only fails if memory allocation fails (or
	// Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Thread-safe.
	inline bool enqueue(producer_token_t const& token, T&& item)
	{
		if ((details::likely)(inner.enqueue(token, std::move(item)))) {
			sema->signal();
			return true;
		}
		return false;
	}
	
	// Enqueues several items.
	// Allocates memory if required. Only fails if memory allocation fails (or
	// implicit production is disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE
	// is 0, or Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Note: Use std::make_move_iterator if the elements should be moved instead of copied.
	// Thread-safe.
	template<typename It>
	inline bool enqueue_bulk(It itemFirst, size_t count)
	{
		if ((details::likely)(inner.enqueue_bulk(std::forward<It>(itemFirst), count))) {
			sema->signal((LightweightSemaphore::ssize_t)(ssize_t)count);
			return true;
		}
		return false;
	}
	
	// Enqueues several items using an explicit producer token.
	// Allocates memory if required. Only fails if memory allocation fails
	// (or Traits::MAX_SUBQUEUE_SIZE has been defined and would be surpassed).
	// Note: Use std::make_move_iterator if the elements should be moved
	// instead of copied.
	// Thread-safe.
	template<typename It>
	inline bool enqueue_bulk(producer_token_t const& token, It itemFirst, size_t count)
	{
		if ((details::likely)(inner.enqueue_bulk(token, std::forward<It>(itemFirst), count))) {
			sema->signal((LightweightSemaphore::ssize_t)(ssize_t)count);
			return true;
		}
		return false;
	}
	
	// Enqueues a single item (by copying it).
	// Does not allocate memory. Fails if not enough room to enqueue (or implicit
	// production is disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE
	// is 0).
	// Thread-safe.
	inline bool try_enqueue(T const& item)
	{
		if (inner.try_enqueue(item)) {
			sema->signal();
			return true;
		}
		return false;
	}
	
	// Enqueues a single item (by moving it, if possible).
	// Does not allocate memory (except for one-time implicit producer).
	// Fails if not enough room to enqueue (or implicit production is
	// disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE is 0).
	// Thread-safe.
	inline bool try_enqueue(T&& item)
	{
		if (inner.try_enqueue(std::move(item))) {
			sema->signal();
			return true;
		}
		return false;
	}
	
	// Enqueues a single item (by copying it) using an explicit producer token.
	// Does not allocate memory. Fails if not enough room to enqueue.
	// Thread-safe.
	inline bool try_enqueue(producer_token_t const& token, T const& item)
	{
		if (inner.try_enqueue(token, item)) {
			sema->signal();
			return true;
		}
		return false;
	}
	
	// Enqueues a single item (by moving it, if possible) using an explicit producer token.
	// Does not allocate memory. Fails if not enough room to enqueue.
	// Thread-safe.
	inline bool try_enqueue(producer_token_t const& token, T&& item)
	{
		if (inner.try_enqueue(token, std::move(item))) {
			sema->signal();
			return true;
		}
		return false;
	}
	
	// Enqueues several items.
	// Does not allocate memory (except for one-time implicit producer).
	// Fails if not enough room to enqueue (or implicit production is
	// disabled because Traits::INITIAL_IMPLICIT_PRODUCER_HASH_SIZE is 0).
	// Note: Use std::make_move_iterator if the elements should be moved
	// instead of copied.
	// Thread-safe.
	template<typename It>
	inline bool try_enqueue_bulk(It itemFirst, size_t count)
	{
		if (inner.try_enqueue_bulk(std::forward<It>(itemFirst), count)) {
			sema->signal((LightweightSemaphore::ssize_t)(ssize_t)count);
			return true;
		}
		return false;
	}
	
	// Enqueues several items using an explicit producer token.
	// Does not allocate memory. Fails if not enough room to enqueue.
	// Note: Use std::make_move_iterator if the elements should be moved
	// instead of copied.
	// Thread-safe.
	template<typename It>
	inline bool try_enqueue_bulk(producer_token_t const& token, It itemFirst, size_t count)
	{
		if (inner.try_enqueue_bulk(token, std::forward<It>(itemFirst), count)) {
			sema->signal((LightweightSemaphore::ssize_t)(ssize_t)count);
			return true;
		}
		return false;
	}
	
	
	// Attempts to dequeue from the queue.
	// Returns false if all producer streams appeared empty at the time they
	// were checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename U>
	inline bool try_dequeue(U& item)
	{
		if (sema->tryWait()) {
			while (!inner.try_dequeue(item)) {
				continue;
			}
			return true;
		}
		return false;
	}
	
	// Attempts to dequeue from the queue using an explicit consumer token.
	// Returns false if all producer streams appeared empty at the time they
	// were checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename U>
	inline bool try_dequeue(consumer_token_t& token, U& item)
	{
		if (sema->tryWait()) {
			while (!inner.try_dequeue(token, item)) {
				continue;
			}
			return true;
		}
		return false;
	}
	
	// Attempts to dequeue several elements from the queue.
	// Returns the number of items actually dequeued.
	// Returns 0 if all producer streams appeared empty at the time they
	// were checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename It>
	inline size_t try_dequeue_bulk(It itemFirst, size_t max)
	{
		size_t count = 0;
		max = (size_t)sema->tryWaitMany((LightweightSemaphore::ssize_t)(ssize_t)max);
		while (count != max) {
			count += inner.template try_dequeue_bulk<It&>(itemFirst, max - count);
		}
		return count;
	}
	
	// Attempts to dequeue several elements from the queue using an explicit consumer token.
	// Returns the number of items actually dequeued.
	// Returns 0 if all producer streams appeared empty at the time they
	// were checked (so, the queue is likely but not guaranteed to be empty).
	// Never allocates. Thread-safe.
	template<typename It>
	inline size_t try_dequeue_bulk(consumer_token_t& token, It itemFirst, size_t max)
	{
		size_t count = 0;
		max = (size_t)sema->tryWaitMany((LightweightSemaphore::ssize_t)(ssize_t)max);
		while (count != max) {
			count += inner.template try_dequeue_bulk<It&>(token, itemFirst, max - count);
		}
		return count;
	}
	
	
	
	// Blocks the current thread until there's something to dequeue, then
	// dequeues it.
	// Never allocates. Thread-safe.
	template<typename U>
	inline void wait_dequeue(U& item)
	{
		while (!sema->wait()) {
			continue;
		}
		while (!inner.try_dequeue(item)) {
			continue;
		}
	}

	// Blocks the current thread until either there's something to dequeue
	// or the timeout (specified in microseconds) expires. Returns false
	// without setting `item` if the timeout expires, otherwise assigns
	// to `item` and returns true.
	// Using a negative timeout indicates an indefinite timeout,
	// and is thus functionally equivalent to calling wait_dequeue.
	// Never allocates. Thread-safe.
	template<typename U>
	inline bool wait_dequeue_timed(U& item, std::int64_t timeout_usecs)
	{
		if (!sema->wait(timeout_usecs)) {
			return false;
		}
		while (!inner.try_dequeue(item)) {
			continue;
		}
		return true;
	}
    
    // Blocks the current thread until either there's something to dequeue
	// or the timeout expires. Returns false without setting `item` if the
    // timeout expires, otherwise assigns to `item` and returns true.
	// Never allocates. Thread-safe.
	template<typename U, typename Rep, typename Period>
	inline bool wait_dequeue_timed(U& item, std::chrono::duration<Rep, Period> const& timeout)
    {
        return wait_dequeue_timed(item, std::chrono::duration_cast<std::chrono::microseconds>(timeout).count());
    }
	
	// Blocks the current thread until there's something to dequeue, then
	// dequeues it using an explicit consumer token.
	// Never allocates. Thread-safe.
	template<typename U>
	inline void wait_dequeue(consumer_token_t& token, U& item)
	{
		while (!sema->wait()) {
			continue;
		}
		while (!inner.try_dequeue(token, item)) {
			continue;
		}
	}
	
	// Blocks the current thread until either there's something to dequeue
	// or the timeout (specified in microseconds) expires. Returns false
	// without setting `item` if the timeout expires, otherwise assigns
	// to `item` and returns true.
	// Using a negative timeout indicates an indefinite timeout,
	// and is thus functionally equivalent to calling wait_dequeue.
	// Never allocates. Thread-safe.
	template<typename U>
	inline bool wait_dequeue_timed(consumer_token_t& token, U& item, std::int64_t timeout_usecs)
	{
		if (!sema->wait(timeout_usecs)) {
			return false;
		}
		while (!inner.try_dequeue(token, item)) {
			continue;
		}
		return true;
	}
    
    // Blocks the current thread until either there's something to dequeue
	// or the timeout expires. Returns false without setting `item` if the
    // timeout expires, otherwise assigns to `item` and returns true.
	// Never allocates. Thread-safe.
	template<typename U, typename Rep, typename Period>
	inline bool wait_dequeue_timed(consumer_token_t& token, U& item, std::chrono::duration<Rep, Period> const& timeout)
    {
        return wait_dequeue_timed(token, item, std::chrono::duration_cast<std::chrono::microseconds>(timeout).count());
    }
	
	// Attempts to dequeue several elements from the queue.
	// Returns the number of items actually dequeued, which will
	// always be at least one (this method blocks until the queue
	// is non-empty) and at most max.
	// Never allocates. Thread-safe.
	template<typename It>
	inline size_t wait_dequeue_bulk(It itemFirst, size_t max)
	{
		size_t count = 0;
		max = (size_t)sema->waitMany((LightweightSemaphore::ssize_t)(ssize_t)max);
		while (count != max) {
			count += inner.template try_dequeue_bulk<It&>(itemFirst, max - count);
		}
		return count;
	}
	
	// Attempts to dequeue several elements from the queue.
	// Returns the number of items actually dequeued, which can
	// be 0 if the timeout expires while waiting for elements,
	// and at most max.
	// Using a negative timeout indicates an indefinite timeout,
	// and is thus functionally equivalent to calling wait_dequeue_bulk.
	// Never allocates. Thread-safe.
	template<typename It>
	inline size_t wait_dequeue_bulk_timed(It itemFirst, size_t max, std::int64_t timeout_usecs)
	{
		size_t count = 0;
		max = (size_t)sema->waitMany((LightweightSemaphore::ssize_t)(ssize_t)max, timeout_usecs);
		while (count != max) {
			count += inner.template try_dequeue_bulk<It&>(itemFirst, max - count);
		}
		return count;
	}
    
    // Attempts to dequeue several elements from the queue.
	// Returns the number of items actually dequeued, which can
	// be 0 if the timeout expires while waiting for elements,
	// and at most max.
	// Never allocates. Thread-safe.
	template<typename It, typename Rep, typename Period>
	inline size_t wait_dequeue_bulk_timed(It itemFirst, size_t max, std::chrono::duration<Rep, Period> const& timeout)
    {
        return wait_dequeue_bulk_timed<It&>(itemFirst, max, std::chrono::duration_cast<std::chrono::microseconds>(timeout).count());
    }
	
	// Attempts to dequeue several elements from the queue using an explicit consumer token.
	// Returns the number of items actually dequeued, which will
	// always be at least one (this method blocks until the queue
	// is non-empty) and at most max.
	// Never allocates. Thread-safe.
	template<typename It>
	inline size_t wait_dequeue_bulk(consumer_token_t& token, It itemFirst, size_t max)
	{
		size_t count = 0;
		max = (size_t)sema->waitMany((LightweightSemaphore::ssize_t)(ssize_t)max);
		while (count != max) {
			count += inner.template try_dequeue_bulk<It&>(token, itemFirst, max - count);
		}
		return count;
	}
	
	// Attempts to dequeue several elements from the queue using an explicit consumer token.
	// Returns the number of items actually dequeued, which can
	// be 0 if the timeout expires while waiting for elements,
	// and at most max.
	// Using a negative timeout indicates an indefinite timeout,
	// and is thus functionally equivalent to calling wait_dequeue_bulk.
	// Never allocates. Thread-safe.
	template<typename It>
	inline size_t wait_dequeue_bulk_timed(consumer_token_t& token, It itemFirst, size_t max, std::int64_t timeout_usecs)
	{
		size_t count = 0;
		max = (size_t)sema->waitMany((LightweightSemaphore::ssize_t)(ssize_t)max, timeout_usecs);
		while (count != max) {
			count += inner.template try_dequeue_bulk<It&>(token, itemFirst, max - count);
		}
		return count;
	}
	
	// Attempts to dequeue several elements from the queue using an explicit consumer token.
	// Returns the number of items actually dequeued, which can
	// be 0 if the timeout expires while waiting for elements,
	// and at most max.
	// Never allocates. Thread-safe.
	template<typename It, typename Rep, typename Period>
	inline size_t wait_dequeue_bulk_timed(consumer_token_t& token, It itemFirst, size_t max, std::chrono::duration<Rep, Period> const& timeout)
    {
        return wait_dequeue_bulk_timed<It&>(token, itemFirst, max, std::chrono::duration_cast<std::chrono::microseconds>(timeout).count());
    }
	
	
	// Returns an estimate of the total number of elements currently in the queue. This
	// estimate is only accurate if the queue has completely stabilized before it is called
	// (i.e. all enqueue and dequeue operations have completed and their memory effects are
	// visible on the calling thread, and no further operations start while this method is
	// being called).
	// Thread-safe.
	inline size_t size_approx() const
	{
		return (size_t)sema->availableApprox();
	}
	
	
	// Returns true if the underlying atomic variables used by
	// the queue are lock-free (they should be on most platforms).
	// Thread-safe.
	static constexpr bool is_lock_free()
	{
		return ConcurrentQueue::is_lock_free();
	}
	

private:
	template<typename U, typename A1, typename A2>
	static inline U* create(A1&& a1, A2&& a2)
	{
		void* p = (Traits::malloc)(sizeof(U));
		return p != nullptr ? new (p) U(std::forward<A1>(a1), std::forward<A2>(a2)) : nullptr;
	}
	
	template<typename U>
	static inline void destroy(U* p)
	{
		if (p != nullptr) {
			p->~U();
		}
		(Traits::free)(p);
	}
	
private:
	ConcurrentQueue inner;
	std::unique_ptr<LightweightSemaphore, void (*)(LightweightSemaphore*)> sema;
};


template<typename T, typename Traits>
inline void swap(BlockingConcurrentQueue<T, Traits>& a, BlockingConcurrentQueue<T, Traits>& b) MOODYCAMEL_NOEXCEPT
{
	a.swap(b);
}

}	// end namespace moodycamel


// #include "log_types.hpp"


/**
 * @file log_types.hpp
 * @brief Core type definitions and constants for the logging system
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <cstdint>
#include <array>
#include <string>
#include <algorithm>
#include <chrono>

namespace slwoggy {

// Buffer pool constants
inline  constexpr size_t BUFFER_POOL_SIZE        = 128 * 1024;              // Number of pre-allocated buffers
inline  constexpr size_t MAX_STACK_BUFFER_SIZE   = 8 * 1024;                // Maximum stack buffer size for log messages
inline  constexpr size_t MAX_BATCH_SIZE          = 16 * 1024;               // Maximum buffers to dequeue in one batch
inline  constexpr size_t MAX_DISPATCH_QUEUE_SIZE = 8 * 1024;                // Maximum size of the dispatch queue

// Log buffer constants
inline constexpr size_t LOG_BUFFER_SIZE       = 2048;
inline constexpr size_t METADATA_RESERVE      = 256; // Reserve bytes for structured metadata
inline constexpr uint32_t MAX_STRUCTURED_KEYS = 256; // Maximum structured keys

// JSON formatting constants
inline  constexpr size_t UNICODE_ESCAPE_SIZE   = 7;   // \uXXXX + null terminator
inline  constexpr size_t UNICODE_ESCAPE_CHARS  = 6;   // \uXXXX
inline  constexpr size_t TIMESTAMP_BUFFER_SIZE = 256; // Buffer for timestamp formatting
inline  constexpr size_t LINE_BUFFER_SIZE      = 64;  // Buffer for line number formatting

// Metrics collection configuration
// Define these before including log.hpp to enable metrics collection:
// #define LOG_COLLECT_BUFFER_POOL_METRICS 1 // Enable buffer pool statistics
// #define LOG_COLLECT_DISPATCHER_METRICS  1 // Enable dispatcher statistics
// #define LOG_COLLECT_STRUCTURED_METRICS  1 // Enable structured logging statistics
// #define LOG_COLLECT_DISPATCHER_MSG_RATE 1 // Enable sliding window message rate (requires LOG_COLLECT_DISPATCHER_METRICS)

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
inline const char *log_level_names[] = {
    "TRACE",
    "DEBUG",
    "INFO ",
    "WARN ",
    "ERROR",
    "FATAL"
};

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

// Platform-specific fast timing utilities
#ifdef __APPLE__
    #include <mach/mach_time.h>
    
    inline std::chrono::steady_clock::time_point log_fast_timestamp()
    {
        static struct {
            mach_timebase_info_data_t timebase;
            bool initialized = false;
        } info;
        
        if (!info.initialized) {
            mach_timebase_info(&info.timebase);
            info.initialized = true;
        }
        
        uint64_t mach_time = mach_absolute_time();
        uint64_t nanos = mach_time * info.timebase.numer / info.timebase.denom;
        
        auto duration = std::chrono::nanoseconds(nanos);
        return std::chrono::steady_clock::time_point(duration);
    }
    
#elif defined(__linux__)
    #include <time.h>
    
    inline std::chrono::steady_clock::time_point log_fast_timestamp()
    {
        struct timespec ts;
        // Use CLOCK_MONOTONIC_COARSE for speed (1-4ms resolution)
        // Change to CLOCK_MONOTONIC for microsecond precision
        clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
        
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
        auto duration = std::chrono::milliseconds(ticks);
        return std::chrono::steady_clock::time_point(duration);
    }
    
#else
    // Fallback to standard chrono
    inline std::chrono::steady_clock::time_point log_fast_timestamp()
    {
        return std::chrono::steady_clock::now();
    }
#endif

} // namespace slwoggy

// #include "log_site.hpp"


/**
 * @file log_site.hpp
 * @brief Log site registration and runtime control
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <deque>
#include <mutex>
#include <cstring>
// #include "log_types.hpp"


namespace slwoggy {

/**
 * @brief Descriptor for a single LOG() macro invocation site
 *
 * This structure captures compile-time information about each LOG() call site
 * in the codebase. Instances are created via static initialization within the
 * LOG() macro, ensuring one registration per unique call site.
 */
struct log_site_descriptor
{
    const char *file;     ///< Source file path (from SOURCE_FILE_NAME)
    int line;             ///< Line number where LOG() was invoked
    const char *function; ///< Function name (currently unused, always "unknown")
    log_level min_level;  ///< The log level specified in LOG(level)
};

/**
 * @brief Global registry of all LOG() call sites in the application
 *
 * This registry automatically collects information about every LOG() macro
 * invocation that survives compile-time filtering (level >= GLOBAL_MIN_LOG_LEVEL).
 * The registry serves multiple purposes:
 * - Tracks all active logging locations for analysis/debugging
 * - Calculates the maximum filename length for aligned log output
 * - Enables potential future features like per-site dynamic control
 *
 * Registration happens automatically via static initialization, ensuring
 * thread-safe, one-time registration per call site with zero runtime overhead
 * for disabled log levels.
 */
struct log_site_registry
{
    /**
     * @brief Get the vector of all registered log sites
     * @return Reference to the static deque containing all log site descriptors
     * @note Thread-safe access requires external synchronization via registry_mutex()
     * @note The deque is used to ensure pointer stability across the application lifetime
     */
    static std::deque<log_site_descriptor> &sites()
    {
        static std::deque<log_site_descriptor> s;
        return s;
    }

    /**
     * @brief Register a new log site
     * @param file Source file path (typically SOURCE_FILE_NAME)
     * @param line Line number of the LOG() invocation
     * @param min_level The log level specified in LOG(level)
     * @param function Optional function name (currently unused)
     *
     * This method is called automatically by the LOG() macro during static
     * initialization. It also updates the longest filename tracking for
     * proper log alignment.
     */
    static auto &register_site(const char *file, int line, log_level min_level, const char *function)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        sites().emplace_back(log_site_descriptor{file, line, function ? function : "unknown", min_level});

        if (file && std::strlen(file) > longest_file()) { longest_file() = static_cast<int32_t>(std::strlen(file)); }
        return sites().back();
    }

    /**
     * @brief Find a specific log site by file and line
     * @param file Source file path to search for
     * @param line Line number to search for
     * @return Pointer to the log site descriptor if found, nullptr otherwise
     */
    static const log_site_descriptor *find_site(const char *file, int line)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        for (const auto &site : sites())
        {
            if (std::strcmp(site.file, file) == 0 && site.line == line) { return &site; }
        }
        return nullptr;
    }

    /**
     * @brief Clear all registered log sites
     * @note Primarily for testing; production code rarely needs this
     */
    static void clear()
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        sites().clear();
    }

    /**
     * @brief Get the length of the longest registered filename
     * @return Maximum filename length among all registered log sites
     *
     * This value is used by the logging system to properly align log output,
     * ensuring that all log messages have consistent formatting regardless
     * of source file path length.
     */
    static int32_t &longest_file()
    {
        static int32_t longest_file = 0;
        return longest_file;
    }

    /**
     * @brief Set the log level for a specific site
     * @param file Source file path
     * @param line Line number
     * @param level New log level for the site
     * @return true if site was found and updated, false otherwise
     */
    static bool set_site_level(const char *file, int line, log_level level)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        for (auto &site : sites())
        {
            if (std::strcmp(site.file, file) == 0 && site.line == line)
            {
                site.min_level = level;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Get the log level for a specific site
     * @param file Source file path
     * @param line Line number
     * @return The site's log level if found, log_level::nolog if not found
     */
    static log_level get_site_level(const char *file, int line)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        for (const auto &site : sites())
        {
            if (std::strcmp(site.file, file) == 0 && site.line == line) { return site.min_level; }
        }
        return log_level::nolog;
    }

    /**
     * @brief Set log level for all sites in a specific file
     * @param file Source file path (can include wildcards)
     * @param level New log level
     * @return Number of sites updated
     */
    static size_t set_file_level(const char *file, log_level level)
    {
        if (!file) return 0;

        std::lock_guard<std::mutex> lock(registry_mutex());
        size_t count = 0;

        std::string file_pattern(file);
        bool has_wildcard = (file_pattern.find('*') != std::string::npos);

        if (has_wildcard)
        {
            // Simple wildcard matching
            bool starts_with = (file_pattern.back() == '*');
            bool ends_with   = (file_pattern.front() == '*');

            std::string pattern = file_pattern;
            if (starts_with) pattern.pop_back();
            if (ends_with) pattern.erase(0, 1);

            for (auto &site : sites())
            {
                std::string site_file(site.file);
                bool match = false;

                if (starts_with && ends_with) { match = (site_file.find(pattern) != std::string::npos); }
                else if (starts_with) { match = (site_file.substr(0, pattern.length()) == pattern); }
                else if (ends_with)
                {
                    match = (site_file.length() >= pattern.length() &&
                             site_file.substr(site_file.length() - pattern.length()) == pattern);
                }

                if (match)
                {
                    site.min_level = level;
                    count++;
                }
            }
        }
        else
        {
            // Exact match
            for (auto &site : sites())
            {
                if (std::strcmp(site.file, file) == 0)
                {
                    site.min_level = level;
                    count++;
                }
            }
        }

        return count;
    }

    /**
     * @brief Set log level for all registered sites
     * @param level New log level
     */
    static void set_all_sites_level(log_level level)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        for (auto &site : sites()) { site.min_level = level; }
    }

  private:
    // Add a mutex to protect all shared static data
    static std::mutex &registry_mutex()
    {
        static std::mutex m;
        return m;
    }
};

} // namespace slwoggy
// #include "log_module.hpp"


/**
 * @file log_module.hpp
 * @brief Module-based log level control and registration
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstring>
#include <cctype>
#include <algorithm>

// #include "log_types.hpp"


namespace slwoggy {

/**
 * @brief Internal module configuration data
 *
 * Holds the runtime state for a logging module, including its name
 * and current log level. The level is atomic to allow thread-safe
 * runtime adjustments.
 */
struct log_module_info_detail
{
    const char *name = "generic";                       ///< Module name (owned by registry for non-generic modules)
    std::atomic<log_level> level{GLOBAL_MIN_LOG_LEVEL}; ///< Current minimum log level for this module
};

/**
 * @brief Module information handle used by LOG() macro
 *
 * This lightweight structure holds a pointer to the actual module
 * configuration. Each compilation unit has its own static instance
 * that can be reassigned to different modules via LOG_MODULE_NAME().
 */
struct log_module_info
{
    log_module_info_detail *detail; ///< Pointer to shared module configuration
};

/**
 * @brief Registry for managing shared module configurations
 *
 * The module registry provides a centralized system for managing log modules across
 * the application. Each module can have its own runtime-adjustable log level,
 * allowing fine-grained control over logging verbosity by subsystem.
 *
 * Features:
 * - Thread-safe module registration and lookup
 * - "generic" module as the default for all compilation units
 * - Lazy module creation on first use
 * - Shared module instances across compilation units with the same name
 *
 * Usage:
 * - Modules are automatically created when referenced via LOG_MODULE_NAME()
 * - Module names are case-sensitive and should follow a consistent naming scheme
 * - The "generic" module is special and always exists with default settings
 *
 * Example module names:
 * - "network", "database", "ui", "physics", "audio"
 * - "subsystem::component" for hierarchical organization
 */
class log_module_registry
{
  private:
    // Default "generic" module - lives forever
    log_module_info_detail generic_module_{"generic", GLOBAL_MIN_LOG_LEVEL};

    // Registry of all modules by name
    std::unordered_map<std::string_view, std::unique_ptr<log_module_info_detail>> modules_;
    mutable std::shared_mutex mutex_; // Allow concurrent reads

    log_module_registry()
    {
        // Pre-register the generic module (with nullptr since we use the member directly)
        modules_.emplace("generic", nullptr);
    }

  public:
    static log_module_registry &instance()
    {
        static log_module_registry inst;
        return inst;
    }

    log_module_info_detail *get_module(const char *name)
    {
        // Fast path for "generic"
        if (std::strcmp(name, "generic") == 0) { return &generic_module_; }

        std::string_view name_view(name);

        // Try to find existing module (read lock)
        {
            std::shared_lock lock(mutex_);
            auto it = modules_.find(name_view);
            if (it != modules_.end()) { return it->second ? it->second.get() : &generic_module_; }
        }

        // Create new module (write lock)
        {
            std::unique_lock lock(mutex_);
            // Double-check in case another thread created it
            auto it = modules_.find(name_view);
            if (it != modules_.end()) { return it->second ? it->second.get() : &generic_module_; }

            // Create new module with same level as generic
            auto new_module   = std::make_unique<log_module_info_detail>();
            new_module->name  = strdup(name); // Need to own the string
            new_module->level = generic_module_.level.load();

            auto *ptr = new_module.get();
            // Use the owned string as the key
            modules_.emplace(std::string_view(new_module->name), std::move(new_module));
            return ptr;
        }
    }

    // Get generic module for initialization
    log_module_info_detail *get_generic() { return &generic_module_; }

    std::vector<const log_module_info_detail *> get_all_modules() const
    {
        std::shared_lock lock(mutex_);
        std::vector<const log_module_info_detail *> result;
        result.reserve(modules_.size());

        for (auto &[name, module] : modules_)
        {
            // No const_cast needed!
            result.push_back(module ? module.get() : &generic_module_);
        }
        return result;
    }

    /**
     * @brief Set the log level for a specific module
     * @param name Module name
     * @param level New log level
     *
     * If the module doesn't exist, it will be created with the specified level.
     */
    void set_module_level(const char *name, log_level level)
    {
        auto *module = get_module(name);
        if (module) { module->level.store(level, std::memory_order_relaxed); }
    }

    /**
     * @brief Get the current log level for a module
     * @param name Module name
     * @return Current log level, or GLOBAL_MIN_LOG_LEVEL if module doesn't exist
     */
    log_level get_module_level(const char *name) const
    {
        std::string_view name_view(name);

        // Fast path for generic
        if (name_view == "generic") { return generic_module_.level.load(std::memory_order_relaxed); }

        std::shared_lock lock(mutex_);
        auto it = modules_.find(name_view);
        if (it != modules_.end() && it->second) { return it->second->level.load(std::memory_order_relaxed); }

        return GLOBAL_MIN_LOG_LEVEL;
    }

    /**
     * @brief Set all modules to the same log level
     * @param level New log level for all modules
     */
    void set_all_modules_level(log_level level)
    {
        // Set generic module
        generic_module_.level.store(level, std::memory_order_relaxed);

        // Set all other modules
        std::shared_lock lock(mutex_);
        for (auto &[name, module] : modules_)
        {
            if (module) { module->level.store(level, std::memory_order_relaxed); }
        }
    }

    /**
     * @brief Reset all modules to GLOBAL_MIN_LOG_LEVEL
     */
    void reset_all_modules_level() { set_all_modules_level(GLOBAL_MIN_LOG_LEVEL); }

    /**
     * @brief Configure logging levels from a string specification
     * @param config Configuration string
     * @return true if configuration was valid, false otherwise
     *
     * Format examples:
     * - "info" - Set all modules to info level
     * - "network=debug,database=warn" - Set specific modules
     * - "info,network=debug" - Set default to info, network to debug
     * - "*=warn,network=debug" - Set all to warn, then network to debug
     *
     * Module names can include wildcards:
     * - "net*=debug" - All modules starting with "net"
     * - "*worker=info" - All modules ending with "worker"
     */
    bool configure_from_string(const char *config)
    {
        if (!config) return false;

        std::string config_str(config);
        if (config_str.empty()) return false;

        // Remove whitespace
        config_str.erase(std::remove_if(config_str.begin(), config_str.end(), ::isspace), config_str.end());

        // Split by comma
        size_t pos = 0;
        while (pos < config_str.length())
        {
            size_t comma_pos = config_str.find(',', pos);
            if (comma_pos == std::string::npos) comma_pos = config_str.length();

            std::string part = config_str.substr(pos, comma_pos - pos);
            pos              = comma_pos + 1;

            if (part.empty()) continue;

            // Check if it's a module=level pair
            size_t eq_pos = part.find('=');
            if (eq_pos == std::string::npos)
            {
                // No equals sign - set global level
                log_level level = log_level_from_string(part.c_str());
                if (level == log_level::nolog && part != "nolog" && part != "off")
                {
                    return false; // Invalid level
                }
                set_all_modules_level(level);
            }
            else
            {
                // Module=level pair
                std::string module_pattern = part.substr(0, eq_pos);
                std::string level_str      = part.substr(eq_pos + 1);

                if (module_pattern.empty() || level_str.empty()) return false;

                log_level level = log_level_from_string(level_str.c_str());
                if (level == log_level::nolog && level_str != "nolog" && level_str != "off")
                {
                    return false; // Invalid level
                }

                // Handle wildcards
                if (module_pattern == "*")
                {
                    // Set all modules
                    set_all_modules_level(level);
                }
                else if (module_pattern.find('*') != std::string::npos)
                {
                    // Wildcard matching - get all modules and match pattern
                    auto modules = get_all_modules();

                    // Simple wildcard matching
                    bool starts_with = (module_pattern.back() == '*');
                    bool ends_with   = (module_pattern.front() == '*');

                    std::string pattern = module_pattern;
                    if (starts_with) pattern.pop_back();
                    if (ends_with) pattern.erase(0, 1);

                    for (const auto *module : modules)
                    {
                        if (!module) continue;
                        std::string module_name(module->name);

                        bool match = false;
                        if (starts_with && ends_with) { match = (module_name.find(pattern) != std::string::npos); }
                        else if (starts_with) { match = (module_name.substr(0, pattern.length()) == pattern); }
                        else if (ends_with)
                        {
                            match = (module_name.length() >= pattern.length() &&
                                     module_name.substr(module_name.length() - pattern.length()) == pattern);
                        }

                        if (match) { set_module_level(module->name, level); }
                    }
                }
                else
                {
                    // Exact module name
                    set_module_level(module_pattern.c_str(), level);
                }
            }
        }

        return true;
    }
};

/*
 * Start of log module support
 */

namespace
{
// Initialize with the generic module
static log_module_info g_log_module_info{log_module_registry::instance().get_generic()};
} // namespace

/**
 * @brief Helper struct to configure module settings during static initialization
 *
 * This struct is used by LOG_MODULE_NAME and LOG_MODULE_LEVEL macros to
 * configure the logging module for a compilation unit. The constructors
 * run during static initialization to set up module association.
 */
struct log_module_configurator
{
    /**
     * @brief Set the module name for the current compilation unit
     * @param name Module name to use (must have static storage duration)
     */
    log_module_configurator(const char *name)
    {
        // Only change if different
        if (std::strcmp(g_log_module_info.detail->name, name) != 0)
        {
            g_log_module_info.detail = log_module_registry::instance().get_module(name);
        }
    }

    /**
     * @brief Set the initial log level for the current module
     * @param level Initial minimum log level
     */
    log_module_configurator(log_level level) { g_log_module_info.detail->level = level; }
};

/**
 * @brief Helper macro to generate unique variable names
 * Used internally by LOG_MODULE_NAME and LOG_MODULE_LEVEL
 */
#define _LOG_UNIQ_VAR_NAME(base) base##__COUNTER__

/**
 * @brief Set the module name for all LOG() calls in the current compilation unit
 *
 * This macro should be used at file scope (outside any function) to assign
 * all log messages in the file to a specific module. Modules allow grouped
 * control over log levels.
 *
 * @param name String literal module name (e.g., "network", "database")
 *
 * @code
 * // At file scope
 * LOG_MODULE_NAME("network");
 *
 * void process_request() {
 *     LOG(debug) << "Processing request";  // Uses "network" module
 * }
 * @endcode
 */
#define LOG_MODULE_NAME(name)                                                                    \
    namespace                                                                                    \
    {                                                                                            \
    static ::slwoggy::log_module_configurator _LOG_UNIQ_VAR_NAME(_log_module_name_setter){name}; \
    }

/**
 * @brief Set the initial log level for the current module
 *
 * This macro sets the initial minimum log level for the module associated
 * with the current compilation unit. Should be used after LOG_MODULE_NAME.
 * The level can be changed at runtime via the module registry.
 *
 * @param level The minimum log_level (e.g., log_level::debug)
 *
 * @code
 * LOG_MODULE_NAME("network");
 * LOG_MODULE_LEVEL(log_level::info);  // Start with info and above
 * @endcode
 */
#define LOG_MODULE_LEVEL(level)                                                                    \
    namespace                                                                                      \
    {                                                                                              \
    static ::slwoggy::log_module_configurator _LOG_UNIQ_VAR_NAME(_log_module_level_setter){level}; \
    }

} // namespace slwoggy
// #include "log_buffer.hpp"


/**
 * @file log_buffer.hpp
 * @brief Log buffer management and structured logging support
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <cstdint>
#include <cstring>
#include <string_view>
#include <array>
#include <chrono>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <format>
#include <memory>

// #include "moodycamel/concurrentqueue.h"


// #include "log_types.hpp"


namespace slwoggy {

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
        std::unordered_map<std::string_view, uint16_t> key_to_id;

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
    std::unordered_map<std::string_view, uint16_t> key_to_id_;
    std::vector<std::string> keys_;                            // Stable storage
    std::unordered_map<uint16_t, std::string_view> id_to_key_;
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
        constexpr size_t MAX_FORMATTED_SIZE = 128;
        size_t header_size                  = sizeof(uint16_t) + sizeof(uint16_t);

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

        auto result = std::format_to_n(format_start, available_space, "{}", std::forward<T>(value));

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
    constexpr char *begin() { return data_.data(); }
    constexpr const char *begin() const { return data_.data(); }

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
            available_buffers_.enqueue(buffer);
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
            total_releases_.fetch_add(1, std::memory_order_relaxed);
            buffers_in_use_.fetch_sub(1, std::memory_order_relaxed);
#endif
        }
    }

#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
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
    }
#endif
};

inline void log_buffer::release()
{
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        // Last reference, reset buffer before returning to pool
        reset();
        buffer_pool::instance().release(this);
    }
}

} // namespace slwoggy
// #include "log_sink.hpp"


/**
 * @file log_sink.hpp
 * @brief Type-erased sink implementation for log output
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <memory>
#include <utility>
#include <cstring>
// #include "log_buffer.hpp"


namespace slwoggy {

template <size_t BufferSize> class log_sink_impl
{
    static constexpr size_t buffer_size = BufferSize;

    struct concept_t
    {
        virtual ~concept_t()                                                   = default;
        virtual size_t process_batch(log_buffer **buffers, size_t count) const = 0;

        virtual concept_t *clone_in_place(void *buffer) const = 0;
        virtual concept_t *move_in_place(void *buffer)        = 0; // <— added
        virtual concept_t *heap_clone() const                 = 0;
        virtual size_t object_size() const                    = 0;
    };

    template <typename Formatter, typename Writer> struct model_t final : concept_t
    {
        Formatter formatter_;
        Writer writer_;

        model_t(Formatter f, Writer w) : formatter_(std::move(f)), writer_(std::move(w)) {}

        size_t process_batch(log_buffer **buffers, size_t count) const override
        {
            constexpr size_t STACK_LIMIT = 64 * 1024;

            // Process buffers until we hit a marker
            size_t processed = 0;
            for (size_t i = 0; i < count; ++i)
            {
                if (!buffers[i] || buffers[i]->is_flush_marker())
                {
                    break;  // Stop at markers
                }
                processed++;
            }
            
            if (processed == 0) return 0;
            
            // Calculate total size for processed buffers
            size_t total_size = 0;
            for (size_t i = 0; i < processed; ++i)
            {
                total_size += formatter_.calculate_size(buffers[i]);
            }

            if (total_size < STACK_LIMIT)
            {
                char *buf     = static_cast<char *>(alloca(total_size));
                size_t offset = 0;
                for (size_t i = 0; i < processed; ++i)
                    offset += formatter_.format(buffers[i], buf + offset, total_size - offset);
                writer_.write(buf, offset);
            }
            else
            {
                auto buf      = std::make_unique<char[]>(total_size);
                size_t offset = 0;
                for (size_t i = 0; i < processed; ++i)
                    offset += formatter_.format(buffers[i], buf.get() + offset, total_size - offset);
                writer_.write(buf.get(), offset);
            }
            
            return processed;
        }

        concept_t *clone_in_place(void *buffer) const override { return new (buffer) model_t(formatter_, writer_); }

        // Correct move_in_place implementation:
        concept_t *move_in_place(void *buffer) override
        {
            return new (buffer) model_t(std::move(formatter_), std::move(writer_));
        }

        concept_t *heap_clone() const override { return new model_t(formatter_, writer_); }

        size_t object_size() const override { return sizeof(model_t); }
    };

    alignas(std::max_align_t) char storage_[buffer_size];
    concept_t *ptr_ = nullptr;
    bool is_small_  = false;

    void destroy()
    {
        if (ptr_)
        {
            if (is_small_)
                ptr_->~concept_t();
            else
                delete ptr_;
            ptr_ = nullptr;
        }
    }

  public:
    template <typename Formatter, typename Writer> log_sink_impl(Formatter f, Writer w)
    {
        using Model = model_t<Formatter, Writer>;
        if constexpr (sizeof(Model) <= buffer_size)
        {
            ptr_      = new (storage_) Model(std::move(f), std::move(w));
            is_small_ = true;
        }
        else
        {
            ptr_      = new Model(std::move(f), std::move(w));
            is_small_ = false;
        }
    }

    log_sink_impl(const log_sink_impl &other) : is_small_(other.is_small_)
    {
        if (other.ptr_) { ptr_ = is_small_ ? other.ptr_->clone_in_place(storage_) : other.ptr_->heap_clone(); }
    }

    log_sink_impl(log_sink_impl &&other) noexcept : is_small_(other.is_small_), ptr_(nullptr)
    {
        if (other.ptr_)
        {
            if (is_small_)
            {
                ptr_ = other.ptr_->move_in_place(storage_);
                other.destroy();
            }
            else
            {
                ptr_       = other.ptr_;
                other.ptr_ = nullptr;
            }
        }
    }

    log_sink_impl &operator=(const log_sink_impl &other)
    {
        if (this != &other)
        {
            destroy();
            is_small_ = other.is_small_;
            if (other.ptr_) { ptr_ = is_small_ ? other.ptr_->clone_in_place(storage_) : other.ptr_->heap_clone(); }
        }
        return *this;
    }

    log_sink_impl &operator=(log_sink_impl &&other) noexcept
    {
        if (this != &other)
        {
            destroy();
            is_small_ = other.is_small_;
            if (other.ptr_)
            {
                if (is_small_)
                {
                    ptr_ = other.ptr_->move_in_place(storage_);
                    other.destroy();
                }
                else
                {
                    ptr_       = other.ptr_;
                    other.ptr_ = nullptr;
                }
            }
        }
        return *this;
    }

    ~log_sink_impl() { destroy(); }

    size_t process_batch(log_buffer **buffers, size_t count) const
    {
        if (ptr_) return ptr_->process_batch(buffers, count);
        return 0;
    }
};

using log_sink = log_sink_impl<64>; // Default buffer size of 64 bytes

} // namespace slwoggy

// #include "log_formatters.hpp"


/**
 * @file log_formatters.hpp
 * @brief Log message formatting implementations
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <cstring>

// #include "log_types.hpp"

// #include "log_buffer.hpp"


namespace slwoggy {

class raw_formatter
{
  public:
    bool use_color = true;
    bool add_newline = false;

    size_t calculate_size(const log_buffer *buffer) const
    {
        if (!buffer) return 0;

        size_t size = 0;

        // Color prefix
        if (use_color && buffer->level_ >= log_level::trace && buffer->level_ <= log_level::fatal)
        {
            size += std::strlen(log_level_colors[static_cast<int>(buffer->level_)]);
        }

        // Message text
        size += buffer->len();

        // Structured data
        auto metadata = buffer->get_metadata_adapter();
        auto iter = metadata.get_iterator();
        while (iter.has_next())
        {
            auto kv = iter.next();
            auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);
            
            size += 1; // space
            size += key_name.size();
            size += 1; // equals
            
            // Check if needs quotes
            bool needs_quotes = kv.value.find_first_of(" \t\n\r\"'") != std::string_view::npos;
            if (needs_quotes) size += 2; // quotes
            size += kv.value.size();
        }

        // Color reset
        if (use_color)
        {
            size += 4; // "\033[0m"
        }
        
        if (add_newline)
        {
            size += 1; // newline
        }
        
        return size;
    }

    size_t format(const log_buffer *buffer, char *output, size_t max_size) const
    {
        if (!buffer) return 0;

        char *ptr = output;
        char *end = output + max_size;

        // Helper to append string_view
        auto append = [&ptr, end](std::string_view sv) -> bool
        {
            if (ptr + sv.size() > end) return false;
            std::memcpy(ptr, sv.data(), sv.size());
            ptr += sv.size();
            return true;
        };

        // Add color
        if (use_color && buffer->level_ >= log_level::trace && buffer->level_ <= log_level::fatal)
        {
            const char *color = log_level_colors[static_cast<int>(buffer->level_)];
            if (!append(color)) return ptr - output;
        }

        // Add message text
        if (!append(buffer->get_text())) return ptr - output;

        // Add structured data
        auto metadata = buffer->get_metadata_adapter();
        auto iter = metadata.get_iterator();

        while (iter.has_next())
        {
            auto kv = iter.next();
            auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);
            
            if (ptr >= end) return ptr - output;
            *ptr++ = ' ';
            
            if (!append(key_name)) return ptr - output;
            
            if (ptr >= end) return ptr - output;
            *ptr++ = '=';
            
            // Check if needs quotes
            bool needs_quotes = kv.value.find_first_of(" \t\n\r\"'") != std::string_view::npos;
            
            if (needs_quotes)
            {
                if (ptr >= end) return ptr - output;
                *ptr++ = '"';
            }
            
            if (!append(kv.value)) return ptr - output;
            
            if (needs_quotes)
            {
                if (ptr >= end) return ptr - output;
                *ptr++ = '"';
            }
        }

        // Reset color
        if (use_color)
        {
            if (!append("\033[0m")) return ptr - output;
        }

        // Newline
        if (add_newline && ptr < end)
        {
            *ptr++ = '\n';
        }

        return ptr - output;
    }
};

class json_formatter
{
  public:
    bool pretty_print = false;
    bool add_newline = false;

    size_t calculate_size(const log_buffer *buffer) const
    {
        if (!buffer) return 0;

        // Helper to calculate escaped JSON string size
        auto calculate_escaped_size = [](std::string_view str) -> size_t
        {
            size_t size = 0;
            for (char c : str)
            {
                switch (c)
                {
                case '"':
                case '\\':
                case '\b':
                case '\f':
                case '\n':
                case '\r':
                case '\t': size += 2; break;
                default:
                    if (c >= 0x20 && c <= 0x7E) { size += 1; }
                    else { size += UNICODE_ESCAPE_CHARS; }
                }
            }
            return size;
        };

        // Calculate timestamp
        auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(buffer->timestamp_.time_since_epoch()).count();
        auto ms = timestamp_us / 1000;
        auto us = timestamp_us % 1000;

        // Calculate timestamp size: max int64 is 19 digits + dot + 3 decimal places = 23 chars
        char timestamp_buf[32];
        int timestamp_len = std::snprintf(timestamp_buf, sizeof(timestamp_buf), "%lld.%03lld", 
                                         static_cast<long long>(ms), static_cast<long long>(us));

        // Pre-calculate sizes for all fields
        auto text = buffer->get_message();
        size_t level_str_len = std::strlen(log_level_names[static_cast<int>(buffer->level_)]);
        size_t file_len = buffer->file_.size();
        
        // Calculate line number size
        char line_buf[16];
        int line_len = std::snprintf(line_buf, sizeof(line_buf), "%u", buffer->line_);

        // Calculate escaped sizes
        size_t text_escaped_size = calculate_escaped_size(text);
        size_t file_escaped_size = calculate_escaped_size(buffer->file_);

        // JSON structure size calculation:
        size_t json_size = 0;
        json_size += 2;  // {}
        json_size += 13; // "timestamp":
        json_size += timestamp_len;
        json_size += 10; // ,"level":"
        json_size += level_str_len;
        json_size += 10; // ","file":"
        json_size += file_escaped_size;
        json_size += 10; // ","line":
        json_size += line_len;
        json_size += 13; // ,"message":"
        json_size += text_escaped_size;
        json_size += 1; // closing quote for message

        // Add k/v overhead if any
        auto metadata = buffer->get_metadata_adapter();
        auto iter = metadata.get_iterator();
        while (iter.has_next())
        {
            auto kv = iter.next();
            auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);
            json_size += 3; // ,"
            json_size += calculate_escaped_size(key_name);
            json_size += 3; // ":"
            json_size += calculate_escaped_size(kv.value);
            json_size += 1; // "
        }

        // Add newline
        if (add_newline) json_size += 1;

        return json_size;
    }

    size_t format(const log_buffer *buffer, char *output, size_t max_size) const
    {
        if (!buffer) return 0;

        // Helper to escape JSON strings
        auto escape_json = [](std::string_view str, char *out) -> size_t
        {
            char *start = out;
            for (char c : str)
            {
                switch (c)
                {
                case '"':
                    *out++ = '\\';
                    *out++ = '"';
                    break;
                case '\\':
                    *out++ = '\\';
                    *out++ = '\\';
                    break;
                case '\b':
                    *out++ = '\\';
                    *out++ = 'b';
                    break;
                case '\f':
                    *out++ = '\\';
                    *out++ = 'f';
                    break;
                case '\n':
                    *out++ = '\\';
                    *out++ = 'n';
                    break;
                case '\r':
                    *out++ = '\\';
                    *out++ = 'r';
                    break;
                case '\t':
                    *out++ = '\\';
                    *out++ = 't';
                    break;
                default:
                    if (c >= 0x20 && c <= 0x7E) { *out++ = c; }
                    else
                    {
                        // Unicode escape for control chars
                        out += std::snprintf(out, UNICODE_ESCAPE_SIZE, "\\u%04x", static_cast<unsigned char>(c));
                    }
                }
            }
            return out - start;
        };

        char *ptr = output;
        char *end = output + max_size;

        // Calculate timestamp
        auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(buffer->timestamp_.time_since_epoch()).count();
        auto ms = timestamp_us / 1000;
        auto us = timestamp_us % 1000;

        // Start with timestamp and basic fields
        ptr += std::snprintf(ptr, end - ptr,
                            "{\"timestamp\":%lld.%03lld,\"level\":\"%s\",\"file\":\"",
                            static_cast<long long>(ms),
                            static_cast<long long>(us),
                            log_level_names[static_cast<int>(buffer->level_)]);

        if (ptr >= end) return ptr - output;

        ptr += escape_json(buffer->file_, ptr);
        if (ptr >= end) return ptr - output;

        ptr += std::snprintf(ptr, end - ptr, "\",\"line\":%u,\"message\":\"", buffer->line_);
        if (ptr >= end) return ptr - output;

        ptr += escape_json(buffer->get_message(), ptr);
        if (ptr >= end) return ptr - output;
        *ptr++ = '"';

        // Add structured data as JSON fields
        auto metadata = buffer->get_metadata_adapter();
        auto iter = metadata.get_iterator();
        while (iter.has_next())
        {
            auto kv = iter.next();
            auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);

            if (ptr + 7 >= end) return ptr - output; // Minimum space for ,"":{""
            *ptr++ = ',';
            *ptr++ = '"';
            ptr += escape_json(key_name, ptr);
            if (ptr >= end) return ptr - output;
            *ptr++ = '"';
            *ptr++ = ':';
            *ptr++ = '"';
            ptr += escape_json(kv.value, ptr);
            if (ptr >= end) return ptr - output;
            *ptr++ = '"';
        }

        if (ptr >= end) return ptr - output;
        *ptr++ = '}';

        if (add_newline && ptr < end)
        {
            *ptr++ = '\n';
        }

        return ptr - output;
    }
};


} // namespace slwoggy
// #include "log_writers.hpp"


/**
 * @file log_writers.hpp
 * @brief Log output writer implementations
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <string>
#include <cstring>
#include <stdexcept>
#include <unistd.h> // For write() and STDOUT_FILENO
#include <fcntl.h>

namespace slwoggy {

class file_writer
{
  public:
    file_writer(const std::string &filename) : filename_(filename), fd_(-1), close_fd_(true)
    {
        fd_ = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd_ < 0) { throw std::runtime_error("Failed to open log file: " + filename); }
    }
    file_writer(int fd, bool close_fd = false) : filename_(""), fd_(fd), close_fd_(close_fd) {}

    ~file_writer()
    {
        if (close_fd_ && fd_ >= 0)
        {
            close(fd_);
            fd_ = -1;
        }
    }

    ssize_t write(const char *data, size_t len) const
    {
        if (fd_ < 0) { return -1; } // Not initialized

        ssize_t written = ::write(fd_, data, len);
        return written;
    }

  private:
    std::string filename_; ///< File name for logging
    int fd_{-1};           ///< File descriptor for logging
    bool close_fd_{false}; ///< Whether to close fd on destruction
};

} // namespace slwoggy
// #include "log_version.hpp"
/**
 * @file log_version.hpp
 * @brief Version information for slwoggy logging library
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */



namespace slwoggy {

// Version will be set by build process or amalgamation script
// Fallback to development version if not defined
#ifndef SLWOGGY_VERSION_STRING
#define SLWOGGY_VERSION_STRING "v0.0.1-dirty"
#endif

inline constexpr const char* VERSION = SLWOGGY_VERSION_STRING;

} // namespace slwoggy

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
