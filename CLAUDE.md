# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with the slwoggy logging library codebase.

## Project Overview

slwoggy is a header-only C++20 logging library that provides asynchronous logging with structured data support. The library uses lock-free data structures and pre-allocated buffers to minimize allocations during logging.

## Architecture

### Core Components

1. **log_buffer** (`log_buffer.hpp`)
   - Fixed-size buffers (2KB) with reference counting
   - Bidirectional growth: text forward, metadata backward
   - Cache-line aligned to prevent false sharing
   - Automatic padding for multi-line logs
   - Thread-local ProducerToken using unique_ptr for proper cleanup
   - Stores module pointer for efficient sink filtering
   - Contains all metadata (timestamp, level, module, file, line)

2. **buffer_pool** (`log_buffer.hpp`)
   - Pre-allocated pool of 32K buffers
   - Lock-free acquire/release using ConcurrentQueue
   - Singleton pattern with lazy initialization
   - Metrics tracking (optional)
   - Thread-local ProducerToken to avoid memory leaks

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
   - Skips buffers marked as filtered

5. **Filter System** (`log_filter.hpp`, `log_filters.hpp`)
   - RCU-based filter chain management
   - Zero-allocation filtering via buffer flags
   - Example filters: dedup, rate limit, sampler, module, module_exclude
   - Filters run in dispatcher worker thread only
   - Filters set `filtered_` flag on buffers to drop
   - Module filters enable efficient log routing to specialized sinks
   - Composite filters (and_filter, or_filter, not_filter) for complex rules

6. **Module Registry** (`log_module.hpp`)
   - Thread-safe module management
   - Runtime log level control
   - Wildcard pattern matching
   - Per-compilation-unit configuration

7. **Site Registry** (`log_site.hpp`)
   - Automatic registration of all LOG() locations
   - Compile-time filtering
   - Runtime per-site control
   - Filename width tracking

8. **Structured Key Registry** (`log_structured.hpp`)
   - Pre-registered internal keys (IDs 0-4)
   - Ultra-fast path for internal key lookups
   - Thread-local caching for user keys
   - Lock-free fast path, shared lock medium path

9. **File Rotation Service** (`log_file_rotator.hpp`, `log_file_rotator_impl.hpp`)
   - Singleton service with background thread for rotation operations
   - Zero-gap rotation using atomic link+rename operations
   - Comprehensive policies: size, time, or combined triggers
   - Automatic ENOSPC handling with cleanup priority (.pending → .gz → raw)
   - Retention management by count, age, and total size
   - Optional gzip compression for rotated files (sync or async)
   - Platform-specific sync (fdatasync/F_FULLFSYNC/_commit)
   - Thread-safe rotation metrics with structured stats interface
   - Auto-detection of non-regular files (pipes, devices) with rotation disabled
   
10. **Compression Thread** (`log_file_rotator_impl.hpp`)
   - Optional background thread for asynchronous compression
   - State machine: idle → queued → compressing → done/cancelled
   - Graceful cancellation when files deleted by retention
   - Configurable batching delay for efficiency
   - Bounded queue to prevent memory exhaustion
   - Comprehensive statistics API with high-water mark tracking

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
- Compile-time path shortening with get_path_suffix()
- file_source() macro for consistent, shorter file paths

### Structured Logging Design
- Binary metadata format for efficiency
- 16-bit key IDs instead of strings
- Global key registry with thread caching
- Bidirectional buffer: text grows forward, metadata backward
- Dynamic allocation with collision detection
- Pre-registered internal keys with guaranteed IDs:
  - `_ts` (ID 0) - Timestamp
  - `_level` (ID 1) - Log level
  - `_module` (ID 2) - Module name
  - `_file` (ID 3) - Source file
  - `_line` (ID 4) - Source line
- Ultra-fast path for internal keys using direct comparison
- Switch-based reverse lookup for internal key IDs

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

## File Rotation System

### Architecture Overview

The file rotation system consists of three main components:

1. **rotation_handle**: Thread-safe handle managing file descriptors and rotation state
2. **file_rotation_service**: Singleton service running background rotation thread
3. **rotation_metrics**: Global metrics tracking rotation behavior

### Key Implementation Details

#### Zero-Gap Rotation
The system achieves zero-gap rotation (no log loss) using:
1. Pre-prepared next file descriptor before rotation
2. Atomic FD swap in writer thread
3. Link+rename for atomic file movement
4. Fallback to simple rename if link fails

#### ENOSPC Handling
When disk space is exhausted, automatic cleanup occurs in priority order:
1. Delete `.pending` files (incomplete compressions)
2. Delete `.gz` files (compressed logs) 
3. Delete oldest raw log files
4. Track all deletions in metrics

#### Time-Based Rotation
Correctly handles:
- Daily rotation at specified UTC time
- Arbitrary intervals (seconds precision)
- Next rotation time computation after each rotation
- Clock adjustments and timezone considerations

#### Thread Safety
- Rotation handle uses atomics for all shared state
- Service uses blocking queue for message passing
- Metrics use relaxed atomics for counters
- No locks in write path

### Platform Compatibility

#### File Sync Operations
```cpp
// Platform-specific implementation in log_file_rotator_impl.hpp
#ifdef __APPLE__
    fcntl(fd, F_FULLFSYNC);  // macOS full sync
#elif defined(_WIN32)
    _commit(fd);             // Windows commit
#else
    fdatasync(fd);           // Linux/POSIX data sync
#endif
```

#### Error String Handling
```cpp
// GNU vs POSIX strerror_r compatibility
#ifdef _GNU_SOURCE
    // GNU version returns char*
    return strerror_r(errno_val, buffer, sizeof(buffer));  
#else
    // POSIX version returns int
    strerror_r(errno_val, buffer, sizeof(buffer));
    return buffer;
#endif
```

#### iovec Limits
```cpp
// Platform-specific limits for writev operations
#ifdef UIO_MAXIOV
    static constexpr size_t WRITER_MAX_IOV = UIO_MAXIOV;
#elif defined(IOV_MAX)  
    static constexpr size_t WRITER_MAX_IOV = IOV_MAX;
#else
    static constexpr size_t WRITER_MAX_IOV = 1024;
#endif
```

### Testing Rotation Features

```cpp
// Example test for ENOSPC handling
TEST_CASE("ENOSPC emergency cleanup") {
    // Requires TEST_TMPFS_DIR environment variable
    // pointing to a small tmpfs mount
    const char* tmpfs = std::getenv("TEST_TMPFS_DIR");
    if (!tmpfs) {
        SKIP("TEST_TMPFS_DIR not set");
    }
    
    rotate_policy policy;
    policy.mode = rotate_policy::kind::size;
    policy.max_bytes = 50 * 1024;  // 50KB files
    policy.keep_files = 100;  // Try to keep many
    
    auto sink = make_raw_file_sink(tmpfs + "/test.log"s, policy);
    // Write until ENOSPC triggers cleanup...
}
```

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

### Creating Module-Filtered Sinks

```cpp
using namespace slwoggy;

// Route specific modules to dedicated files
auto network_sink = make_raw_file_sink("network.log", {},
    module_filter{{"network", "http", "websocket"}});

auto db_sink = make_raw_file_sink("database.log", {},
    module_filter{{"database", "sql", "cache"}});

// Exclude verbose modules from main log
auto main_sink = make_raw_file_sink("app.log", {},
    module_exclude_filter{{"trace", "debug_internal"}});

// Complex filtering with composite filters
and_filter critical_errors;
critical_errors.add(module_filter{{"security", "auth"}})
               .add(level_filter{log_level::error});
auto alert_sink = make_stdout_sink(critical_errors);

// Add all sinks
log_line_dispatcher::instance().add_sink(network_sink);
log_line_dispatcher::instance().add_sink(db_sink);
log_line_dispatcher::instance().add_sink(main_sink);
log_line_dispatcher::instance().add_sink(alert_sink);
```

### Creating a Rotating File Sink

```cpp
using namespace slwoggy;

// Configure rotation policy
rotate_policy policy;
policy.mode = rotate_policy::kind::size_or_time;
policy.max_bytes = 100 * 1024 * 1024;  // 100MB
policy.every = std::chrono::hours(24);  // Daily
policy.at = std::chrono::hours(3);      // At 3 AM UTC
policy.keep_files = 30;                 // Keep 30 files
policy.compress = true;                 // Gzip old files
policy.sync_on_rotate = true;           // Ensure durability

// Optional: Start async compression thread (recommended for production)
file_rotation_service::instance().start_compression_thread(
    std::chrono::milliseconds{500},  // Batch delay
    10                                // Max queue size
);

// Create sink with rotation
auto sink = make_writev_file_sink("/var/log/app.log", policy);
log_line_dispatcher::instance().add_sink(sink);
```

### Monitoring Compression

```cpp
// Get compression statistics
auto stats = file_rotation_service::instance().get_compression_stats();
LOG(info) << "Compression queue: " << stats.current_queue_size 
          << "/" << stats.queue_high_water_mark;

// Reset statistics for testing
file_rotation_service::instance().reset_compression_stats();
```

## Build System

### Build Modes

- **Release**: `-O3`, `NDEBUG`, no metrics
- **Debug**: `-O0`, `-g`, all metrics enabled
- **MemCheck**: Debug + sanitizers
- **Profile**: `-O2`, `-g`, metrics, frame pointers

### Metric Flags

Enable in Debug/Profile builds:
- `LOG_COLLECT_BUFFER_POOL_METRICS` - Buffer pool statistics
- `LOG_COLLECT_DISPATCHER_METRICS` - Dispatcher batch/timing metrics
- `LOG_COLLECT_STRUCTURED_METRICS` - Structured key registry metrics
- `LOG_COLLECT_DISPATCHER_MSG_RATE` - Message rate tracking (1s/10s/60s)

Rotation metrics are always collected (no compile flag needed):
```cpp
auto stats = rotation_metrics::instance().get_stats();
std::cout << "Rotations: " << stats.total_rotations << "\n";
std::cout << "ENOSPC cleanups: " << stats.enospc_raw_deleted << "\n";
```

### Testing

```bash
# Build tests on-demand
cd build && make tests

# Run all tests (no such binary - run individual test executables)
./tests/test_log
./tests/test_rotation
./tests/test_log_structured
# ... etc

# Run specific test case by name
./tests/test_rotation "Size-based rotation"

# Run specific test section with -c flag
./tests/test_rotation -c "Compression cancellation"

# Test compression features
./tests/test_rotation -c "Non-regular file rotation disabled"
./tests/test_rotation -c "Compression statistics tracking"

# Test ENOSPC handling (requires tmpfs)
# Create a 1MB tmpfs for testing:
# Linux: sudo mount -t tmpfs -o size=1M tmpfs /tmp/test_tmpfs
# macOS: diskutil erasevolume HFS+ 'test_tmpfs' `hdiutil attach -nomount ram://2048`
TEST_TMPFS_DIR=/tmp/test_tmpfs ./tests/test_rotation "[enospc]"

# With sanitizers
ASAN_OPTIONS=detect_leaks=1 ./tests/test_log
UBSAN_OPTIONS=print_stacktrace=1 ./tests/test_rotation

# List available tests
./tests/test_rotation --list-tests
./tests/test_rotation --list-tags
```

### Ubuntu Testing Checklist

When testing on Ubuntu (or other Linux distributions):

1. **Compiler Compatibility**
   ```bash
   # Ensure C++20 support
   g++ --version  # Should be >= 10
   clang++ --version  # Should be >= 12
   ```

2. **Build All Configurations**
   ```bash
   for config in Release Debug MemCheck Profile; do
       cmake -B build-$config -DCMAKE_BUILD_TYPE=$config
       cmake --build build-$config -j$(nproc)
   done
   ```

3. **Run Test Suite**
   ```bash
   cd build-Release && ctest --verbose
   cd ../build-Debug && ctest --verbose
   ```

4. **Platform-Specific Tests**
   ```bash
   # Test fdatasync availability
   ./tests/test_rotation "[sync]"
   
   # Test iovec limits
   ./tests/test_log "[writev]"
   
   # Test GNU strerror_r
   ./tests/test_rotation "[error_handling]"
   ```

5. **Sanitizer Runs**
   ```bash
   cd build-MemCheck
   ASAN_OPTIONS=detect_leaks=1:check_initialization_order=1 ./tests/all_tests
   UBSAN_OPTIONS=print_stacktrace=1 ./tests/all_tests
   ```

6. **Performance Testing**
   ```bash
   cd build-Profile
   ./bin/slwoggy -b  # Benchmark mode
   ./bin/rotation_demo  # Should show 2 rotations, not 4000+
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
- Metadata grows dynamically from buffer end
- Use shorter key names

### Module Issues
- Verify with `get_all_modules()`
- Check module name spelling
- Use `configure_from_string()` carefully
- Module filters require exact string match
- Module names are case-sensitive
- Filters work on dispatcher thread (after LOG() returns)

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
- macOS: `mach_absolute_time()`, F_FULLFSYNC for file sync
- Linux: `CLOCK_MONOTONIC_COARSE`, fdatasync for file sync
- Windows: `GetTickCount64()`, _commit for file sync
- iovec limits: UIO_MAXIOV (Linux/macOS), IOV_MAX fallback, 1024 default
- strerror_r: GNU vs POSIX variants handled via _GNU_SOURCE check

## Recently Completed Features

- **File Rotation**: Complete implementation with size/time policies
- **Compression**: Automatic gzip compression for rotated files (sync and async)
- **Filter Chains**: RCU-based filter system with examples
- **Sampling/Rate Limiting**: Implemented as example filters
- **Async Compression Thread**: Optional background compression with cancellation
- **Non-Regular File Detection**: Auto-disable rotation for pipes, devices, etc.
- **Compression Statistics API**: Track queue depth, overflows, and throughput
- **Filename Collision Fix**: Proper handling of .log and .gz filename generation
- **Thread-Safe Test Cleanup**: RAII pattern for thread cleanup in tests
- **Module-Based Sink Filtering**: Route logs to different sinks based on module
- **Metadata Refactoring**: Moved all metadata to log_buffer for better efficiency
- **Compile-Time Path Shortening**: get_path_suffix() and file_source() for cleaner logs
- **Composite Filters**: and_filter, or_filter, not_filter for complex filtering rules

## Future Enhancements

Potential areas for extension:
- Network sinks (syslog, TCP)
- Coroutine support
- Remote configuration
- Structured output formats (CBOR, MessagePack)

## Important Warnings

1. **No Logging in Destructors**: Avoid LOG() in global/static destructors
2. **Thread Termination**: Ensure threads join before shutdown
3. **Sink Lifetime**: Sinks must outlive the dispatcher
4. **Key Registry Limits**: Maximum 256 unique structured keys
5. **Module Names**: Case-sensitive, no deduplication
6. **Rotation Handle Lifetime**: Rotation handles must be properly closed
7. **ENOSPC Handling**: Retention policies may be violated during disk exhaustion
8. **Thread-Local Storage**: ProducerTokens use unique_ptr to prevent leaks
9. **Compression with High Throughput**: Do NOT use compression (sync or async) with high-throughput logging - can cause data loss or UB
10. **Non-Regular Files**: Rotation automatically disabled for /dev/null, pipes, sockets
11. **Compression Thread Shutdown**: Call stop_compression_thread() in destructor to prevent crashes
12. **Module Filter Performance**: Module filtering happens in dispatcher thread, not at LOG() site
13. **Buffer Metadata**: All metadata now in log_buffer, log_line is text-only
14. **Filter Order**: Filters in composite filters are evaluated in the order they were added

## Performance Characteristics

- **Latency**: ~100ns per LOG() in fast path
- **Throughput**: >10M logs/second (single thread)
- **Memory**: ~64MB for default pool (32K * 2KB)
- **CPU**: One dedicated worker thread
- **I/O**: Batched writes reduce syscalls
- **Structured Logging**: 
  - Internal keys: Direct comparison/switch (no hash lookup)
  - User keys: Thread-local cache hit ~5ns, miss ~50ns