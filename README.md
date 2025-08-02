# slwoggy

A high-performance, header-only C++20 logging library featuring lock-free asynchronous processing, structured logging, and compile-time optimization.

## Key Features

- **Zero-Copy Architecture**: Pre-allocated buffer pool with reference counting eliminates allocations in the hot path
- **Lock-Free Performance**: Uses moodycamel::ConcurrentQueue for minimal contention between threads
- **Structured Logging**: Efficient key-value metadata with binary storage and global key registry
- **Compile-Time Optimization**: Log sites below `GLOBAL_MIN_LOG_LEVEL` are completely eliminated
- **Module System**: Per-module runtime log level control with wildcard pattern matching
- **Asynchronous Processing**: Dedicated worker thread with batch processing for efficient I/O
- **Type-Erased Sinks**: Flexible output handling with small buffer optimization
- **Platform Optimized**: Fast platform-specific timestamps (mach_absolute_time, CLOCK_MONOTONIC, etc.)

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
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│   LOG()     │────▶│ Buffer Pool  │────▶│   Queue     │
│   Macro     │     │ (lock-free)  │     │ (lock-free) │
└─────────────┘     └──────────────┘     └─────────────┘
                                                │
                          ┌─────────────────────▼─────────┐
                          │      Worker Thread           │
                          │   (batch processing)         │
                          └─────────────────────┬─────────┘
                                                │
                    ┌───────────────┬───────────▼────────┬─────────────┐
                    │ Console Sink  │   File Sink       │ Custom Sink │
                    └───────────────┴────────────────────┴─────────────┘
```

## License

[Your license here]

## Contributing

[Your contributing guidelines here]