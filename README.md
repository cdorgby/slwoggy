# slwoggy

A header-only C++20 logging library featuring asynchronous processing, structured logging, and compile-time filtering.


## Quick Start

```cpp
#include "slwoggy.hpp"

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

## Integration

### Using CMake FetchContent (Recommended)

Add slwoggy to your project using CMake's FetchContent:

```cmake
cmake_minimum_required(VERSION 3.11)
project(my_project)

include(FetchContent)

FetchContent_Declare(
    slwoggy
    GIT_REPOSITORY https://github.com/cdorgby/slwoggy.git
    GIT_TAG main  # or use a specific tag/commit
    GIT_SHALLOW TRUE  # Faster cloning
)

FetchContent_MakeAvailable(slwoggy)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE slwoggy::slwoggy)
```

Then in your code:
```cpp
#include <slwoggy.hpp>

using namespace slwoggy;

int main() {
    log_line_dispatcher::instance().add_sink(make_stdout_sink());
    LOG(info) << "Hello from slwoggy!" << endl;
    return 0;
}
```

### Using the Amalgamated Header

For simpler integration without CMake, use the single-header version:

1. Download `slwoggy.hpp` from the [releases page](https://github.com/cdorgby/slwoggy/releases)
2. Copy it to your project
3. Include it: `#include "slwoggy.hpp"`

The amalgamated header includes all dependencies (fmt, taocpp-json, moodycamel) and works standalone.

### CMake Options

When using FetchContent or building slwoggy directly, these options are available:

- `SLWOGGY_BUILD_EXAMPLES` - Build example applications (default: OFF)
- `SLWOGGY_BUILD_TESTS` - Build tests (default: OFF)
- `SLWOGGY_METRICS_ALL` - Enable all metrics collection (default: OFF)
- `SLWOGGY_METRICS_BUFFER_POOL` - Enable buffer pool metrics
- `SLWOGGY_METRICS_DISPATCHER` - Enable dispatcher metrics
- `SLWOGGY_METRICS_STRUCTURED` - Enable structured logging metrics
- `SLWOGGY_METRICS_MSG_RATE` - Enable message rate tracking

For Release builds, examples and tests are OFF by default. For Debug builds, they're automatically enabled.

## Formatting Methods

slwoggy provides multiple ways to format log messages:

### Stream Style (operator<<)
```cpp
LOG(info) << "User " << username << " logged in at " << timestamp;
```

### Format Style (format method)
```cpp
LOG(info).format("User {} logged in at {}", username, timestamp);
// Uses fmt library for type-safe formatting
```

### Printf Style
```cpp
LOG(info).printf("User %s logged in at %ld", username, timestamp);
// C-style formatting for compatibility
LOG(info).printfmt("Count: %d", count); // Returns *this for chaining
```

### Immediate Flush and Reusable Log Lines
```cpp
// Use endl to force immediate flush
LOG(error) << "Critical error: " << error_msg << endl;

// Or call flush() explicitly  
LOG(warn).format("Warning: {}", msg).flush();

// Important: flush/endl makes log_line reusable for incremental logging
auto logger = LOG(info);
logger << "Starting process...";
logger.flush();  // Sends log, but logger remains valid

// Do some work...
process_data();

logger << "Process completed";  // Reuses same log line
// Destructor will flush automatically
```

## Log Output Formats

slwoggy supports two built-in output formats that can be selected at the call site, plus you can create custom formats (see [Custom Log Line Classes](#custom-log-line-classes) in Advanced Features):

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

Modules allow you to control logging verbosity by subsystem. Each compilation unit can belong to a named module, and log levels can be controlled per-module at runtime.

### Module Configuration

```cpp
using namespace slwoggy;

// At file scope - declare module name for this compilation unit
LOG_MODULE_NAME("network");

// Optional: Set initial log level for this module
LOG_MODULE_LEVEL(log_level::debug);

// In another file
LOG_MODULE_NAME("database");
LOG_MODULE_LEVEL(log_level::warn);

// Files without LOG_MODULE_NAME use the "generic" module
```

### Runtime Control

```cpp
// Get the module registry
auto& registry = log_module_registry::instance();

// Set level for specific module
registry.set_module_level("network", log_level::info);

// Set level for all modules
registry.set_all_modules_level(log_level::warn);

// Configure from string (format: default_level,module=level,...)
registry.configure_from_string("warn,network=debug,database=error");

// Query current level
log_level current = registry.get_module_level("network");

// List all modules
auto modules = registry.get_all_modules();
for (const auto& [name, level] : modules) {
    std::cout << name << ": " << log_level_names[static_cast<int>(level)] << "\n";
}
```

### Module Name Patterns

- Module names are case-sensitive
- Use consistent naming: "network", "database", "auth", etc.
- Avoid spaces and special characters
- The "generic" module is the default for files without LOG_MODULE_NAME

### Dynamic Module Selection

You can specify a module name at the log call site using the LOG_MOD macros. This allows logging with a different module's settings without changing the compilation unit's default module:

```cpp
// Three variants available:
// LOG_MOD_TEXT - traditional text format
// LOG_MOD_STRUCT - structured logfmt format  
// LOG_MOD - alias for LOG_MOD_TEXT (defaults to text)

// Log with "network" module even if file uses different module
LOG_MOD(info, "network") << "Connection established";
LOG_MOD_TEXT(debug, "network") << "Same as above - explicit text format";

// Structured format with specific module
LOG_MOD_STRUCT(error, "database")
    .add("query_id", 123)
    .add("error", "connection timeout")
    << "Query failed";

```

**Performance Note:** The LOG_MOD macros perform a one-time module lookup during static initialization and cache the result. While more efficient than runtime lookup on every call, they still have slightly more overhead than regular LOG macros which use a pre-resolved module reference from the compilation unit. The module name must be a compile-time string literal.

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

The system pre-registers five internal metadata keys with guaranteed IDs:
- `ts` (ID 0) - Timestamp (automatically added by LOG_STRUCTURED)
- `level` (ID 1) - Log level (automatically added by LOG_STRUCTURED)
- `module` (ID 2) - Module name (automatically added by LOG_STRUCTURED)
- `file` (ID 3) - Source file (automatically added by LOG_STRUCTURED)
- `line` (ID 4) - Source line number (automatically added by LOG_STRUCTURED)

These internal keys use fast lookup paths that bypass all caching layers, making them essentially free to use. When using `LOG_STRUCTURED()`, these fields are automatically populated. When using `LOG()` or `LOG_TEXT()`, this metadata is still available internally but formatted differently in the output.

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

## Sinks, Formatters and Writers

### Available Formatters

**raw_formatter** - Traditional text format with timestamps and levels
```cpp
raw_formatter{
    .use_color = true,      // ANSI color codes (default: true)
    .add_newline = true     // Auto-append newlines (default: true)
}
```

**taocpp_json_formatter** - JSON output for structured logging (requires TAO JSON)
```cpp
taocpp_json_formatter{
    .pretty_print = false,  // Pretty formatting (default: false)  
    .add_newline = true     // Newline after each JSON object (default: true)
}
```

**nop_formatter** - Pass-through formatter (no formatting)
```cpp
nop_formatter{}  // Returns buffer content as-is
```

### Available Writers

**file_writer** - Standard file output
```cpp
file_writer("/var/log/app.log")                    // Write to file
file_writer("/var/log/app.log", rotation_policy)   // With rotation support
file_writer(STDOUT_FILENO)                         // Write to stdout
file_writer(STDERR_FILENO)                         // Write to stderr
```

**writev_file_writer** - Vectored I/O (inherits from file_writer)
```cpp
writev_file_writer("/var/log/app.log")             // Batches multiple buffers per syscall
writev_file_writer("/var/log/app.log", rotation_policy)  // With rotation support
```

**discard_writer** - Null sink
```cpp
discard_writer{}  // Discards all output (for testing/benchmarking)
```

### Creating Sinks

```cpp
// Console output with colors
auto console_sink = std::make_shared<log_sink>(
    raw_formatter{.use_color = true, .add_newline = true},
    file_writer{STDOUT_FILENO}
);

// File output without colors, with rotation
rotate_policy policy;
policy.mode = rotate_policy::kind::size;
policy.max_bytes = 100 * 1024 * 1024;  // 100MB

auto file_sink = std::make_shared<log_sink>(
    raw_formatter{.use_color = false, .add_newline = true},
    file_writer{"/var/log/app.log", policy}
);

// Add sinks to dispatcher
auto& dispatcher = log_line_dispatcher::instance();
dispatcher.add_sink(console_sink);
dispatcher.add_sink(file_sink);

```

**NOTE**: Never write multiple sinks to the same file path! This causes undefined behavior due to concurrent writes.

## Per-Sink Filtering

slwoggy allows each sink to have its own filter, enabling different log levels and criteria for different outputs. Filters have zero overhead when not used.

### Basic Level Filtering

```cpp
#include "slwoggy.hpp"
#include "log_sink_filters.hpp"

using namespace slwoggy;

// Console shows only warnings and above
auto console = make_stdout_sink(level_filter{log_level::warn});

// File captures everything for debugging
auto debug_file = make_raw_file_sink("/var/log/debug.log");

// Error log captures only errors and fatal
auto error_file = make_raw_file_sink("/var/log/errors.log", {}, 
                                      level_filter{log_level::error});

auto& dispatcher = log_line_dispatcher::instance();
dispatcher.set_sink(0, console);       // Replace default stdout
dispatcher.add_sink(debug_file);       // Add debug file
dispatcher.add_sink(error_file);       // Add error file

// Now:
// - INFO and below go only to debug.log
// - WARN goes to console and debug.log
// - ERROR and FATAL go to all three outputs
```

### Range and Max Level Filters

```cpp
// Only capture info and warn levels (not debug or error)
auto info_sink = make_file_sink("info.log", {}, 
    level_range_filter{log_level::info, log_level::warn});

// Only capture trace and debug (useful for verbose debug logs)
auto verbose_sink = make_file_sink("verbose.log", {},
    max_level_filter{log_level::debug});
```

### Composite Filters

Combine multiple filters with AND, OR, and NOT logic:

```cpp
// AND filter: warn/error only (not debug or fatal)
and_filter warn_error_only;
warn_error_only.add(level_filter{log_level::warn})
               .add(max_level_filter{log_level::error});
auto filtered = make_stdout_sink(warn_error_only);

// OR filter: debug messages OR errors and above
or_filter debug_or_severe;
debug_or_severe.add(level_range_filter{log_level::debug, log_level::debug})
               .add(level_filter{log_level::error});
auto special = make_file_sink("special.log", {}, debug_or_severe);

// NOT filter: everything except info level
auto no_info = make_stdout_sink(
    not_filter{level_range_filter{log_level::info, log_level::info}});
```

### Module-Based Filtering

Filter logs by module name to route different subsystems to different sinks:

```cpp
// Network logs go to a dedicated file
auto network_sink = make_raw_file_sink("network.log", {},
    module_filter{{"network", "http", "websocket"}});

// Database operations to another file
auto db_sink = make_raw_file_sink("database.log", {},
    module_filter{{"database", "sql"}});

// Exclude verbose modules from main log
auto main_sink = make_raw_file_sink("app.log", {},
    module_exclude_filter{{"trace", "verbose", "debug_internal"}});

// Combine module and level filtering
and_filter critical_network;
critical_network.add(module_filter{{"network", "security"}})
                .add(level_filter{log_level::error});
auto alert_sink = make_stdout_sink(critical_network);

// Add all sinks
auto& dispatcher = log_line_dispatcher::instance();
dispatcher.add_sink(network_sink);
dispatcher.add_sink(db_sink);
dispatcher.add_sink(main_sink);
dispatcher.add_sink(alert_sink);

// Use LOG_MOD to specify module at log site
LOG_MOD(info, "network") << "Connection established";
LOG_MOD(error, "database") << "Query failed";
LOG_MOD(debug, "verbose") << "Detailed trace info";  // Excluded from main_sink
```

### Complex Filtering Example

```cpp
// Production setup with different filters for different purposes
void setup_production_logging() {
    auto& dispatcher = log_line_dispatcher::instance();
    
    // Console: warnings and above for operators
    dispatcher.set_sink(0, make_stdout_sink(level_filter{log_level::warn}));
    
    // Main log: everything, rotated daily
    rotate_policy daily;
    daily.mode = rotate_policy::kind::time;
    daily.every = std::chrono::hours(24);
    daily.keep_files = 30;
    dispatcher.add_sink(make_writev_file_sink("/var/log/app.log", daily));
    
    // Error log: only errors, kept longer
    rotate_policy error_policy;
    error_policy.mode = rotate_policy::kind::size;
    error_policy.max_bytes = 10 * 1024 * 1024;  // 10MB
    error_policy.keep_files = 90;  // Keep 90 files
    dispatcher.add_sink(make_writev_file_sink("/var/log/errors.log", 
                                               error_policy,
                                               level_filter{log_level::error}));
    
    // Debug log: only in debug builds
#ifdef DEBUG
    // Verbose debugging but exclude info level (too noisy)
    and_filter debug_filter;
    debug_filter.add(max_level_filter{log_level::debug})
                .add(not_filter{level_range_filter{log_level::info, log_level::info}});
    dispatcher.add_sink(make_file_sink("/tmp/debug.log", {}, debug_filter));
#endif
}
```

### Available Filter Types

- **`no_filter`** - Default, accepts all messages (zero overhead)
- **`level_filter{min_level}`** - Accepts messages >= min_level
- **`max_level_filter{max_level}`** - Accepts messages <= max_level  
- **`level_range_filter{min, max}`** - Accepts messages in range [min, max]
- **`and_filter`** - All sub-filters must pass
- **`or_filter`** - At least one sub-filter must pass
- **`not_filter{filter}`** - Inverts the wrapped filter

Filters are evaluated in the dispatcher's worker thread, not in the logging thread, so filtering overhead doesn't block the application's logging calls.

## File Rotation

slwoggy provides comprehensive file rotation support with size-based, time-based, and combined rotation policies.

### Basic Rotation

```cpp
#include "slwoggy.hpp"

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

### File Compression

slwoggy supports automatic gzip compression of rotated log files to save disk space. Compression can be configured to run either synchronously (in the rotation thread) or asynchronously (in a dedicated compression thread).

⚠️ **WARNING**: Compression should NOT be used with high-throughput logging. Even with async compression, high log volumes can overwhelm the compression thread, cause queue overflows, and lead to data loss or undefined behavior. Compression is best suited for low to moderate throughput scenarios where disk space is a concern.

#### Synchronous Compression

```cpp
// Simple compression for low-throughput scenarios
rotate_policy policy;
policy.mode = rotate_policy::kind::size;
policy.max_bytes = 10 * 1024 * 1024;  // 10MB files
policy.keep_files = 5;
policy.compress = true;  // Enable compression

auto sink = make_writev_file_sink("/var/log/app.log", policy);
log_line_dispatcher::instance().add_sink(sink);
// Files will be compressed synchronously during rotation
// ⚠️ Blocks rotation thread during compression
```

#### (Optional) Asynchronous Compression

```cpp
// Start compression thread, you can start it later
file_rotation_service::instance().start_compression_thread(
    std::chrono::milliseconds{500},  // Batch delay (wait for more files)
    10                                // Max queue size
);

rotate_policy policy;
policy.mode = rotate_policy::kind::size;
policy.max_bytes = 100 * 1024 * 1024;  // 100MB files
policy.keep_files = 30;
policy.compress = true;  // Will use async compression thread

auto sink = make_writev_file_sink("/var/log/app.log", policy);
log_line_dispatcher::instance().add_sink(sink);

// Thread can be stopped anytime, it can be restarted again at any time.
file_rotation_service::instance().stop_compression_thread();
```

#### Compression Behavior

- **File naming**: Compressed files get `.gz` extension (e.g., `app-20240120-001.log.gz`)
- **Atomic operation**: Uses `.gz.pending` temporary files during compression
- **Cancellation**: Files deleted by retention policy cancel pending compressions
- **Queue overflow**: When compression queue is full, new compressions are skipped (data not lost, just not compressed)
- **State tracking**: Files transition through states: idle → queued → compressing → done/cancelled

#### Monitoring Compression

```cpp
// Get compression statistics
auto stats = file_rotation_service::instance().get_compression_stats();
std::cout << "Files queued: " << stats.files_queued << "\n";
std::cout << "Files compressed: " << stats.files_compressed << "\n";
std::cout << "Files cancelled: " << stats.files_cancelled << "\n";
std::cout << "Queue overflows: " << stats.queue_overflows << "\n";
std::cout << "Current queue size: " << stats.current_queue_size << "\n";
std::cout << "Queue high water mark: " << stats.queue_high_water_mark << "\n";

// Reset statistics (useful for testing)
file_rotation_service::instance().reset_compression_stats();
```

#### Best Practices

1. **Avoid compression for high-throughput systems** - Compression cannot keep up with rapid file rotation
2. **Monitor queue depth** - If queue_overflows > 0, compression is falling behind
3. **Use async over sync** - Async compression prevents rotation blocking but still has throughput limits
4. **Batch processing** - The delay parameter allows batching multiple files for efficiency
5. **Graceful shutdown** - Try to stop the compression thread before application exit if you want predictable shutdown.
6. **ENOSPC handling** - Compression creates temporary files; ensure adequate disk space
7. **Consider alternatives** - For high-throughput systems, use external log rotation tools or compress during off-peak hours

## Performance Tuning

### Compile-Time Optimization

Set `GLOBAL_MIN_LOG_LEVEL` to completely eliminate lower-priority logs from the binary:

```cpp
// In log_types.hpp (default is trace - all logs enabled)
namespace slwoggy {
    inline constexpr log_level GLOBAL_MIN_LOG_LEVEL = log_level::info;
}
```

**Impact of GLOBAL_MIN_LOG_LEVEL**:
- Logs below this level are completely removed at compile time
- Zero runtime overhead (not even a level check)
- No binary size impact
- No site registration
- Cannot be enabled at runtime

```cpp
// With GLOBAL_MIN_LOG_LEVEL = info:
LOG(trace) << "This code is completely eliminated";  // Not compiled
LOG(debug) << "This too";                           // Not compiled  
LOG(info) << "This is included";                    // Compiled
LOG(warn) << "This is included";                    // Compiled
```

Choose based on deployment target:
- `trace`: Development builds with maximum debugging
- `debug`: Testing/staging builds
- `info`: Production builds with operational logging
- `warn`: Production builds with minimal logging
- `error`: Production builds with only error reporting

### Configuration and Tuning

slwoggy's behavior can be adjusted through compile-time constants defined in `log_types.hpp`. These values control memory usage, performance characteristics, and system limits.

#### Memory Configuration

The buffer pool is pre-allocated at startup to avoid runtime allocations:

- **BUFFER_POOL_SIZE**: Number of pre-allocated buffers (default: 128 buffers)
  - Note: Current default is intentionally small for testing
  - For production, see performance tuning guidelines below

- **LOG_BUFFER_SIZE**: Size of each log buffer (default: 2048 bytes)
  - With default settings: 128 buffers × 2KB = ~256KB total pool memory
  - Larger buffers handle longer log messages but use more memory
  - Smaller buffers reduce memory but may truncate long messages

- **LOG_SINK_BUFFER_SIZE**: Intermediate buffer for batching writes (default: 64KB)
  - Used by sinks to collect formatted output before writing
  - Larger values reduce syscalls but increase memory usage

#### Performance Tuning

The relationship between pool size, batch size, and queue size is critical for performance:

**For maximum throughput (full blast mode):**
- **BUFFER_POOL_SIZE** = 512 × number of logging threads
- **MAX_BATCH_SIZE** = 0.5 × BUFFER_POOL_SIZE  
- **MAX_DISPATCH_QUEUE_SIZE** = 0.25 × BUFFER_POOL_SIZE

Example for 4 threads at maximum throughput:
```cpp
inline constexpr size_t BUFFER_POOL_SIZE = 2048;        // 512 × 4 threads
inline constexpr size_t MAX_BATCH_SIZE = 1024;          // 0.5 × pool size
inline constexpr size_t MAX_DISPATCH_QUEUE_SIZE = 512;  // 0.25 × pool size
```

**For balanced performance (default):**
- Current defaults (128/128/128) work well for light to moderate logging
- Suitable for applications with occasional bursts

**For low-latency logging:**
- Reduce MAX_BATCH_SIZE to minimize batching delay
- Keep BUFFER_POOL_SIZE high to avoid blocking
- Reduce BATCH_COLLECT_TIMEOUT for faster dispatch

**Batching timeouts:**

- **BATCH_COLLECT_TIMEOUT**: Maximum time collecting a batch (default: 10μs)
  - Balances between latency and batching efficiency
  - Longer timeouts reduce I/O operations and increase throughput
  - In reliable mode: Longer collection times improve throughput, benefits plateau around 1 second
  - In unreliable mode: Longer collection periods increase message loss rate during bursts
  - Sweet spot for most applications: 100μs to 10ms

- **BATCH_POLL_INTERVAL**: Polling interval during batch collection (default: 1μs)
  - Fine-tunes the responsiveness during batch collection

#### Structured Logging Limits

- **MAX_STRUCTURED_KEYS**: Maximum unique keys per log buffer (default: 255)
  - Hard limit due to metadata format using uint8_t
  - Most applications use far fewer keys per log entry

- **MAX_FORMATTED_SIZE**: Maximum size for structured values (default: 512 bytes)
  - Values exceeding this are truncated
  - Prevents runaway memory usage from large values

#### File Rotation Behavior

Control how aggressively the rotation service retries operations:

- **ROTATION_MAX_RETRIES**: Attempts before giving up (default: 10)
- **ROTATION_INITIAL_BACKOFF**: Starting retry delay (default: 1ms)
- **ROTATION_MAX_BACKOFF**: Maximum retry delay (default: 1s)
- **ROTATION_LINK_ATTEMPTS**: Attempts for atomic link operation (default: 3)

These control resilience against transient filesystem issues like ENOSPC or permission errors.

### Reliable Delivery vs Performance

slwoggy offers two modes for handling buffer pool exhaustion:

```cpp
// In log_types.hpp
#define LOG_RELIABLE_DELIVERY 1  // Default: enabled
```

**With LOG_RELIABLE_DELIVERY enabled (default)**:
- Guarantees no log loss under high load
- Blocks when buffer pool is exhausted
- Writers wait until buffers become available
- Best for: Critical logging, debugging, audit trails

**With LOG_RELIABLE_DELIVERY disabled**:
- Drops logs when buffer pool is exhausted  
- Never blocks the application
- Operations silently no-op when buffer unavailable
- Best for: High-performance production systems

```cpp
// When LOG_RELIABLE_DELIVERY is disabled:
LOG(info) << "This silently fails if buffer pool exhausted";
// No exception, no blocking, just silent drop

// Monitor drops via metrics (if enabled)
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
auto stats = buffer_pool::instance().get_stats();
if (stats.acquire_failures > 0) {
    // Handle buffer pool exhaustion
}
#endif
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

### Custom Log Line Classes

You can create custom log_line classes with custom header writers to add custom prefix text to your logs:

```cpp
#include "slwoggy.hpp"
#include <cstring>

using namespace slwoggy;

// Custom log_line that adds a session ID prefix
class session_log_line : public log_line_base {
private:
    const char* session_id_;
    
public:
    // Constructor that takes session ID
    session_log_line(log_level level, log_module_info& mod, 
                     std::string_view file, uint32_t line,
                     const char* session_id) 
        : log_line_base(level, mod, file, line, true, true), // needs_header=true, human_readable=true
          session_id_(session_id) {}
    
protected:
    // Override write_header to add custom prefix
    size_t write_header() override {
        // Check if buffer is available
        if (!buffer_) return 0;
        
        // Remember starting position
        size_t text_len_before = buffer_->len();
        
        // Write session ID prefix using buffer's write_raw method
        buffer_->write_raw("[Session:");
        buffer_->write_raw(session_id_);
        buffer_->write_raw("] ");
        
        // You can also write timestamp if desired
        auto& dispatcher = log_line_dispatcher::instance();
        int64_t diff_us = std::chrono::duration_cast<std::chrono::microseconds>(
            timestamp_ - dispatcher.start_time()).count();
        int64_t ms = diff_us / 1000;
        
        buffer_->format_to_buffer_with_padding("{:08}.{:03} ", ms, diff_us % 1000);
        
        // Calculate and store header width for padding
        buffer_->header_width_ = buffer_->len() - text_len_before;
        return buffer_->header_width_;
    }
};

// Define custom macro using LOG_BASE
#define SESSION_LOG(level, session_id) \
    LOG_BASE(level, session_log_line, session_id)

// Usage
void handle_request(const char* session_id) {
    SESSION_LOG(info, session_id) << "Processing request";
    // Output: [Session:abc123] 00001234.567 Processing request
    
    SESSION_LOG(debug, session_id) << "Request details: " << request_info;
    // Output: [Session:abc123] 00001234.568 Request details: ...
}

// Or use LOG_BASE directly if you prefer
void alternative_usage() {
    const char* my_session = "xyz789";
    LOG_BASE(warn, session_log_line, my_session) 
        << "Connection timeout";
}
```

Another example - adding thread ID to logs:

```cpp
class thread_log_line : public log_line_base {
public:
    thread_log_line(log_level level, log_module_info& mod, 
                    std::string_view file, uint32_t line) 
        : log_line_base(level, mod, file, line, true, true) {}
    
protected:
    size_t write_header() override {
        if (!buffer_) return 0;
        
        size_t text_len_before = buffer_->len();
        
        // Write thread ID
        auto tid = std::this_thread::get_id();
        buffer_->format_to_buffer_with_padding("[T:{}] ", tid);
        
        // Add standard timestamp and level
        auto& dispatcher = log_line_dispatcher::instance();
        int64_t diff_us = std::chrono::duration_cast<std::chrono::microseconds>(
            timestamp_ - dispatcher.start_time()).count();
        
        buffer_->format_to_buffer_with_padding("{:08}.{:03} [{:<5}] ",
            diff_us / 1000, diff_us % 1000,
            log_level_names[static_cast<int>(level_)]);
        
        buffer_->header_width_ = buffer_->len() - text_len_before;
        return buffer_->header_width_;
    }
};

#define THREAD_LOG(level) LOG_BASE(level, thread_log_line)

// Usage
THREAD_LOG(info) << "Worker thread started";
// Output: [T:140735123456789] 00001234.567 [INFO ] Worker thread started
```

The pattern for custom header writers is:
1. Check if buffer is available (`if (!buffer_) return 0;`)
2. Remember starting position (`size_t text_len_before = buffer_->len();`)
3. Write your custom data using:
   - `buffer_->write_raw()` for raw strings
   - `buffer_->format_to_buffer_with_padding()` for formatted output
4. Update `buffer_->header_width_` with total bytes written
5. Return the header width

This allows you to add any custom prefix like session IDs, thread IDs, request IDs, or any other contextual information to your log lines.

### Per-Site Control

Every LOG() macro invocation in your code is automatically registered as a "site" when first executed. You can control and inspect these sites at runtime:

#### Site Control APIs

```cpp
using namespace slwoggy;

// Set level for a specific LOG() location
log_site_registry::set_site_level("network.cpp", 42, log_level::trace);

// Get level for a specific site
log_level current = log_site_registry::get_site_level("network.cpp", 42);

// Set level for all sites in file(s) - supports wildcards
log_site_registry::set_file_level("database/*.cpp", log_level::error);
log_site_registry::set_file_level("src/net/*", log_level::debug);

// Set level for all sites globally
log_site_registry::set_all_sites_level(log_level::warn);

// Find a specific site
auto* site = log_site_registry::find_site("main.cpp", 100);
if (site) {
    std::cout << "Found site in function: " << site->function << "\n";
}
```

#### Site Introspection

```cpp
// Get count of registered sites (cheap O(1) operation)
size_t count = log_site_registry::get_site_count();
std::cout << "Total log sites: " << count << "\n";

// Get all sites and their settings (WARNING: expensive for large codebases)
// Each LOG() that survives compile-time filtering creates one entry
// A large codebase might have thousands of sites
auto all_sites = log_site_registry::get_all_sites();
for (const auto& site : all_sites) {
    std::cout << site.file << ":" << site.line 
              << " level=" << log_level_names[static_cast<int>(site.min_level)]
              << " function=" << site.function << "\n";
}
```

**Performance Note**: `get_all_sites()` creates a full copy of all site descriptors. In a large codebase with thousands of LOG() statements, this can allocate significant memory and take time. Use `get_site_count()` if you only need the count, or query specific sites instead of enumerating all.

#### Practical Use Cases

```cpp
// Debugging: Enable trace logging for specific problem area
log_site_registry::set_file_level("src/problem_module.cpp", log_level::trace);

// Production: Selectively enable debug for one component
log_site_registry::set_file_level("auth/*", log_level::debug);

// Testing: Count active log sites
std::cout << "Active sites: " << log_site_registry::get_site_count() << "\n";
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

### Build Options

- **LOG_RELIABLE_DELIVERY** (default: ON) - When enabled, LOG() calls block if buffer pool is exhausted instead of dropping messages
- **SLWOGGY_BUILD_TESTS** (default: OFF) - Build test suite
- **SLWOGGY_BUILD_EXAMPLES** (default: ON) - Build example applications

```bash
# Build with reliable delivery disabled (allows message drops)
cmake .. -DLOG_RELIABLE_DELIVERY=OFF

# Build with tests
cmake .. -DSLWOGGY_BUILD_TESTS=ON
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

This creates `amalgamation/slwoggy.hpp` which includes the moodycamel library and all slwoggy headers in one file. Simply copy this file to your project and `#include "slwoggy.hpp"`.

**Note about file paths**: When building with CMake, `SOURCE_FILE_NAME` is defined to show relative paths in log output. The amalgamated header automatically falls back to `__FILE__` when `SOURCE_FILE_NAME` is not defined, so it works out of the box without any build system configuration.

### Pre-Built Downloads

For convenience, automated builds are available that include the amalgamated header and documentation:

**GitHub Releases** (for tagged versions):
- Visit the [Releases page](https://github.com/cdorgby/slwoggy/releases) 
- Download `slwoggy.hpp` directly from any release
- Includes version info and ready-to-use examples

**GitHub Actions Artifacts** (for development builds):
- Go to [Actions](https://github.com/cdorgby/slwoggy/actions/workflows/amalgamation-build.yml)
- Download build artifacts from recent runs
- Contains complete build package with examples and documentation

Each build package includes:
- `slwoggy.hpp` - Single-header library (~1MB)
- `BUILD_README.md` - Quick start guide
- `example.cpp` - Simple usage example  
- `build-example.sh` - Build script
- Full project documentation

**Quick start with a pre-built download:**
1. Download `slwoggy.hpp` from releases or actions
2. Copy to your project: `cp slwoggy.hpp my_project/`
3. Include and use: `#include "slwoggy.hpp"`
4. Compile: `g++ -std=c++20 -pthread my_file.cpp`

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
                          │              Dispatcher Worker Thread               │
                          │         (batch dequeue + filter chains)             │
                          └───────────────────────────────────────────┬─────────┘
                                                                      │
                    ┌───────────────┬─────────────────────┬───────────▼────────┐
                    │ Console Sink  │  Rotating File Sink │   Custom Sink      │
                    │               │  (with compression) │                    │
                    └───────────────┴──────────┬──────────┴────────────────────┘
                                               │
                                               ▼
                                    ┌─────────────────────┐
                                    │  Rotation Service   │
                                    │  (background thread)│
                                    │  - Apply retention  │
                                    │  - Gzip compression │
                                    │  - ENOSPC handling  │
                                    └─────────────────────┘
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
