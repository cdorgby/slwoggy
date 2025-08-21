/**
 * @file module_filter_demo.cpp
 * @brief Demonstration of module-based sink filtering
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include "log.hpp"
#include "log_sinks.hpp"
#include "log_sink_filters.hpp"
#include <thread>
#include <chrono>
#include <iostream>

using namespace slwoggy;
using namespace std::chrono_literals;

int main()
{
    // Create different sinks with module filters
    
    // 1. Console sink for errors from all modules
    auto console_sink = make_stdout_sink(level_filter{log_level::error});
    
    // 2. Network log file - only network-related modules
    auto network_sink = make_raw_file_sink(
        "network.log",
        rotate_policy{},  // default rotation policy
        module_filter{{"network", "http", "websocket", "tcp"}}
    );
    
    // 3. Database log file - only database module
    auto db_sink = make_raw_file_sink(
        "database.log",
        rotate_policy{},  // default rotation policy 
        module_filter{{"database", "sql", "query"}}
    );
    
    // 4. Application log - everything except verbose debug modules
    auto app_sink = make_raw_file_sink(
        "application.log",
        rotate_policy{},  // default rotation policy
        module_exclude_filter{{"trace", "verbose", "debug_internal"}}
    );
    
    // 5. Critical issues file - errors from critical modules only
    auto critical_sink = make_raw_file_sink(
        "critical.log",
        rotate_policy{},  // default rotation policy
        and_filter{}
            .add(level_filter{log_level::error})
            .add(module_filter{{"auth", "payment", "security"}})
    );
    
    // Add all sinks to dispatcher
    log_line_dispatcher::instance().add_sink(console_sink);
    log_line_dispatcher::instance().add_sink(network_sink);
    log_line_dispatcher::instance().add_sink(db_sink);
    log_line_dispatcher::instance().add_sink(app_sink);
    log_line_dispatcher::instance().add_sink(critical_sink);
    
    // Simulate various module logs
    LOG_MOD(info, "app") << "Application starting..." << endl;
    LOG_MOD(debug, "network") << "Initializing network stack" << endl;
    LOG_MOD(info, "database") << "Connecting to database at localhost:5432" << endl;
    
    // Simulate some operations
    LOG_MOD(debug, "http") << "Starting HTTP server on port 8080" << endl;
    LOG_MOD(info, "websocket") << "WebSocket handler registered at /ws" << endl;
    LOG_MOD(trace, "verbose") << "This won't appear in application.log" << endl;
    
    // Simulate some warnings and errors
    LOG_MOD(warn, "network") << "High latency detected: 250ms" << endl;
    LOG_MOD(error, "database") << "Connection pool exhausted!" << endl;
    LOG_MOD(error, "auth") << "Failed login attempt from 192.168.1.100" << endl;
    LOG_MOD(fatal, "payment") << "Payment gateway timeout - transaction rolled back" << endl;
    
    // More normal operations
    LOG_MOD(info, "app") << "Processing request ID: 12345" << endl;
    LOG_MOD(debug, "sql") << "SELECT * FROM users WHERE id = ?" << endl;
    LOG_MOD(info, "query") << "Query executed in 23ms" << endl;
    
    // Security events
    LOG_MOD(warn, "security") << "Suspicious activity detected from IP 10.0.0.1" << endl;
    LOG_MOD(error, "security") << "Potential SQL injection attempt blocked" << endl;
    
    // Trace messages (filtered out from most sinks)
    LOG_MOD(trace, "trace") << "Entering function processRequest()" << endl;
    LOG_MOD(trace, "debug_internal") << "Memory usage: 45MB" << endl;
    
    // Wait for logs to be processed
    std::this_thread::sleep_for(100ms);
    log_line_dispatcher::instance().flush();
    
    std::cout << "\n=== Module Filter Demo Complete ===\n";
    std::cout << "Check the following log files:\n";
    std::cout << "  - network.log: Contains network, http, websocket, tcp modules\n";
    std::cout << "  - database.log: Contains database, sql, query modules\n";
    std::cout << "  - application.log: Everything except trace, verbose, debug_internal\n";
    std::cout << "  - critical.log: Errors from auth, payment, security modules\n";
    std::cout << "  - Console: All errors and above from any module\n\n";
    
    return 0;
}