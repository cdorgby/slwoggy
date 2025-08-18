# slwoggy

A header-only C++20 logging library featuring asynchronous processing, structured logging, and compile-time filtering.

## Key Features

- **Pre-allocated Buffer Pool**: Uses a fixed-size buffer pool with reference counting
- **Lock-Free Queue**: Uses moodycamel::ConcurrentQueue for thread communication
- **Structured Logging**: Key-value metadata support with binary storage
- **Compile-Time Filtering**: Log calls below `GLOBAL_MIN_LOG_LEVEL` are eliminated at compile time
- **Module System**: Runtime log level control per module with wildcard pattern matching
- **Asynchronous Processing**: Dedicated worker thread processes log messages
- **Type-Erased Sinks**: Output handling using type erasure with small buffer optimization
- **Platform-Specific Timestamps**: Uses platform APIs for timestamp generation
- **Adaptive Batching**: Three-phase batching algorithm that adapts to workload patterns
- **File Rotation**: Comprehensive rotation with size/time policies, compression, and retention management
- **Zero-Gap Rotation**: Atomic operations ensure no log loss during rotation
- **ENOSPC Handling**: Automatic cleanup when disk space is exhausted

## Quick Start

```cpp
#include "log.hpp"

using namespace slwoggy;

// Module declaration (optional, defaults to "generic")
LOG_MODULE_NAME("myapp");

int main() {
    // The logger starts with a default stdout sink, so logs are immediately visible
    LOG(info) << "Application started" << endl;
    
    // Optionally configure custom sinks
    // Note: The first add_sink() call replaces the default stdout sink
    log_line_dispatcher::instance().add_sink(make_raw_file_sink("/tmp/app.log"));
    
    // Now logs go to the file instead of stdout
    LOG(debug) << "Processing " << 42 << " items" << endl;
    
    // Modern C++20 formatting
    LOG(warn).format("Temperature {}°C exceeds threshold {}°C", 98.5, 95.0);
    
    // Structured logging with metadata
    LOG(error).add("user_id", 12345)
             .add("error_code", "AUTH_FAILED")
             .add("ip", "192.168.1.1")
        << "Authentication failed" << endl;
    
    // Printf-style (discouraged but available)
    LOG(info).printf("Legacy message: %s %d", "value", 123);
    
    return 0;
}
```

## Log Output Formats

slwoggy supports two output formats that can be selected at the call site:

### Traditional Text Format (LOG_TEXT)
Human-readable format with timestamp, level, module, and location information:
```cpp
LOG_TEXT(info) << "User logged in" << endl;
// Output: 00001234.567 [INFO ] myapp      main.cpp:42 User logged in
```

### Structured Format (LOG_STRUCTURED)
Machine-parseable logfmt format with key-value pairs. This format automatically includes standard metadata fields:
```cpp
LOG_STRUCTURED(info) << "User logged in" << endl;
// Output: msg="User logged in" ts=1234567890 level=info module=myapp file=main.cpp line=42
```

Default metadata fields automatically included:
- `ts`: Timestamp (nanoseconds since epoch)
- `level`: Log level (trace, debug, info, warn, error, fatal)
- `module`: Module name (from LOG_MODULE_NAME or "generic")
- `file`: Source file name
- `line`: Source line number

**Note about `msg="..."` field**: The `msg="..."` that appears first in the output is not a true structured field - it's part of the log format prefix and won't interfere with or be searchable as structured metadata. If you add your own `msg` field via `.add("msg", "value")`, both will appear in the output:
```cpp
LOG_STRUCTURED(info).add("msg", "custom value") << "Log text" << endl;
// Output: msg="Log text" msg="custom value" ts=... level=... 
//         ^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^
//         Format prefix   Your structured field
```

### Default Behavior (LOG)
The `LOG()` macro defaults to `LOG_TEXT()` for backwards compatibility:
```cpp
LOG(info) << "This uses traditional text format" << endl;
// Equivalent to: LOG_TEXT(info) << "This uses traditional text format" << endl;
```

Both formats support all the same features including:
- Stream operators (`<<`)
- Format strings (`.format()`)
- Structured metadata (`.add()`)
- Multi-line support with automatic indentation

Choose the format based on your needs:
- **LOG_TEXT()**: Best for console output, development, and human inspection
- **LOG_STRUCTURED()**: Best for log aggregation systems, parsing, and analysis
- **LOG()**: Use when you want the default behavior (currently text format)

## Default Sink Behavior

The logging system initializes with a default stdout sink for convenience. This ensures that logs are immediately visible without any configuration. The default sink behavior is:

- **Initial state**: All logs go to stdout with raw formatting
- **First `add_sink()` call**: Replaces the default stdout sink with your custom sink
- **`set_sink()` or `remove_sink()` calls**: Also disable the default sink behavior
- **Multiple sinks**: After the first sink is added, subsequent `add_sink()` calls append to the sink list

This design provides a zero-configuration experience while allowing full customization when needed.

## Log Levels

- `trace` - Finest-grained debugging information
- `debug` - Debug messages  
- `info` - General informational messages
- `warn` - Warning messages
- `error` - Error messages
- `fatal` - Critical errors

## Module System

Control logging verbosity by subsystem:

```cpp
using namespace slwoggy;

// In network code
LOG_MODULE_NAME("network");
LOG_MODULE_LEVEL(log_level::debug);

// In database code  
LOG_MODULE_NAME("database");
LOG_MODULE_LEVEL(log_level::warn);

// Runtime control
auto& registry = log_module_registry::instance();
registry.set_module_level("network", log_level::info);
registry.configure_from_string("warn,network=debug,database=error");
```

## Structured Logging

Add searchable metadata to any log message:

```cpp
using namespace slwoggy;

// Pre-register frequently used keys at startup (optional optimization)
auto& key_registry = structured_log_key_registry::instance();
key_registry.batch_register({"user_id", "request_id", "latency_ms"});

// Use in logging with either format
LOG(info).add("user_id", user.id)
         .add("request_id", req.id)
         .add("latency_ms", elapsed.count())
    << "Request completed successfully" << endl;

// For explicit structured format output (logfmt style)
LOG_STRUCTURED(info).add("user_id", user.id)
                    .add("request_id", req.id)
                    .add("latency_ms", elapsed.count())
    << "Request completed successfully" << endl;
// Output: msg="Request completed successfully" user_id=123 request_id=456 latency_ms=78 ts=... level=info ...
```

### Internal Metadata Keys

The system pre-registers five internal metadata keys with guaranteed IDs for optimal performance:
- `ts` (ID 0) - Timestamp (automatically added by LOG_STRUCTURED)
- `level` (ID 1) - Log level (automatically added by LOG_STRUCTURED)
- `module` (ID 2) - Module name (automatically added by LOG_STRUCTURED)
- `file` (ID 3) - Source file (automatically added by LOG_STRUCTURED)
- `line` (ID 4) - Source line number (automatically added by LOG_STRUCTURED)

These internal keys use ultra-fast lookup paths that bypass all caching layers, making them essentially free to use. When using `LOG_STRUCTURED()`, these fields are automatically populated. When using `LOG()` or `LOG_TEXT()`, this metadata is still available internally but formatted differently in the output.

## Binary Data and Hex Dumps

slwoggy provides built-in support for logging binary data in various hex dump formats:

### Basic Hex Dump

```cpp
uint8_t data[64];
// ... fill data ...

// Dump with ASCII sidebar (like hexdump -C)
LOG(info).hex_dump_best_effort(data, sizeof(data), log_line_base::hex_dump_format::full);
// Output: 0000: 00 01 02 03  04 05 06 07  08 09 0a 0b  0c 0d 0e 0f  |................|
//         0010: 10 11 12 13  14 15 16 17  18 19 1a 1b  1c 1d 1e 1f  |................|

// Hex only without ASCII
LOG(info).hex_dump_best_effort(data, sizeof(data), log_line_base::hex_dump_format::no_ascii);
// Output: 0000: 00 01 02 03  04 05 06 07  08 09 0a 0b  0c 0d 0e 0f
//         0010: 10 11 12 13  14 15 16 17  18 19 1a 1b  1c 1d 1e 1f
```

### Inline Hex Format

For compact inline representation with customizable formatting:

```cpp
// Simple inline hex
LOG(info) << "Data: ";
LOG(info).hex_dump_best_effort(data, 16, log_line_base::hex_dump_format::inline_hex);
// Output: Data: 000102030405060708090a0b0c0d0e0f

// With custom formatting
hex_inline_config hex_0x = {"0x", "", " ", "", ""};  // prefix, suffix, separator, left/right brackets
LOG(info).hex_dump_best_effort(data, 16, log_line_base::hex_dump_format::inline_hex, 8, hex_0x);
// Output: 0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0a 0x0b 0x0c 0x0d 0x0e 0x0f

hex_inline_config hex_brackets = {"", "", " ", "[", "]"};
LOG(info).hex_dump_best_effort(data, 16, log_line_base::hex_dump_format::inline_hex, 8, hex_brackets);
// Output: [00] [01] [02] [03] [04] [05] [06] [07] [08] [09] [0a] [0b] [0c] [0d] [0e] [0f]
```

### Large Data Dumps

For dumping large amounts of data that may exceed buffer capacity:

```cpp
uint8_t large_data[4096];
// ... fill data ...

// Automatically handles buffer boundaries and continues across multiple log lines
LOG(info).hex_dump_full(large_data, sizeof(large_data), log_line_base::hex_dump_format::no_ascii);
// Output: binary data len: 0/4096
//         0000: 00 01 02 03  04 05 06 07  08 09 0a 0b  0c 0d 0e 0f
//         ...
//         binary data len: 2048/4096  (continues in next buffer)
//         0800: ...
```

### Features

- **Duplicate Line Compression**: Repeated 16-byte lines are compressed with `*` (like `hexdump -C`)
- **Automatic Buffer Management**: Large dumps automatically continue across buffer boundaries
- **Progress Tracking**: Shows current offset/total for multi-buffer dumps
- **Proper Alignment**: Hex dump lines are properly indented to align with log headers
- **Complete Line Guarantee**: Never writes partial lines when buffer space is insufficient

## Filters

Apply filters to drop or modify log messages before they reach sinks:

```cpp
using namespace slwoggy;
using namespace std::chrono_literals;

// Deduplication filter - drops duplicate messages within time window
auto dedup = std::make_shared<dedup_filter>(100ms);
log_line_dispatcher::instance().add_filter(dedup);

// Rate limiting filter - limits messages per second
auto rate_limit = std::make_shared<rate_limit_filter>(1000);  // 1000 msg/s
log_line_dispatcher::instance().add_filter(rate_limit);

// Statistical sampling - only pass a percentage of messages
auto sampler = std::make_shared<sampler_filter>(0.1);  // 10% sampling
log_line_dispatcher::instance().add_filter(sampler);

// Multiple filters are applied in order (AND logic)
// Message must pass ALL filters to reach sinks
```

Filters use the same RCU pattern as sinks for lock-free updates. The provided filters are examples for testing - production use should implement proper metrics and configuration.

## Custom Sinks

Create custom output handlers:

```cpp
using namespace slwoggy;

// JSON output to stdout
auto json_sink = log_sink{
    json_formatter{.pretty_print = false, .add_newline = true},
    file_writer{STDOUT_FILENO}
};

// File output with color
auto file_sink = log_sink{
    raw_formatter{.use_color = false, .add_newline = true},
    file_writer{"/var/log/myapp.log"}
};

// Add sinks
auto& dispatcher = log_line_dispatcher::instance();
dispatcher.add_sink(std::make_shared<log_sink>(std::move(json_sink)));
```

## File Rotation

slwoggy provides comprehensive file rotation support with size-based, time-based, and combined rotation policies.

### Basic Rotation

```cpp
#include "log.hpp"
#include "log_file_rotator.hpp"

using namespace slwoggy;

// Size-based rotation: rotate when file reaches 100MB
rotate_policy policy;
policy.mode = rotate_policy::kind::size;
policy.max_bytes = 100 * 1024 * 1024;  // 100MB
policy.keep_files = 10;  // Keep last 10 rotated files

auto sink = make_raw_file_sink("/var/log/app.log", policy);
log_line_dispatcher::instance().add_sink(sink);
```

### Time-Based Rotation

```cpp
// Daily rotation at midnight
rotate_policy policy;
policy.mode = rotate_policy::kind::time;
policy.every = std::chrono::hours(24);  // Rotate daily
policy.at = std::chrono::hours(0);      // At midnight UTC
policy.keep_files = 30;                 // Keep 30 days of logs
```

### Combined Rotation

```cpp
// Rotate on size OR time, whichever comes first
rotate_policy policy;
policy.mode = rotate_policy::kind::size_or_time;
policy.max_bytes = 50 * 1024 * 1024;    // 50MB
policy.every = std::chrono::hours(24);   // Or daily
policy.keep_files = 14;                  // Keep 2 weeks
```

### Rotation Policy Options

```cpp
struct rotate_policy {
    enum class kind {
        none,           // No rotation (default)
        size,           // Rotate by size only
        time,           // Rotate by time only
        size_or_time    // Rotate on size OR time
    };
    
    kind mode = kind::none;
    
    // Size policy
    uint64_t max_bytes = 0;              // Max file size before rotation
    
    // Time policy (using std::chrono::seconds for flexibility)
    std::chrono::seconds every{0};       // Rotation interval
    std::chrono::seconds at{0};          // Time of day for daily rotation
    
    // Retention policies (applied in order of precedence)
    int keep_files = 5;                  // Number of files to keep
    std::chrono::seconds max_age{0};     // Delete files older than this
    uint64_t max_total_bytes = 0;        // Total size limit for all logs
    
    // Post-rotation actions
    bool compress = false;                // Compress rotated files (.gz)
    bool sync_on_rotate = false;         // fsync before rotation
    
    // Error handling
    int max_retries = 10;                // Retry attempts on failure
};
```

### Advanced Features

#### Zero-Gap Rotation
The rotation system uses atomic link+rename operations to ensure no log messages are lost during rotation, even under high load.

#### ENOSPC Handling
When disk space is exhausted, the system automatically:
1. Deletes `.pending` files first (incomplete compressions)
2. Then deletes `.gz` files (compressed logs)
3. Finally deletes oldest raw log files
4. Tracks all deletions in metrics for monitoring

#### Compression
Rotated files can be automatically compressed using gzip:
```cpp
policy.compress = true;  // Creates .gz files after rotation
```

#### Retention Management
Multiple retention strategies can be combined:
```cpp
policy.keep_files = 10;                          // Keep max 10 files
policy.max_total_bytes = 1024 * 1024 * 1024;    // Max 1GB total
policy.max_age = std::chrono::hours(24 * 30);   // Delete after 30 days
```

### Rotation Metrics

Monitor rotation behavior with built-in metrics:
```cpp
auto stats = rotation_metrics::instance().get_stats();
std::cout << "Total rotations: " << stats.total_rotations << "\n";
std::cout << "Avg rotation time: " << stats.avg_rotation_time_us << " μs\n";
std::cout << "ENOSPC cleanups: " << stats.enospc_raw_deleted << " files\n";
```

### Example: Production Configuration

```cpp
// Production setup with comprehensive policies
rotate_policy policy;
policy.mode = rotate_policy::kind::size_or_time;
policy.max_bytes = 256 * 1024 * 1024;           // 256MB per file
policy.every = std::chrono::hours(24);          // Daily rotation
policy.at = std::chrono::hours(3);              // At 3 AM UTC
policy.keep_files = 30;                         // Keep 30 files
policy.max_total_bytes = 10L * 1024 * 1024 * 1024; // Max 10GB total
policy.max_age = std::chrono::hours(24 * 90);   // Delete after 90 days
policy.compress = true;                         // Compress old files
policy.sync_on_rotate = true;                   // Ensure durability

auto sink = make_writev_file_sink("/var/log/production.log", policy);
log_line_dispatcher::instance().add_sink(sink);
```

## Performance Tuning

### Compile-Time Optimization

Set minimum log level to eliminate code at compile time:

```cpp
// In log_types.hpp or compile flags
namespace slwoggy {
    inline constexpr log_level GLOBAL_MIN_LOG_LEVEL = log_level::info;
}
```

### Buffer Pool Configuration

```cpp
// In log_types.hpp
namespace slwoggy {
    inline constexpr size_t BUFFER_POOL_SIZE = 32 * 1024;   // Number of buffers
}
```

### Batching Configuration

The dispatcher uses an adaptive three-phase batching algorithm:

1. **Idle Wait**: Blocks indefinitely with zero CPU usage until messages arrive
2. **Bounded Collection**: Collects messages for up to 100μs after first arrival
3. **Adaptive Phase**: Continues collecting while messages keep flowing

```cpp
// In log_types.hpp - Tunable parameters
namespace slwoggy {
    // Maximum time to wait for additional messages (Phase 2)
    inline constexpr auto BATCH_COLLECT_TIMEOUT = std::chrono::microseconds(100);
    
    // Polling interval during bounded collection
    inline constexpr auto BATCH_POLL_INTERVAL = std::chrono::microseconds(10);
    
    // Maximum messages per batch
    inline constexpr size_t MAX_BATCH_SIZE = 4 * 1024;
}
```

Performance characteristics:
- **Idle**: Zero CPU usage (thread blocks waiting for messages)
- **Light load**: Small batches (10-100 messages), minimal latency
- **Heavy load**: Large batches (up to 4096 messages), maximum throughput
- **Typical performance**: >10M messages/second with adaptive batching

### Metrics Collection

Enable optional metrics at compile time:

```cpp
#define LOG_COLLECT_BUFFER_POOL_METRICS
#define LOG_COLLECT_DISPATCHER_METRICS  
#define LOG_COLLECT_STRUCTURED_METRICS
#define LOG_COLLECT_DISPATCHER_MSG_RATE

using namespace slwoggy;

// Access metrics
auto pool_stats = buffer_pool::instance().get_stats();
auto disp_stats = log_line_dispatcher::instance().get_stats();

// Key dispatcher metrics include:
// - Batch sizes: min/avg/max messages per batch
// - Dequeue timing: min/avg/max time spent collecting batches
// - Processing rates: messages/second over 1s/10s/60s windows
// - In-flight timing: time from log creation to processing
```

### Structured Logging Performance

The structured logging system includes several optimizations:

- **Ultra-fast internal keys**: Built-in keys (_ts, _level, etc.) use direct string comparison, bypassing hash lookups
- **Thread-local caching**: User-defined keys are cached per-thread to avoid lock contention
- **Pre-registration**: Register frequently used keys at startup to minimize runtime overhead
- **Compact storage**: 16-bit IDs instead of full strings in log buffers

## Advanced Features

### Per-Site Control

Control individual log locations:

```cpp
using namespace slwoggy;

// Runtime control of specific log sites
log_site_registry::set_site_level("network.cpp", 42, log_level::trace);
log_site_registry::set_file_level("database/*.cpp", log_level::error);
```

### Multi-line Support

Multi-line logs are automatically indented:

```cpp
using namespace slwoggy;

LOG(info) << "Request details:\n"
          << "  Method: GET\n"  
          << "  Path: /api/users\n"
          << "  Status: 200" << endl;
```

### Smart Pointer Support

```cpp
using namespace slwoggy;

auto ptr = std::make_shared<MyClass>();
auto weak = std::weak_ptr<MyClass>(ptr);

LOG(debug) << "Shared: " << ptr;   // Logs address or "nullptr"
LOG(debug) << "Weak: " << weak;    // Logs address or "(expired)"
```

## Building

### Requirements

- C++20 compatible compiler
- CMake 3.11+
- POSIX threads (Linux/macOS)
- Windows threads (Windows)

### Build Instructions

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run example
./bin/slwoggy

# Run tests
make tests && ctest
```

### Single-Header Version

To create a single-header amalgamation that includes all dependencies:

```bash
# Using the shell script
./create-amalgamation.sh

# Or using CMake
cd build
make amalgamation
```

This creates `include/slwoggy.hpp` which includes the moodycamel library and all slwoggy headers in one file. Simply copy this file to your project and `#include "slwoggy.hpp"`.

**Note about file paths**: When building with CMake, `SOURCE_FILE_NAME` is defined to show relative paths in log output. The amalgamated header automatically falls back to `__FILE__` when `SOURCE_FILE_NAME` is not defined, so it works out of the box without any build system configuration.

### Build Modes

- `Release` - Optimized production build
- `Debug` - Debug symbols, metrics enabled
- `MemCheck` - AddressSanitizer and UndefinedBehaviorSanitizer
- `Profile` - Optimized with debug symbols for profiling

## Thread Safety

- All logging operations are thread-safe
- Log order preserved within each thread
- Module registry protected by shared_mutex
- Sink modifications use RCU pattern (rare updates)

## Architecture Overview

```
┌─────────────┐     ┌──────────────┐     ┌──────────────┐     ┌─────────────┐
│   LOG()     │────▶│   log_line   │────▶│ Buffer Pool  │────▶│   Buffer    │
│   Macro     │     │   (RAII)     │     │ (lock-free)  │     │   + Data    │
└─────────────┘     └──────────────┘     └──────────────┘     └──────┬──────┘
                                                                     │
                                                                     ▼
                                                              ┌───────────────┐
                                                              │    Queue      │
                                                              │ (lock-free)   │
                                                              └───────┬───────┘
                                                                      │
                          ┌───────────────────────────────────────────▼─────────┐
                          │                 Worker Thread                       │
                          │            (batch processing)                       │
                          └───────────────────────────────────────────┬─────────┘
                                                                      │
                    ┌───────────────┬─────────────────────┬───────────▼────────┐
                    │ Console Sink  │    File Sink        │   Custom Sink      │
                    └───────────────┴─────────────────────┴────────────────────┘
```

## Versioning

slwoggy uses [Semantic Versioning](https://semver.org/). Version information is:
- Tracked via git tags (e.g., `v1.0.0`)
- Automatically embedded in amalgamated headers
- Available at runtime via `slwoggy::VERSION`

```cpp
#include <iostream>
#include "slwoggy.hpp"

int main() {
    std::cout << "Using slwoggy version: " << slwoggy::VERSION << std::endl;
    return 0;
}
```

## License

MIT License - see [LICENSE](LICENSE) file for details.
