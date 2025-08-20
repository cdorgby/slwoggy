/**
 * @file sink_filter_demo.cpp
 * @brief Demo of per-sink filtering feature
 */

#include "log.hpp"
#include "log_sinks.hpp"
#include "log_sink_filters.hpp"
#include <iostream>

using namespace slwoggy;

int main() {
    // Clear any default sinks
    auto& dispatcher = log_line_dispatcher::instance();
    
    // Add console sink that shows warnings and above
    auto console_sink = make_stdout_sink(level_filter{log_level::warn});
    dispatcher.set_sink(0, console_sink);
    
    // Add file sink that captures everything
    auto file_sink = make_raw_file_sink("/tmp/all_logs.log");
    dispatcher.add_sink(file_sink);
    
    // Add error file sink that only captures errors
    auto error_sink = make_raw_file_sink("/tmp/errors.log", {}, level_filter{log_level::error});
    dispatcher.add_sink(error_sink);
    
    // Generate some test logs
    LOG(trace) << "This is a trace message - only in all_logs.log" << endl;
    LOG(debug) << "This is a debug message - only in all_logs.log" << endl;
    LOG(info) << "This is an info message - only in all_logs.log" << endl;
    LOG(warn) << "This is a warning - in console and all_logs.log" << endl;
    LOG(error) << "This is an error - in all three outputs" << endl;
    LOG(fatal) << "This is fatal - in all three outputs" << endl;
    
    // Demonstrate composite filters
    std::cout << "\n--- Testing composite filters ---\n" << std::endl;
    
    // Create a sink that only logs warnings and errors (not fatal)
    and_filter warn_error_filter;
    warn_error_filter.add(level_filter{log_level::warn})
                     .add(max_level_filter{log_level::error});
    
    auto limited_sink = make_stdout_sink(warn_error_filter);
    dispatcher.set_sink(0, limited_sink);
    
    LOG(info) << "Info - not shown" << endl;
    LOG(warn) << "Warning - shown" << endl;
    LOG(error) << "Error - shown" << endl;
    LOG(fatal) << "Fatal - not shown (filtered by max_level)" << endl;
    
    // Flush to ensure all logs are written
    dispatcher.flush();
    
    std::cout << "\n--- Demo complete ---" << std::endl;
    std::cout << "Check /tmp/all_logs.log for all messages" << std::endl;
    std::cout << "Check /tmp/errors.log for errors only" << std::endl;
    
    return 0;
}