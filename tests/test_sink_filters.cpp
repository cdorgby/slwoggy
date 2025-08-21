/**
 * @file test_sink_filters.cpp
 * @brief Comprehensive test suite for per-sink filtering
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "log.hpp"
#include "log_sinks.hpp"
#include "log_sink_filters.hpp"
#include "log_dispatcher.hpp"
#include "log_formatters.hpp"

using namespace slwoggy;
using namespace Catch::Matchers;

// Test sink that captures filtered output
class FilterTestSink {
public:
    struct LogEntry {
        log_level level;
        std::string message;
    };
    
private:
    mutable std::mutex mutex_;
    mutable std::vector<LogEntry> entries_;
    mutable std::condition_variable cv_;
    
    // Formatter that captures messages
    class capturing_formatter {
        FilterTestSink* parent_;
    public:
        explicit capturing_formatter(FilterTestSink* parent) : parent_(parent) {}
        
        size_t calculate_size(const log_buffer_base* buffer) const {
            raw_formatter fmt{false};
            return fmt.calculate_size(buffer);
        }
        
        size_t format(const log_buffer_base* buffer, char* output, size_t max_size) const {
            raw_formatter fmt{false};
            size_t written = fmt.format(buffer, output, max_size);
            
            // Store the entry
            LogEntry entry;
            entry.level = buffer->level_;
            
            // Parse message from formatted output - format is "TTTTTTTT.mmm [LEVEL] file:line message"
            std::string formatted_output(output, written);
            size_t bracket_pos = formatted_output.find(']');
            if (bracket_pos != std::string::npos) {
                size_t colon_pos = formatted_output.find(':', bracket_pos);
                if (colon_pos != std::string::npos) {
                    size_t space_pos = formatted_output.find(' ', colon_pos);
                    if (space_pos != std::string::npos) {
                        entry.message = formatted_output.substr(space_pos + 1);
                        // Remove trailing newline if present
                        if (!entry.message.empty() && entry.message.back() == '\n') {
                            entry.message.pop_back();
                        }
                    }
                }
            }
            
            {
                std::lock_guard<std::mutex> lock(parent_->mutex_);
                parent_->entries_.push_back(entry);
                parent_->cv_.notify_all();
            }
            
            return written;
        }
    };
    
    // Writer that discards output (we capture in formatter)
    class null_writer {
    public:
        void write(const char*, size_t) const {}
    };
    
    std::shared_ptr<log_sink> sink_;
    
public:
    FilterTestSink() = default;
    
    // Create sink without filter
    std::shared_ptr<log_sink> get_sink() {
        sink_ = std::make_shared<log_sink>(capturing_formatter{this}, null_writer{});
        return sink_;
    }
    
    // Create sink with filter
    template<typename Filter>
    std::shared_ptr<log_sink> get_sink_with_filter(Filter filter) {
        sink_ = std::make_shared<log_sink>(capturing_formatter{this}, null_writer{}, filter);
        return sink_;
    }
    
    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }
    
    bool contains(const std::string& msg) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : entries_) {
            if (entry.message == msg) return true;
        }
        return false;
    }
    
    bool contains_level(log_level level) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : entries_) {
            if (entry.level == level) return true;
        }
        return false;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }
    
    // Wait for a specific number of messages
    bool wait_for_messages(size_t expected, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this, expected]() {
            return entries_.size() >= expected;
        });
    }
    
    std::vector<LogEntry> get_entries() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_;
    }
};

// RAII sink manager - uses set_sink(0) to replace the default sink
class SinkGuard {
    std::shared_ptr<log_sink> sink_;
    std::shared_ptr<log_sink> previous_sink_;
public:
    explicit SinkGuard(std::shared_ptr<log_sink> sink) : sink_(sink) {
        // Replace the first sink (index 0) with our test sink
        // This ensures we only have one sink active during testing
        log_line_dispatcher::instance().set_sink(0, sink_);
    }
    
    ~SinkGuard() {
        // Remove our test sink by setting index 0 to nullptr
        // This prevents the sink from being used after the test
        log_line_dispatcher::instance().set_sink(0, nullptr);
        log_line_dispatcher::instance().flush();
    }
    
    // Prevent copying
    SinkGuard(const SinkGuard&) = delete;
    SinkGuard& operator=(const SinkGuard&) = delete;
};

// Helper to ensure dispatcher is properly cleaned up between tests
struct DispatcherCleanup {
    DispatcherCleanup() {
        log_line_dispatcher::instance().flush();
    }
    
    ~DispatcherCleanup() {
        // Flush and wait for all processing to complete
        log_line_dispatcher::instance().flush();
        // Small delay to ensure worker thread finishes processing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
};

// Null writer class for testing
class null_writer {
public:
    void write(const char*, size_t) const {}
};

// Helper function to create test sinks with different filters
template<typename Filter>
std::shared_ptr<log_sink> create_test_sink(Filter filter) {
    return std::make_shared<log_sink>(
        raw_formatter{false},
        null_writer{},
        filter
    );
}

TEST_CASE("Basic level filtering", "[sink_filters]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("Filter by minimum level") {
        test_sink.clear();  // Clear sink for fresh test
        log_line_dispatcher::instance().flush();  // Ensure previous logs are processed
        
        level_filter filter{log_level::warn};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(trace) << "Trace message" << endl;
        LOG(debug) << "Debug message" << endl;
        LOG(info) << "Info message" << endl;
        LOG(warn) << "Warning message" << endl;
        LOG(error) << "Error message" << endl;
        LOG(fatal) << "Fatal message" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 3);
        REQUIRE_FALSE(test_sink.contains("Trace message"));
        REQUIRE_FALSE(test_sink.contains("Debug message"));
        REQUIRE_FALSE(test_sink.contains("Info message"));
        REQUIRE(test_sink.contains("Warning message"));
        REQUIRE(test_sink.contains("Error message"));
        REQUIRE(test_sink.contains("Fatal message"));
    }
    
    SECTION("Accept all levels when no filter") {
        test_sink.clear();  // Clear sink for fresh test
        SinkGuard guard(test_sink.get_sink());  // No filter
        
        LOG(trace) << "Trace" << endl;
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(warn) << "Warn" << endl;
        LOG(error) << "Error" << endl;
        LOG(fatal) << "Fatal" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 6);
        REQUIRE(test_sink.contains("Trace"));
        REQUIRE(test_sink.contains("Debug"));
        REQUIRE(test_sink.contains("Info"));
        REQUIRE(test_sink.contains("Warn"));
        REQUIRE(test_sink.contains("Error"));
        REQUIRE(test_sink.contains("Fatal"));
    }
    
    SECTION("Filter critical only") {
        test_sink.clear();  // Clear sink for fresh test
        level_filter filter{log_level::fatal};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(warn) << "Warn" << endl;
        LOG(error) << "Error" << endl;
        LOG(fatal) << "Fatal" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 1);
        REQUIRE(test_sink.contains("Fatal"));
    }
}

TEST_CASE("Max level filtering", "[sink_filters]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("Filter by maximum level") {
        test_sink.clear();  // Clear sink for fresh test
        max_level_filter filter{log_level::info};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(trace) << "Trace" << endl;
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(warn) << "Warn" << endl;
        LOG(error) << "Error" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 3);
        REQUIRE(test_sink.contains("Trace"));
        REQUIRE(test_sink.contains("Debug"));
        REQUIRE(test_sink.contains("Info"));
        REQUIRE_FALSE(test_sink.contains("Warn"));
        REQUIRE_FALSE(test_sink.contains("Error"));
    }
}

TEST_CASE("Level range filtering", "[sink_filters]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("Filter by level range") {
        test_sink.clear();  // Clear sink for fresh test
        level_range_filter filter{log_level::debug, log_level::warn};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(trace) << "Trace" << endl;
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(warn) << "Warn" << endl;
        LOG(error) << "Error" << endl;
        LOG(fatal) << "Fatal" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 3);
        REQUIRE_FALSE(test_sink.contains("Trace"));
        REQUIRE(test_sink.contains("Debug"));
        REQUIRE(test_sink.contains("Info"));
        REQUIRE(test_sink.contains("Warn"));
        REQUIRE_FALSE(test_sink.contains("Error"));
        REQUIRE_FALSE(test_sink.contains("Fatal"));
    }
}

TEST_CASE("Composite AND filtering", "[sink_filters]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("AND filter with multiple conditions") {
        test_sink.clear();  // Clear sink for fresh test
        and_filter filter;
        filter.add(level_filter{log_level::debug})  // Must be debug or higher
              .add(max_level_filter{log_level::warn}); // Must be warn or lower
        
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(trace) << "Trace" << endl;
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(warn) << "Warn" << endl;
        LOG(error) << "Error" << endl;
        
        log_line_dispatcher::instance().flush();
        
        // Only debug, info, and warn pass both filters
        REQUIRE(test_sink.count() == 3);
        REQUIRE_FALSE(test_sink.contains("Trace"));
        REQUIRE(test_sink.contains("Debug"));
        REQUIRE(test_sink.contains("Info"));
        REQUIRE(test_sink.contains("Warn"));
        REQUIRE_FALSE(test_sink.contains("Error"));
    }
}

TEST_CASE("Composite OR filtering", "[sink_filters]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("OR filter with multiple conditions") {
        test_sink.clear();  // Clear sink for fresh test
        or_filter filter;
        filter.add(level_filter{log_level::error})    // Either error+ level
              .add(max_level_filter{log_level::trace}); // Or trace level
        
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(trace) << "Trace" << endl;
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(warn) << "Warn" << endl;
        LOG(error) << "Error" << endl;
        LOG(fatal) << "Fatal" << endl;
        
        log_line_dispatcher::instance().flush();
        
        // Trace (passes second filter) and error/fatal (pass first filter)
        REQUIRE(test_sink.count() == 3);
        REQUIRE(test_sink.contains("Trace"));
        REQUIRE_FALSE(test_sink.contains("Debug"));
        REQUIRE_FALSE(test_sink.contains("Info"));
        REQUIRE_FALSE(test_sink.contains("Warn"));
        REQUIRE(test_sink.contains("Error"));
        REQUIRE(test_sink.contains("Fatal"));
    }
}

TEST_CASE("NOT filter", "[sink_filters]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("Invert a level filter") {
        test_sink.clear();  // Clear sink for fresh test
        not_filter filter{level_filter{log_level::error}};  // NOT (error or above)
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(warn) << "Warn" << endl;
        LOG(error) << "Error" << endl;
        LOG(fatal) << "Fatal" << endl;
        
        log_line_dispatcher::instance().flush();
        
        // Everything below error level
        REQUIRE(test_sink.count() == 3);
        REQUIRE(test_sink.contains("Debug"));
        REQUIRE(test_sink.contains("Info"));
        REQUIRE(test_sink.contains("Warn"));
        REQUIRE_FALSE(test_sink.contains("Error"));
        REQUIRE_FALSE(test_sink.contains("Fatal"));
    }
}

TEST_CASE("Multiple sinks with different filters", "[sink_filters]") {
    DispatcherCleanup cleanup;
    
    FilterTestSink errors_only;
    FilterTestSink debug_only;
    FilterTestSink all_messages;
    
    // Test each sink separately since SinkGuard uses set_sink(0)
    // First test: errors only
    {
        SinkGuard guard(errors_only.get_sink_with_filter(level_filter{log_level::error}));
        
        LOG(trace) << "Trace" << endl;
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(warn) << "Warn" << endl;
        LOG(error) << "Error" << endl;
        LOG(fatal) << "Fatal" << endl;
        
        log_line_dispatcher::instance().flush();
    }
    
    // Second test: debug only
    {
        SinkGuard guard(debug_only.get_sink_with_filter(
            level_range_filter{log_level::trace, log_level::debug}));
        
        LOG(trace) << "Trace2" << endl;
        LOG(debug) << "Debug2" << endl;
        LOG(info) << "Info2" << endl;
        LOG(warn) << "Warn2" << endl;
        LOG(error) << "Error2" << endl;
        LOG(fatal) << "Fatal2" << endl;
        
        log_line_dispatcher::instance().flush();
    }
    
    // Third test: all messages
    {
        SinkGuard guard(all_messages.get_sink());
        
        LOG(trace) << "Trace3" << endl;
        LOG(debug) << "Debug3" << endl;
        LOG(info) << "Info3" << endl;
        LOG(warn) << "Warn3" << endl;
        LOG(error) << "Error3" << endl;
        LOG(fatal) << "Fatal3" << endl;
        
        log_line_dispatcher::instance().flush();
    }
    
    // Verify each sink got the right messages
    REQUIRE(errors_only.count() == 2);  // error and fatal
    REQUIRE(errors_only.contains("Error"));
    REQUIRE(errors_only.contains("Fatal"));
    
    REQUIRE(debug_only.count() == 2);  // trace and debug
    REQUIRE(debug_only.contains("Trace2"));
    REQUIRE(debug_only.contains("Debug2"));
    
    REQUIRE(all_messages.count() == 6);  // all messages
    REQUIRE(all_messages.contains("Trace3"));
    REQUIRE(all_messages.contains("Debug3"));
    REQUIRE(all_messages.contains("Info3"));
    REQUIRE(all_messages.contains("Warn3"));
    REQUIRE(all_messages.contains("Error3"));
    REQUIRE(all_messages.contains("Fatal3"));
}

TEST_CASE("Factory functions with filters", "[sink_filters]") {
    DispatcherCleanup cleanup;
    
    SECTION("Create stdout sink with filter") {
        auto sink = make_stdout_sink(level_filter{log_level::warn});
        REQUIRE(sink != nullptr);
    }
}

TEST_CASE("Filter performance with high volume", "[sink_filters][performance]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("Level filter with many messages") {
        test_sink.clear();  // Clear sink for fresh test
        level_filter filter{log_level::error};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Log 1000 messages of various levels
        for (int i = 0; i < 200; ++i) {
            LOG(trace) << "Trace " << i << endl;
            LOG(debug) << "Debug " << i << endl;
            LOG(info) << "Info " << i << endl;
            LOG(warn) << "Warn " << i << endl;
            LOG(error) << "Error " << i << endl;  // Only this should pass
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        log_line_dispatcher::instance().flush();
        
        // Only error messages should have been processed
        REQUIRE(test_sink.count() == 200);
        
        // Performance check - should be fast even with filtering
        INFO("Filter processing time for 1000 messages: " << duration.count() << " ms");
        REQUIRE(duration.count() < 500);  // Should complete in under 500ms
    }
}

TEST_CASE("Thread safety of sink filters", "[sink_filters][threading]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("Multiple threads logging with filters") {
        level_filter filter{log_level::info};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        const int num_threads = 4;
        const int messages_per_thread = 25;
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([t, messages_per_thread]() {
                for (int i = 0; i < messages_per_thread; ++i) {
                    LOG(debug) << "Debug T" << t << " M" << i << endl;  // Filtered out
                    LOG(info) << "Info T" << t << " M" << i << endl;    // Passes filter
                    LOG(error) << "Error T" << t << " M" << i << endl;  // Passes filter
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        log_line_dispatcher::instance().flush();
        
        // Each thread sends messages_per_thread info and error messages
        REQUIRE(test_sink.count() == num_threads * messages_per_thread * 2);
        
        // Verify we got messages from all threads
        for (int t = 0; t < num_threads; ++t) {
            REQUIRE(test_sink.contains("Info T" + std::to_string(t) + " M0"));
            REQUIRE(test_sink.contains("Error T" + std::to_string(t) + " M0"));
            // Debug messages should be filtered out
            REQUIRE_FALSE(test_sink.contains("Debug T" + std::to_string(t) + " M0"));
        }
    }
}

TEST_CASE("Edge case: Empty composite filters", "[sink_filters][edge]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("Empty AND filter accepts all") {
        and_filter filter;  // No conditions added
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(error) << "Error" << endl;
        
        log_line_dispatcher::instance().flush();
        
        // Empty AND means all conditions (none) are met
        REQUIRE(test_sink.count() == 3);
        REQUIRE(test_sink.contains("Debug"));
        REQUIRE(test_sink.contains("Info"));
        REQUIRE(test_sink.contains("Error"));
    }
    
    SECTION("Empty OR filter rejects all") {
        or_filter filter;  // No conditions added
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(debug) << "Debug" << endl;
        LOG(info) << "Info" << endl;
        LOG(error) << "Error" << endl;
        
        log_line_dispatcher::instance().flush();
        
        // Empty OR means no conditions can be met
        REQUIRE(test_sink.count() == 0);
    }
}

TEST_CASE("Filter replacement at runtime", "[sink_filters][runtime]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink1;
    FilterTestSink test_sink2;
    
    SECTION("Replace sink with different filter") {
        // Start with error-only sink
        {
            SinkGuard guard(test_sink1.get_sink_with_filter(level_filter{log_level::error}));
            
            LOG(debug) << "Debug 1" << endl;
            LOG(info) << "Info 1" << endl;
            LOG(error) << "Error 1" << endl;
            
            log_line_dispatcher::instance().flush();
            REQUIRE(test_sink1.count() == 1);
            REQUIRE(test_sink1.contains("Error 1"));
        }
        
        // Replace with debug-only sink
        {
            SinkGuard guard(test_sink2.get_sink_with_filter(max_level_filter{log_level::debug}));
            
            LOG(debug) << "Debug 2" << endl;
            LOG(info) << "Info 2" << endl;
            LOG(error) << "Error 2" << endl;
            
            log_line_dispatcher::instance().flush();
            REQUIRE(test_sink2.count() == 1);
            REQUIRE(test_sink2.contains("Debug 2"));
        }
    }
}

TEST_CASE("Deeply nested composite filters", "[sink_filters][performance]") {
    DispatcherCleanup cleanup;
    FilterTestSink test_sink;
    
    SECTION("Complex nested AND/OR combinations") {
        // Create: (warn+ AND (trace-info OR error+))
        and_filter outer;
        outer.add(level_filter{log_level::warn});
        
        or_filter inner;
        inner.add(level_range_filter{log_level::trace, log_level::info})
             .add(level_filter{log_level::error});
        
        outer.add(inner);
        
        SinkGuard guard(test_sink.get_sink_with_filter(outer));
        
        LOG(trace) << "Trace" << endl;  // Fails outer condition
        LOG(debug) << "Debug" << endl;  // Fails outer condition
        LOG(info) << "Info" << endl;    // Fails outer condition
        LOG(warn) << "Warn" << endl;    // Passes outer, fails inner (not in ranges)
        LOG(error) << "Error" << endl;  // Passes both
        LOG(fatal) << "Fatal" << endl;  // Passes both
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("Error"));
        REQUIRE(test_sink.contains("Fatal"));
        REQUIRE_FALSE(test_sink.contains("Warn"));
    }
    
    SECTION("Triple nested filters") {
        // Create: ((warn+ AND error-) AND (NOT debug))
        and_filter outer;
        
        // First condition: must be warn or higher
        outer.add(level_filter{log_level::warn});
        
        // Second condition: must be in range info to error
        outer.add(level_range_filter{log_level::info, log_level::error});
        
        // Third condition: add another nested OR just to test nesting
        or_filter nested;
        nested.add(level_range_filter{log_level::trace, log_level::fatal});  // Accept all
        outer.add(nested);
        
        SinkGuard guard(test_sink.get_sink_with_filter(outer));
        
        // Test various log levels
        LOG(trace) << "trace" << endl;  // Fails first condition (not warn+)
        LOG(debug) << "debug" << endl;  // Fails first condition 
        LOG(info) << "info" << endl;    // Fails first condition
        LOG(warn) << "warn" << endl;    // Passes all conditions
        LOG(error) << "error" << endl;  // Passes all conditions
        LOG(fatal) << "fatal" << endl;  // Fails second condition (not in info-error range)
        
        REQUIRE(test_sink.wait_for_messages(2, std::chrono::milliseconds(500)));
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("warn"));
        REQUIRE(test_sink.contains("error"));
        REQUIRE_FALSE(test_sink.contains("trace"));
        REQUIRE_FALSE(test_sink.contains("debug"));
        REQUIRE_FALSE(test_sink.contains("info"));
        REQUIRE_FALSE(test_sink.contains("fatal"));
    }
    
    SECTION("Performance with deep nesting") {
        // Create an extremely nested structure to test performance
        and_filter mega_filter;
        
        for (int i = 0; i < 10; ++i) {
            or_filter or_branch;
            for (int j = 0; j < 10; ++j) {
                and_filter and_leaf;
                and_leaf.add(level_filter{log_level::debug})
                       .add(not_filter{level_filter{log_level::fatal}});
                or_branch.add(and_leaf);
            }
            mega_filter.add(or_branch);
        }
        
        SinkGuard guard(test_sink.get_sink_with_filter(mega_filter));
        
        // Measure time for many messages
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 1000; ++i) {
            LOG(debug) << "Message " << i << endl;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        log_line_dispatcher::instance().flush();
        
        INFO("Deep nesting filter time for 1000 messages: " << duration.count() << " us");
        INFO("Average per message: " << duration.count() / 1000.0 << " us");
        
        // All debug messages should pass the complex filter
        REQUIRE(test_sink.count() == 1000);
        
        // Verify performance is still reasonable (< 10us per message on average)
        REQUIRE(duration.count() / 1000.0 < 10.0);
    }
}

TEST_CASE("Module filtering", "[sink_filters][module]") {
    FilterTestSink test_sink;
    
    SECTION("Basic module filter - single module") {
        test_sink.clear();  // Clear sink for fresh test
        module_filter filter{"network"};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        // Use LOG_MOD to specify module name dynamically
        LOG_MOD(info, "network") << "Network message" << endl;
        LOG_MOD(info, "database") << "Database message" << endl;
        LOG_MOD(info, "network") << "Another network message" << endl;
        LOG_MOD(info, "generic") << "Generic message" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("Network message"));
        REQUIRE(test_sink.contains("Another network message"));
        REQUIRE_FALSE(test_sink.contains("Database message"));
        REQUIRE_FALSE(test_sink.contains("Generic message"));
    }
    
    SECTION("Module filter - multiple allowed modules") {
        test_sink.clear();  // Clear sink for fresh test
        module_filter filter{"network", "http", "websocket"};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG_MOD(info, "network") << "Network msg" << endl;
        LOG_MOD(info, "http") << "HTTP msg" << endl;
        LOG_MOD(info, "websocket") << "WebSocket msg" << endl;
        LOG_MOD(info, "database") << "Database msg" << endl;
        LOG_MOD(info, "file") << "File msg" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 3);
        REQUIRE(test_sink.contains("Network msg"));
        REQUIRE(test_sink.contains("HTTP msg"));
        REQUIRE(test_sink.contains("WebSocket msg"));
        REQUIRE_FALSE(test_sink.contains("Database msg"));
        REQUIRE_FALSE(test_sink.contains("File msg"));
    }
    
    SECTION("Module filter - empty allowed list accepts all") {
        test_sink.clear();  // Clear sink for fresh test
        module_filter filter{};  // Empty list (correct syntax)
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG_MOD(info, "any") << "Any module 1" << endl;
        LOG_MOD(info, "other") << "Any module 2" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("Any module 1"));
        REQUIRE(test_sink.contains("Any module 2"));
    }
    
    SECTION("Module exclude filter - single exclusion") {
        test_sink.clear();  // Clear sink for fresh test
        module_exclude_filter filter{"verbose"};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG_MOD(info, "normal") << "Normal message" << endl;
        LOG_MOD(info, "verbose") << "Verbose message" << endl;
        LOG_MOD(info, "important") << "Important message" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("Normal message"));
        REQUIRE(test_sink.contains("Important message"));
        REQUIRE_FALSE(test_sink.contains("Verbose message"));
    }
    
    SECTION("Module exclude filter - multiple exclusions") {
        test_sink.clear();  // Clear sink for fresh test
        module_exclude_filter filter{"trace", "debug", "verbose"};
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG_MOD(info, "app") << "App message" << endl;
        LOG_MOD(info, "trace") << "Trace message" << endl;
        LOG_MOD(info, "debug") << "Debug message" << endl;
        LOG_MOD(info, "verbose") << "Verbose message" << endl;
        LOG_MOD(info, "network") << "Network message" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("App message"));
        REQUIRE(test_sink.contains("Network message"));
        REQUIRE_FALSE(test_sink.contains("Trace message"));
        REQUIRE_FALSE(test_sink.contains("Debug message"));
        REQUIRE_FALSE(test_sink.contains("Verbose message"));
    }
    
    SECTION("Module exclude filter - empty exclusion list accepts all") {
        test_sink.clear();  // Clear sink for fresh test
        module_exclude_filter filter{};  // Empty exclusion list (correct syntax)
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG_MOD(info, "any") << "Any module 1" << endl;
        LOG_MOD(info, "other") << "Any module 2" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("Any module 1"));
        REQUIRE(test_sink.contains("Any module 2"));
    }
}

TEST_CASE("Module filters with composite filters", "[sink_filters][module][composite]") {
    FilterTestSink test_sink;
    
    SECTION("AND filter - module and level") {
        test_sink.clear();  // Clear sink for fresh test
        // Only ERROR and above from network module
        and_filter filter;
        filter.add(module_filter{"network", "http"})
              .add(level_filter{log_level::error});
        
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG_MOD(debug, "network") << "Network debug" << endl;
        LOG_MOD(info, "network") << "Network info" << endl;
        LOG_MOD(warn, "network") << "Network warn" << endl;
        LOG_MOD(error, "network") << "Network error" << endl;
        LOG_MOD(fatal, "network") << "Network fatal" << endl;
        
        LOG_MOD(error, "database") << "Database error" << endl;
        LOG_MOD(fatal, "database") << "Database fatal" << endl;
        
        LOG_MOD(error, "http") << "HTTP error" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 3);
        REQUIRE(test_sink.contains("Network error"));
        REQUIRE(test_sink.contains("Network fatal"));
        REQUIRE(test_sink.contains("HTTP error"));
        REQUIRE_FALSE(test_sink.contains("Network debug"));
        REQUIRE_FALSE(test_sink.contains("Network info"));
        REQUIRE_FALSE(test_sink.contains("Network warn"));
        REQUIRE_FALSE(test_sink.contains("Database error"));
        REQUIRE_FALSE(test_sink.contains("Database fatal"));
    }
    
    SECTION("OR filter - module or level") {
        test_sink.clear();  // Clear sink for fresh test
        // Accept errors from anywhere OR anything from debug module
        or_filter filter;
        filter.add(level_filter{log_level::error})
              .add(module_filter{"debug"});
        
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG_MOD(info, "app") << "App info" << endl;
        LOG_MOD(error, "app") << "App error" << endl;
        
        LOG_MOD(trace, "debug") << "Debug trace" << endl;
        LOG_MOD(info, "debug") << "Debug info" << endl;
        
        LOG_MOD(warn, "network") << "Network warn" << endl;
        LOG_MOD(fatal, "network") << "Network fatal" << endl;
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 4);
        REQUIRE(test_sink.contains("App error"));      // Passes level filter
        REQUIRE(test_sink.contains("Debug trace"));    // Passes module filter
        REQUIRE(test_sink.contains("Debug info"));     // Passes module filter
        REQUIRE(test_sink.contains("Network fatal"));  // Passes level filter
        REQUIRE_FALSE(test_sink.contains("App info"));
        REQUIRE_FALSE(test_sink.contains("Network warn"));
    }
    
    SECTION("Complex nested filter with modules") {
        // (network AND error+) OR (database AND warn+) OR (excluded from verbose)
        or_filter main_filter;
        
        and_filter network_errors;
        network_errors.add(module_filter{"network"})
                      .add(level_filter{log_level::error});
        
        and_filter database_warnings;
        database_warnings.add(module_filter{"database"})
                         .add(level_filter{log_level::warn});
        
        // Exclude verbose module entirely
        module_exclude_filter exclude_verbose{"verbose"};
        
        main_filter.add(network_errors)
                   .add(database_warnings);
        
        // Wrap in AND to apply the exclusion
        and_filter final_filter;
        final_filter.add(main_filter)
                    .add(exclude_verbose);
        
        SinkGuard guard(test_sink.get_sink_with_filter(final_filter));
        
        LOG_MOD(info, "network") << "Network info" << endl;
        LOG_MOD(error, "network") << "Network error" << endl;
        
        LOG_MOD(debug, "database") << "Database debug" << endl;
        LOG_MOD(warn, "database") << "Database warn" << endl;
        LOG_MOD(error, "database") << "Database error" << endl;
        
        LOG_MOD(error, "verbose") << "Verbose error" << endl;  // Should be excluded
        
        LOG_MOD(fatal, "app") << "App fatal" << endl;  // Doesn't match any condition
        
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 3);
        REQUIRE(test_sink.contains("Network error"));
        REQUIRE(test_sink.contains("Database warn"));
        REQUIRE(test_sink.contains("Database error"));
        REQUIRE_FALSE(test_sink.contains("Network info"));
        REQUIRE_FALSE(test_sink.contains("Database debug"));
        REQUIRE_FALSE(test_sink.contains("Verbose error"));
        REQUIRE_FALSE(test_sink.contains("App fatal"));
    }
}