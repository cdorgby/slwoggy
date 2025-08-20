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
        sink_ = std::make_shared<log_sink>(capturing_formatter{this}, null_writer{}, std::move(filter));
        return sink_;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }
    
    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }
    
    bool wait_for_messages(size_t expected, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this, expected] {
            return entries_.size() >= expected;
        });
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
    
    size_t count_level(log_level level) const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t cnt = 0;
        for (const auto& entry : entries_) {
            if (entry.level == level) cnt++;
        }
        return cnt;
    }
};

// RAII helper to manage sink replacement
class SinkGuard {
private:
    std::shared_ptr<log_sink> original_sink_;
    
public:
    explicit SinkGuard(std::shared_ptr<log_sink> new_sink) {
        auto& dispatcher = log_line_dispatcher::instance();
        // Save original sink if it exists
        original_sink_ = dispatcher.get_sink(0);
        // Replace with new sink
        dispatcher.set_sink(0, new_sink);
    }
    
    ~SinkGuard() {
        log_line_dispatcher::instance().flush();
        // Restore original sink
        log_line_dispatcher::instance().set_sink(0, original_sink_);
    }
    
    // Delete copy/move operations
    SinkGuard(const SinkGuard&) = delete;
    SinkGuard& operator=(const SinkGuard&) = delete;
    SinkGuard(SinkGuard&&) = delete;
    SinkGuard& operator=(SinkGuard&&) = delete;
};

TEST_CASE("Basic level filtering", "[sink_filters]") {
    FilterTestSink test_sink;
    
    SECTION("No filter - receives all messages") {
        SinkGuard guard(test_sink.get_sink());
        
        LOG(trace) << "trace message" << endl;
        LOG(debug) << "debug message" << endl;
        LOG(info) << "info message" << endl;
        LOG(warn) << "warn message" << endl;
        LOG(error) << "error message" << endl;
        LOG(fatal) << "fatal message" << endl;
        
        REQUIRE(test_sink.wait_for_messages(6));
        REQUIRE(test_sink.count() == 6);
        REQUIRE(test_sink.contains("trace message"));
        REQUIRE(test_sink.contains("debug message"));
        REQUIRE(test_sink.contains("info message"));
        REQUIRE(test_sink.contains("warn message"));
        REQUIRE(test_sink.contains("error message"));
        REQUIRE(test_sink.contains("fatal message"));
    }
    
    SECTION("Level filter - only warnings and above") {
        SinkGuard guard(test_sink.get_sink_with_filter(level_filter{log_level::warn}));
        
        LOG(trace) << "trace message" << endl;
        LOG(debug) << "debug message" << endl;
        LOG(info) << "info message" << endl;
        LOG(warn) << "warn message" << endl;
        LOG(error) << "error message" << endl;
        LOG(fatal) << "fatal message" << endl;
        
        REQUIRE(test_sink.wait_for_messages(3));
        REQUIRE(test_sink.count() == 3);
        REQUIRE_FALSE(test_sink.contains("trace message"));
        REQUIRE_FALSE(test_sink.contains("debug message"));
        REQUIRE_FALSE(test_sink.contains("info message"));
        REQUIRE(test_sink.contains("warn message"));
        REQUIRE(test_sink.contains("error message"));
        REQUIRE(test_sink.contains("fatal message"));
    }
    
    SECTION("Level filter - only errors and above") {
        SinkGuard guard(test_sink.get_sink_with_filter(level_filter{log_level::error}));
        
        LOG(trace) << "trace" << endl;
        LOG(debug) << "debug" << endl;
        LOG(info) << "info" << endl;
        LOG(warn) << "warn" << endl;
        LOG(error) << "error" << endl;
        LOG(fatal) << "fatal" << endl;
        
        REQUIRE(test_sink.wait_for_messages(2));
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("error"));
        REQUIRE(test_sink.contains("fatal"));
        REQUIRE_FALSE(test_sink.contains("warn"));
    }
}

TEST_CASE("Max level filtering", "[sink_filters]") {
    FilterTestSink test_sink;
    
    SECTION("Max level debug - only trace and debug") {
        SinkGuard guard(test_sink.get_sink_with_filter(max_level_filter{log_level::debug}));
        
        LOG(trace) << "trace" << endl;
        LOG(debug) << "debug" << endl;
        LOG(info) << "info" << endl;
        LOG(warn) << "warn" << endl;
        LOG(error) << "error" << endl;
        
        REQUIRE(test_sink.wait_for_messages(2, std::chrono::milliseconds(500)));
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("trace"));
        REQUIRE(test_sink.contains("debug"));
        REQUIRE_FALSE(test_sink.contains("info"));
        REQUIRE_FALSE(test_sink.contains("warn"));
        REQUIRE_FALSE(test_sink.contains("error"));
    }
}

TEST_CASE("Level range filtering", "[sink_filters]") {
    FilterTestSink test_sink;
    
    SECTION("Range info to warn") {
        SinkGuard guard(test_sink.get_sink_with_filter(
            level_range_filter{log_level::info, log_level::warn}));
        
        LOG(trace) << "trace" << endl;
        LOG(debug) << "debug" << endl;
        LOG(info) << "info" << endl;
        LOG(warn) << "warn" << endl;
        LOG(error) << "error" << endl;
        LOG(fatal) << "fatal" << endl;
        
        REQUIRE(test_sink.wait_for_messages(2, std::chrono::milliseconds(500)));
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("info"));
        REQUIRE(test_sink.contains("warn"));
        REQUIRE_FALSE(test_sink.contains("trace"));
        REQUIRE_FALSE(test_sink.contains("debug"));
        REQUIRE_FALSE(test_sink.contains("error"));
        REQUIRE_FALSE(test_sink.contains("fatal"));
    }
}

TEST_CASE("Composite AND filtering", "[sink_filters]") {
    FilterTestSink test_sink;
    
    SECTION("Warn and above, but max error") {
        and_filter filter;
        filter.add(level_filter{log_level::warn})
              .add(max_level_filter{log_level::error});
        
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(debug) << "debug" << endl;
        LOG(info) << "info" << endl;
        LOG(warn) << "warn" << endl;
        LOG(error) << "error" << endl;
        LOG(fatal) << "fatal" << endl;
        
        REQUIRE(test_sink.wait_for_messages(2, std::chrono::milliseconds(500)));
        REQUIRE(test_sink.count() == 2);
        REQUIRE(test_sink.contains("warn"));
        REQUIRE(test_sink.contains("error"));
        REQUIRE_FALSE(test_sink.contains("fatal")); // Filtered by max_level
    }
}

TEST_CASE("Composite OR filtering", "[sink_filters]") {
    FilterTestSink test_sink;
    
    SECTION("Debug OR error and above") {
        or_filter filter;
        filter.add(level_range_filter{log_level::debug, log_level::debug})
              .add(level_filter{log_level::error});
        
        SinkGuard guard(test_sink.get_sink_with_filter(filter));
        
        LOG(trace) << "trace" << endl;
        LOG(debug) << "debug" << endl;
        LOG(info) << "info" << endl;
        LOG(warn) << "warn" << endl;
        LOG(error) << "error" << endl;
        LOG(fatal) << "fatal" << endl;
        
        REQUIRE(test_sink.wait_for_messages(3, std::chrono::milliseconds(500)));
        REQUIRE(test_sink.count() == 3);
        REQUIRE(test_sink.contains("debug"));
        REQUIRE(test_sink.contains("error"));
        REQUIRE(test_sink.contains("fatal"));
        REQUIRE_FALSE(test_sink.contains("trace"));
        REQUIRE_FALSE(test_sink.contains("info"));
        REQUIRE_FALSE(test_sink.contains("warn"));
    }
}

TEST_CASE("NOT filter", "[sink_filters]") {
    FilterTestSink test_sink;
    
    SECTION("Everything except debug") {
        SinkGuard guard(test_sink.get_sink_with_filter(
            not_filter{level_range_filter{log_level::debug, log_level::debug}}));
        
        LOG(trace) << "trace" << endl;
        LOG(debug) << "debug" << endl;
        LOG(info) << "info" << endl;
        LOG(warn) << "warn" << endl;
        
        REQUIRE(test_sink.wait_for_messages(3, std::chrono::milliseconds(500)));
        REQUIRE(test_sink.count() == 3);
        REQUIRE(test_sink.contains("trace"));
        REQUIRE_FALSE(test_sink.contains("debug"));
        REQUIRE(test_sink.contains("info"));
        REQUIRE(test_sink.contains("warn"));
    }
}

TEST_CASE("Multiple sinks with different filters", "[sink_filters]") {
    FilterTestSink all_sink;
    FilterTestSink warn_sink;
    FilterTestSink error_sink;
    
    // Set up dispatcher with multiple sinks
    auto& dispatcher = log_line_dispatcher::instance();
    auto original = dispatcher.get_sink(0);
    
    dispatcher.set_sink(0, all_sink.get_sink());
    dispatcher.add_sink(warn_sink.get_sink_with_filter(level_filter{log_level::warn}));
    dispatcher.add_sink(error_sink.get_sink_with_filter(level_filter{log_level::error}));
    
    // Generate logs
    LOG(trace) << "trace1" << endl;
    LOG(debug) << "debug1" << endl;
    LOG(info) << "info1" << endl;
    LOG(warn) << "warn1" << endl;
    LOG(error) << "error1" << endl;
    LOG(fatal) << "fatal1" << endl;
    
    dispatcher.flush();
    
    // Verify each sink got the right messages
    REQUIRE(all_sink.count() == 6);
    REQUIRE(warn_sink.count() == 3); // warn, error, fatal
    REQUIRE(error_sink.count() == 2); // error, fatal
    
    REQUIRE(all_sink.contains("trace1"));
    REQUIRE(all_sink.contains("fatal1"));
    
    REQUIRE_FALSE(warn_sink.contains("info1"));
    REQUIRE(warn_sink.contains("warn1"));
    REQUIRE(warn_sink.contains("error1"));
    
    REQUIRE_FALSE(error_sink.contains("warn1"));
    REQUIRE(error_sink.contains("error1"));
    REQUIRE(error_sink.contains("fatal1"));
    
    // Cleanup - remove added sinks
    dispatcher.remove_sink(2);
    dispatcher.remove_sink(1);
    dispatcher.set_sink(0, original);
}

TEST_CASE("Factory functions with filters", "[sink_filters]") {
    // Just test that the factory functions compile with filters
    auto stdout_filtered = make_stdout_sink(level_filter{log_level::warn});
    REQUIRE(stdout_filtered != nullptr);
    
    auto file_filtered = make_raw_file_sink("/tmp/test_filtered.log", {}, level_filter{log_level::error});
    REQUIRE(file_filtered != nullptr);
    
    auto writev_filtered = make_writev_file_sink("/tmp/test_writev_filtered.log", {}, max_level_filter{log_level::debug});
    REQUIRE(writev_filtered != nullptr);
    
    // Test that no-filter still works
    auto unfiltered = make_stdout_sink();
    REQUIRE(unfiltered != nullptr);
}

TEST_CASE("Filter performance with high volume", "[sink_filters][performance]") {
    FilterTestSink filtered_sink;
    FilterTestSink unfiltered_sink;
    
    // Set up two sinks
    auto& dispatcher = log_line_dispatcher::instance();
    auto original = dispatcher.get_sink(0);
    
    dispatcher.set_sink(0, filtered_sink.get_sink_with_filter(level_filter{log_level::error}));
    dispatcher.add_sink(unfiltered_sink.get_sink());
    
    // Generate many messages
    const int count = 1000;
    int error_count = 0;
    
    for (int i = 0; i < count; ++i) {
        LOG(trace) << "trace " << i << endl;
        LOG(debug) << "debug " << i << endl;
        LOG(info) << "info " << i << endl;
        LOG(warn) << "warn " << i << endl;
        if (i % 10 == 0) {
            LOG(error) << "error " << i << endl;
            error_count++;
        }
    }
    
    dispatcher.flush();
    
    // Filtered sink should only have errors
    REQUIRE(filtered_sink.count() == error_count);
    
    // Unfiltered sink should have all messages
    REQUIRE(unfiltered_sink.count() == (count * 4 + error_count));
    
    // Cleanup
    dispatcher.remove_sink(1);
    dispatcher.set_sink(0, original);
}

TEST_CASE("Thread safety of sink filters", "[sink_filters][threading]") {
    FilterTestSink error_sink;
    
    auto& dispatcher = log_line_dispatcher::instance();
    auto original = dispatcher.get_sink(0);
    dispatcher.set_sink(0, error_sink.get_sink_with_filter(level_filter{log_level::error}));
    
    // Launch multiple threads
    std::vector<std::thread> threads;
    const int thread_count = 4;
    const int messages_per_thread = 100;
    const int errors_per_thread = 10;
    
    for (int t = 0; t < thread_count; ++t) {
        threads.emplace_back([t, messages_per_thread, errors_per_thread]() {
            for (int i = 0; i < messages_per_thread; ++i) {
                LOG(debug) << "debug from thread " << t << " msg " << i << endl;
                LOG(info) << "info from thread " << t << " msg " << i << endl;
                if (i % (messages_per_thread / errors_per_thread) == 0) {
                    LOG(error) << "error from thread " << t << " msg " << i << endl;
                }
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    dispatcher.flush();
    
    // Should have only error messages
    REQUIRE(error_sink.count() == thread_count * errors_per_thread);
    
    // Verify all error messages are present
    for (int t = 0; t < thread_count; ++t) {
        for (int i = 0; i < messages_per_thread; i += (messages_per_thread / errors_per_thread)) {
            std::string expected = "error from thread " + std::to_string(t) + " msg " + std::to_string(i);
            REQUIRE(error_sink.contains(expected));
        }
    }
    
    // Cleanup
    dispatcher.set_sink(0, original);
}

TEST_CASE("Edge case: Empty composite filters", "[sink_filters][edge]") {
    FilterTestSink test_sink;
    
    SECTION("Empty AND filter accepts all") {
        and_filter empty_and;
        SinkGuard guard(test_sink.get_sink_with_filter(empty_and));
        
        LOG(trace) << "trace" << endl;
        LOG(debug) << "debug" << endl;
        LOG(info) << "info" << endl;
        LOG(warn) << "warn" << endl;
        LOG(error) << "error" << endl;
        
        REQUIRE(test_sink.wait_for_messages(5));
        REQUIRE(test_sink.count() == 5);  // All messages should pass
        REQUIRE(test_sink.contains("trace"));
        REQUIRE(test_sink.contains("debug"));
        REQUIRE(test_sink.contains("info"));
        REQUIRE(test_sink.contains("warn"));
        REQUIRE(test_sink.contains("error"));
    }
    
    SECTION("Empty OR filter rejects all") {
        or_filter empty_or;
        SinkGuard guard(test_sink.get_sink_with_filter(empty_or));
        
        LOG(debug) << "debug" << endl;
        LOG(info) << "info" << endl;
        LOG(warn) << "warn" << endl;
        LOG(error) << "error" << endl;
        
        // Wait briefly to see if any messages arrive
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        log_line_dispatcher::instance().flush();
        
        REQUIRE(test_sink.count() == 0);  // No messages should pass
    }
}

TEST_CASE("Filter replacement at runtime", "[sink_filters][runtime]") {
    FilterTestSink test_sink1;
    FilterTestSink test_sink2;
    
    auto& dispatcher = log_line_dispatcher::instance();
    auto original = dispatcher.get_sink(0);
    
    SECTION("Replace filter by replacing sink") {
        // Start with warning filter
        dispatcher.set_sink(0, test_sink1.get_sink_with_filter(level_filter{log_level::warn}));
        
        LOG(debug) << "debug1" << endl;
        LOG(info) << "info1" << endl;
        LOG(warn) << "warn1" << endl;
        LOG(error) << "error1" << endl;
        
        dispatcher.flush();
        REQUIRE(test_sink1.count() == 2);  // warn and error
        REQUIRE(test_sink1.contains("warn1"));
        REQUIRE(test_sink1.contains("error1"));
        
        // Replace with error-only filter
        dispatcher.set_sink(0, test_sink2.get_sink_with_filter(level_filter{log_level::error}));
        
        LOG(debug) << "debug2" << endl;
        LOG(info) << "info2" << endl;
        LOG(warn) << "warn2" << endl;
        LOG(error) << "error2" << endl;
        LOG(fatal) << "fatal2" << endl;
        
        dispatcher.flush();
        REQUIRE(test_sink2.count() == 2);  // error and fatal only
        REQUIRE(test_sink2.contains("error2"));
        REQUIRE(test_sink2.contains("fatal2"));
        REQUIRE_FALSE(test_sink2.contains("warn2"));
        
        // Verify original sink didn't get new messages
        REQUIRE(test_sink1.count() == 2);  // Still just the original 2
    }
    
    // Cleanup
    dispatcher.set_sink(0, original);
}

TEST_CASE("Deeply nested composite filters", "[sink_filters][performance]") {
    FilterTestSink test_sink;
    
    SECTION("Multiple levels of nesting") {
        // Create a complex nested filter:
        // Accept WARN or ERROR, but only if they're also in the INFO-ERROR range
        and_filter outer;
        
        // First condition: must be warn or error
        or_filter severity_filter;
        severity_filter.add(level_filter{log_level::warn})
                       .add(level_filter{log_level::error});
        outer.add(severity_filter);
        
        // Second condition: must be in range info to error
        outer.add(level_range_filter{log_level::info, log_level::error});
        
        // Third condition: add another nested OR just to test nesting
        or_filter nested;
        nested.add(level_range_filter{log_level::trace, log_level::fatal});  // Accept all
        outer.add(nested);
        
        SinkGuard guard(test_sink.get_sink_with_filter(outer));
        
        // Test various log levels
        LOG(trace) << "trace" << endl;  // Fails first condition (not warn/error)
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