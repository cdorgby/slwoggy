# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with the slwoggy logging library codebase.

## Project Overview

slwoggy is a header-only C++20 logging library that provides asynchronous logging with structured data support. The library uses lock-free data structures and pre-allocated buffers to minimize allocations during logging.

## Architecture

### Core Components

1. **log_buffer** (`log_buffer.hpp`)
   - Fixed-size buffers (2KB) with reference counting
   - Metadata section at start for structured logging
   - Cache-line aligned to prevent false sharing
   - Automatic padding for multi-line logs

2. **buffer_pool** (`log_buffer.hpp`)
   - Pre-allocated pool of 128K buffers
   - Lock-free acquire/release using ConcurrentQueue
   - Singleton pattern with lazy initialization
   - Metrics tracking (optional)

3. **log_line_dispatcher** (`log.hpp`)
   - Singleton managing async log processing
   - Worker thread with batch dequeue
   - RCU-style sink management
   - Flush synchronization via markers

4. **Sink System** (`log_sink.hpp`)
   - Type-erased sinks using small buffer optimization
   - Formatter + Writer composition
   - Batch processing interface
   - Move semantics support

5. **Module Registry** (`log_module.hpp`)
   - Thread-safe module management
   - Runtime log level control
   - Wildcard pattern matching
   - Per-compilation-unit configuration

6. **Site Registry** (`log_site.hpp`)
   - Automatic registration of all LOG() locations
   - Compile-time filtering
   - Runtime per-site control
   - Filename width tracking

## Key Design Decisions

### Lock-Free Architecture
- moodycamel::ConcurrentQueue for message passing
- Atomic operations for reference counting
- RCU pattern for sink updates
- Thread-local caching for performance

### Zero-Allocation Fast Path
- Pre-allocated buffer pool
- In-place construction
- Reference counting instead of copying
- Stack allocation for small format operations

### Compile-Time Optimization
- `if constexpr` for level filtering
- Static site registration
- Template-based formatters
- Macro-based source location

### Structured Logging Design
- Binary metadata format for efficiency
- 16-bit key IDs instead of strings
- Global key registry with thread caching
- Compact storage in buffer header

## Development Guidelines

### Adding New Features

When adding features to slwoggy:

1. **Maintain Header-Only Design**
   - All implementation in headers
   - Use `inline` for function definitions
   - Template implementations where appropriate

2. **Preserve Lock-Free Operations**
   - Avoid mutexes in hot paths
   - Use atomic operations carefully
   - Consider RCU for rarely-updated data

3. **Zero-Allocation Principle**
   - No `new`/`malloc` in logging path
   - Use buffer pool for temporary storage
   - Stack allocation for small data

4. **Metrics Collection**
   - Guard with `#ifdef LOG_COLLECT_*`
   - Use relaxed atomics for counters
   - Provide reset methods for testing

### Testing Considerations

The asynchronous nature requires careful test design:

1. **Synchronization**
   ```cpp
   using namespace slwoggy;
   
   // Always wait for logs to process
   LOG(info) << "Test message" << endl;
   log_line_dispatcher::instance().flush();
   ```

2. **Test Sinks**
   - Must implement proper synchronization
   - Use condition variables for waiting
   - Call `add_ref()/release()` on buffers

3. **Global State**
   - Clear registries between tests
   - Reset metrics counters
   - Ensure clean sink state

### Performance Optimization

1. **Batch Processing**
   - Process multiple buffers per iteration
   - Respect flush markers
   - Minimize syscalls

2. **Memory Layout**
   - Cache-line alignment for buffers
   - Separate read/write data
   - Minimize false sharing

3. **Platform-Specific Code**
   - Fast timestamps per platform
   - Optimal memory barriers
   - Native I/O operations

## Common Tasks

### Adding a New Formatter

```cpp
namespace slwoggy {

class my_formatter {
public:
    size_t calculate_size(const log_buffer* buffer) const {
        // Calculate formatted size
    }
    
    size_t format(const log_buffer* buffer, char* output, size_t max_size) const {
        // Format into output buffer
        // Return bytes written
    }
};

} // namespace slwoggy
```

### Adding a New Writer

```cpp
namespace slwoggy {

class my_writer {
public:
    void write(const char* data, size_t len) const {
        // Output data somewhere
    }
};

} // namespace slwoggy
```

### Creating a Custom Sink

```cpp
using namespace slwoggy;

auto my_sink = log_sink{
    my_formatter{},
    my_writer{}
};

log_line_dispatcher::instance().add_sink(
    std::make_shared<log_sink>(std::move(my_sink))
);
```

## Build System

### Build Modes

- **Release**: `-O3`, `NDEBUG`, no metrics
- **Debug**: `-O0`, `-g`, all metrics enabled
- **MemCheck**: Debug + sanitizers
- **Profile**: `-O2`, `-g`, metrics, frame pointers

### Metric Flags

Enable in Debug/Profile builds:
- `LOG_COLLECT_BUFFER_POOL_METRICS`
- `LOG_COLLECT_DISPATCHER_METRICS`
- `LOG_COLLECT_STRUCTURED_METRICS`
- `LOG_COLLECT_DISPATCHER_MSG_RATE`

### Testing

```bash
# Build tests on-demand
cd build && make tests

# Run all tests
./tests/all_tests

# Run specific test
./tests/test_log "[buffer_pool]"

# With sanitizers
ASAN_OPTIONS=detect_leaks=1 ./tests/test_log
```

## Debugging Tips

### Buffer Pool Exhaustion
- Check `acquire_failures` in pool stats
- Increase `BUFFER_POOL_SIZE`
- Look for log bursts

### Queue Congestion
- Monitor `max_queue_size`
- Check sink performance
- Consider batch size tuning

### Metadata Drops
- Check drop statistics
- Increase `METADATA_RESERVE`
- Use shorter key names

### Module Issues
- Verify with `get_all_modules()`
- Check module name spelling
- Use `configure_from_string()` carefully

## Implementation Notes

### Singleton Lifecycle
- Automatic initialization on first use
- Destruction in reverse order
- No explicit init/shutdown needed

### Static Initialization
- Site registration via static locals
- Module configuration via namespace statics
- Order between TUs not guaranteed

### Memory Ordering
- Relaxed for statistics counters
- Acquire/release for buffer refcounts
- Sequential consistency for flush sync

### Platform Differences
- macOS: `mach_absolute_time()`
- Linux: `CLOCK_MONOTONIC_COARSE`
- Windows: `GetTickCount64()`

## Future Enhancements

Potential areas for extension:
- Log rotation in file sink
- Network sinks (syslog, TCP)
- Compression support
- Filter chains
- Sampling/rate limiting
- Coroutine support

## Important Warnings

1. **No Logging in Destructors**: Avoid LOG() in global/static destructors
2. **Thread Termination**: Ensure threads join before shutdown
3. **Sink Lifetime**: Sinks must outlive the dispatcher
4. **Key Registry Limits**: Maximum 256 unique structured keys
5. **Module Names**: Case-sensitive, no deduplication

## Performance Characteristics

- **Latency**: ~100ns per LOG() in fast path
- **Throughput**: >10M logs/second (single thread)
- **Memory**: ~256MB for default pool
- **CPU**: One dedicated worker thread
- **I/O**: Batched writes reduce syscalls