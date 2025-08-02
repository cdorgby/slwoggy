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

## Quick Start

```cpp
#include "log.hpp"

using namespace slwoggy;

// Module declaration (optional, defaults to "generic")
LOG_MODULE_NAME("myapp");

int main() {
    // Basic logging
    LOG(info) << "Application started" << endl;
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

// Use in logging
LOG(info).add("user_id", user.id)
         .add("request_id", req.id)
         .add("latency_ms", elapsed.count())
    << "Request completed successfully" << endl;
```

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
    inline constexpr size_t BUFFER_POOL_SIZE = 128 * 1024;  // Number of buffers
    inline constexpr size_t LOG_BUFFER_SIZE = 2048;         // Bytes per buffer
    inline constexpr size_t METADATA_RESERVE = 256;         // Reserved for structured data
}
```

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
```

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
