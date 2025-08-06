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

## Configuration

### Batching Constants (log_types.hpp)

The following constants control the batching behavior and can be adjusted based on your workload:

```cpp
// Batching configuration constants
inline constexpr auto BATCH_COLLECT_TIMEOUT = std::chrono::microseconds(100); // Max time to collect a batch
inline constexpr auto BATCH_POLL_INTERVAL = std::chrono::microseconds(10);    // Polling interval when collecting

// Buffer pool and batch sizes
inline constexpr size_t MAX_BATCH_SIZE = 4 * 1024;  // Maximum buffers per batch
inline constexpr size_t MAX_DISPATCH_QUEUE_SIZE = 32 * 1024; // Queue capacity
```

Tuning recommendations:
- **Low latency priority**: Reduce `BATCH_COLLECT_TIMEOUT` to 50μs or less
- **High throughput priority**: Increase `BATCH_COLLECT_TIMEOUT` to 200-500μs
- **Very bursty workloads**: Increase `MAX_BATCH_SIZE` to 8192 or 16384
- **Memory constrained**: Reduce `MAX_DISPATCH_QUEUE_SIZE` and `MAX_BATCH_SIZE`

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
cd build/<platform> && make tests  # where <platform> is linux, darwin, windows, etc.

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

### Type Erasure Pattern
The codebase uses a generic `type_erased<ConceptT, BufferSize>` template in `type_erased.hpp`:

#### Key Design Elements:
1. **Small Buffer Optimization**: Objects that fit (size AND alignment) stay on stack
2. **CRTP Helper**: `type_erasure_helper` eliminates boilerplate for simple models
3. **Safety Checks**: 
   - Virtual destructor requirement enforced via static_assert
   - Alignment checks for SBO safety
   - Assertions on empty dereference
4. **Strong Exception Safety**: Copy-and-swap idiom for assignment
5. **Efficient Swap**: Special handling for empty, small, large, and mixed cases

#### Implementation Notes:
- Move semantics: Moved-from small objects are immediately destroyed for predictable state
- The swap implementation uses move operations (not memcpy) to handle non-trivial types
- Requires ConceptT to have: virtual destructor, clone_in_place, move_in_place, heap_clone
- Helper best for single-member models; complex models (like sink_model) need manual implementation

#### Usage Pattern:
```cpp
// Define concept interface
struct my_concept {
    virtual ~my_concept() = default;
    virtual void operation() = 0;
    // Required for type erasure (or use type_erasure_helper)
    virtual my_concept* clone_in_place(void*) const = 0;
    virtual my_concept* move_in_place(void*) noexcept = 0;
    virtual my_concept* heap_clone() const = 0;
};

// Simple model using helper
template<typename T>
struct my_model : type_erasure_helper<my_model<T>, my_concept> {
    T data_;
    explicit my_model(T d) : data_(std::move(d)) {}
    void operation() override { data_.do_something(); }
};

// Use the type erasure
using my_type = type_erased<my_concept, 64>;
my_type obj{my_model<Widget>{Widget{}}};
obj->operation();
```

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
4. **Key Registry Limits**: Maximum 256 unique structured keys by default (configurable via MAX_STRUCTURED_KEYS)
5. **Module Names**: Case-sensitive, no deduplication

## Performance Characteristics

- **Latency**: ~100ns per LOG() in fast path
- **Throughput**: >10M logs/second (single thread)
- **Memory**: ~256MB for default pool
- **CPU**: One dedicated worker thread
- **I/O**: Batched writes reduce syscalls

## Recent Optimizations (2025)

### fmt Library Integration
- Replaced std::format with fmtlib/fmt for better compatibility
- Works with C++11 and up (vs C++20 requirement for std::format)
- Performance gains are modest (5-10%) not dramatic
- Added smart pointer formatting support (std::shared_ptr, std::weak_ptr)

### Structured Logging Optimizations
- Fast-path specializations for common types bypass fmt entirely:
  - **Strings**: Direct memcpy without formatting
  - **Integers**: std::to_chars for fast conversion
  - **Booleans**: Direct "true"/"false" copy
- Exact size calculation instead of conservative estimates
- Reduces metadata drops due to better space utilization

### Hash Map Performance
- Replaced std::unordered_map with robin_hood::unordered_map throughout
- Benefits: 2-3x faster lookups, better cache locality, lower memory overhead
- Used in structured key registry (both thread-local cache and global registry)
- Also used in module registry for consistent performance

### Worker Thread Considerations
The worker_thread_func() is currently NOT ready for multi-threading due to:
- Flush synchronization assumes single consumer
- Statistics collection has race conditions
- Shutdown logic needs per-thread sentinels
Consider whether multi-threading complexity is worth potential gains

### Batching Architecture (2025)

The dispatcher implements a sophisticated three-phase adaptive batching algorithm:

#### Phase 1: Initial Wait
- Uses `wait_dequeue_bulk()` to block indefinitely until data arrives
- Zero CPU usage when idle - thread sleeps until messages arrive
- Avoids polling overhead when system is quiet

#### Phase 2: Bounded Latency Collection
- After first message arrives, tries to collect more within `BATCH_COLLECT_TIMEOUT` (100μs)
- Uses short polls with `BATCH_POLL_INTERVAL` (10μs) to check for additional messages
- Balances between collecting larger batches and maintaining low latency

#### Phase 3: Adaptive Collection
- If Phase 2 collected data, continues polling while data keeps flowing
- Uses 1ms polls (API minimum) to check for more messages
- Stops only when:
  - No more data arrives (poll returns 0), or
  - Buffer is full (`MAX_BATCH_SIZE` = 4096)
- Allows collection of maximum-sized batches during sustained load

#### Performance Characteristics
- **Light load**: Minimal latency, small batches (Phase 1 + partial Phase 2)
- **Burst traffic**: Medium batches collected within timeout (Phase 1 + Phase 2)
- **Sustained load**: Maximum batches of 4096 messages (all phases)
- **Idle**: Zero CPU usage (blocked in Phase 1)

#### Tuning Parameters
- `BATCH_COLLECT_TIMEOUT` (100μs): Maximum time for Phase 2 collection
  - Lower values reduce latency but may miss batching opportunities
  - Higher values improve batching but add latency
- `BATCH_POLL_INTERVAL` (10μs): Polling frequency in Phase 2
  - Lower values find messages faster but use more CPU
  - Higher values are more efficient but may miss messages
- `MAX_BATCH_SIZE` (4096): Maximum messages per batch
  - Larger values reduce processing overhead
  - Must balance with memory usage and cache effects

#### Metrics
The dispatcher tracks comprehensive batching metrics:
- **Batch sizes**: min/avg/max messages per batch
- **Dequeue timing**: min/avg/max time spent collecting batches
- **Processing rates**: messages/second over 1s/10s/60s windows
- **Queue depths**: current and maximum queue sizes

Typical performance with adaptive batching:
- Average batch size: 300-4000 messages (workload dependent)
- Dequeue time: 100-5000μs (depends on wait time and batch size)
- Throughput: >10M messages/second with full batches

## Code Style Guidelines

### File Headers
All header files must include doxygen documentation:
```cpp
/**
 * @file filename.hpp
 * @brief Brief description of file purpose
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once
```

### Smart Pointer Logging
Special handling implemented for smart pointers:
- std::shared_ptr<T>: Logs pointer address or "nullptr"
- std::weak_ptr<T>: Logs pointer address or "(expired)"
- Raw pointers: Already supported with nullptr check

## Development Best Practices

### Code Cleanup Guidelines
- **Do not leave comments around about removed code**
  - Avoid comments like "Removed y function code and moved to X location"
  - Use version control (git) to track code changes instead
  - Keep code clean and remove unnecessary comments

## Buffer Architecture (2025 Refactoring)

### Static Buffer Design
The codebase now uses a C++20 `static_buffer` template for compile-time optimized buffer management:

#### Key Design Elements:
1. **Compile-time region layout**: Template parameters specify region sizes
2. **Alignment guarantees**: `alignas(std::max_align_t)` for SIMD compatibility
3. **Zero-cost abstractions**: `consteval` functions for compile-time calculations
4. **Fold expressions**: Clean compile-time region size calculations
5. **C++20 concepts**: Type-safe interfaces with `requires` clauses

#### Buffer Header Structure:
```cpp
struct alignas(alignof(std::max_align_t)) buffer_header {
    std::atomic<int> ref_count;
    // Ensures data starts at proper alignment boundary
};
```

#### Region Overflow Behavior:
- Fixed-size regions are silently capped if they exceed buffer size
- Allows graceful degradation when buffer sizes vary
- Documented behavior, not a bug - enables runtime flexibility

### Generic Buffer Pool
- Pre-allocated contiguous memory (default 64MB)
- Lock-free acquire/release via ConcurrentQueue
- Statistics collection wrapped in `#ifdef LOG_COLLECT_BUFFER_POOL_METRICS`
- Intrusive reference counting in buffer header

### Memory Ordering Details
The reference counting uses careful memory ordering:
- `fetch_sub(1, acq_rel)`: Ensures decrement can't reorder with prior writes
- Extra `acquire` fence: Cleanup thread sees all writes from other threads
- This is "thread-sanitizer-proof" and critical for correctness

### Template Design Patterns
```cpp
// Region layout with fold expressions
template<size_t... SIZES>
struct region_layout {
    static consteval size_t fixed_total() noexcept {
        return ((SIZES != 0 ? SIZES : 0) + ...);
    }
    static consteval size_t zero_count() noexcept {
        return ((SIZES == 0 ? 1 : 0) + ...);
    }
};

// Type aliases for common patterns
template<size_t HEADER, size_t FOOTER>
using header_body_footer_buffer = static_buffer<HEADER, 0, FOOTER>;
```

### Performance Characteristics (2025)
- **Static buffer append**: ~200ns per operation
- **Pool acquire/release**: ~100μs single-threaded, ~130ns with contention
- **Alignment overhead**: 4 bytes (atomic<int>) → 8 bytes (aligned header)

### Implementation Lessons Learned
1. **Separate implementation headers**: Use `_impl.hpp` for definitions requiring complete types
2. **Avoid mid-file includes**: Keep includes at top, use forward declarations
3. **Share calculation logic**: Extract common code for compile-time and runtime
4. **Document non-obvious behavior**: Especially for graceful degradation cases
5. **Test with actual sizes**: Don't assume sizeof(atomic<int>), check aligned struct sizes