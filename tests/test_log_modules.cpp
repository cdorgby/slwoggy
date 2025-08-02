#include <catch2/catch_test_macros.hpp>
#include "log.hpp"
#include <thread>
#include <vector>

using namespace slwoggy;

// Test sink that captures module names
class module_test_sink final : public log_sink {
public:
    struct LogEntry {
        std::string module_name;
        log_level level;
        std::string message;
    };
    
private:
    mutable std::mutex mutex_;
    mutable std::vector<LogEntry> entries_;
    
public:
    void log(log_buffer* buffer) const override {
        if (!buffer) return;
        
        // We don't have module name in buffer yet, so just capture what we can
        // In real implementation, you'd need to add module name to log_buffer
        LogEntry entry;
        entry.level = buffer->level_;
        
        // Extract message from buffer
        std::string full_log(buffer->get_text());
        
        // Find module name in the formatted output (it's after level)
        // Format is: "TTTTTTTT.mmm [LEVEL] module     file:line message"
        size_t module_start = full_log.find(']');
        if (module_start != std::string::npos) {
            module_start += 2; // Skip "] "
            size_t module_end = module_start;
            while (module_end < full_log.size() && full_log[module_end] != ' ') {
                module_end++;
            }
            entry.module_name = full_log.substr(module_start, module_end - module_start);
            
            // Find message after file:line
            size_t msg_start = full_log.find(' ', full_log.find(':', module_end));
            if (msg_start != std::string::npos) {
                entry.message = full_log.substr(msg_start + 1);
            }
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(std::move(entry));
    }
    
    std::vector<LogEntry> get_entries() const {
        log_line_dispatcher::instance().flush();
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }
};

TEST_CASE("Module registry functionality", "[log][modules]") {
    SECTION("Default module is generic") {
        module_test_sink test_sink;
        auto& dispatcher = log_line_dispatcher::instance();
        auto* original_sink = dispatcher.get_sink(0);
        dispatcher.set_sink(0, &test_sink);
        
        LOG(info) << "Test message";
        
        auto entries = test_sink.get_entries();
        REQUIRE(entries.size() == 1);
        REQUIRE(entries[0].module_name == "generic");
        
        dispatcher.set_sink(0, original_sink);
    }
    
    SECTION("Module registry creates and shares modules") {
        auto& registry = log_module_registry::instance();
        
        // Get same module twice
        auto* mod1 = registry.get_module("test_module");
        auto* mod2 = registry.get_module("test_module");
        
        REQUIRE(mod1 == mod2);
        REQUIRE(std::string(mod1->name) == "test_module");
        
        // Generic module is always the same
        auto* generic1 = registry.get_module("generic");
        auto* generic2 = registry.get_generic();
        REQUIRE(generic1 == generic2);
    }
    
    SECTION("Module level filtering") {
        module_test_sink test_sink;
        auto& dispatcher = log_line_dispatcher::instance();
        auto* original_sink = dispatcher.get_sink(0);
        dispatcher.set_sink(0, &test_sink);
        
        // Get a test module and set its level to warn
        auto* test_mod = log_module_registry::instance().get_module("test_filter");
        test_mod->level = log_level::warn;
        
        // This would need to be in a separate compilation unit to test properly
        // For now, we can at least verify the module exists
        REQUIRE(test_mod->level.load() == log_level::warn);
        
        dispatcher.set_sink(0, original_sink);
    }
    
    SECTION("Thread safety of module registry") {
        const int num_threads = 10;
        const int modules_per_thread = 100;
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([t, modules_per_thread]() {
                for (int m = 0; m < modules_per_thread; ++m) {
                    std::string module_name = "thread_" + std::to_string(t) + "_mod_" + std::to_string(m);
                    auto* mod = log_module_registry::instance().get_module(module_name.c_str());
                    REQUIRE(mod != nullptr);
                    REQUIRE(std::string(mod->name) == module_name);
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // Verify all modules were created
        auto all_modules = log_module_registry::instance().get_all_modules();
        // Should have at least generic + all thread modules
        REQUIRE(all_modules.size() >= 1 + num_threads * modules_per_thread);
    }
}

// Separate compilation unit simulation
namespace test_module_a {
    LOG_MODULE_NAME("module_a")
    LOG_MODULE_LEVEL(log_level::debug)
    
    void test_function() {
        LOG(trace) << "Should not appear (below module level)";
        LOG(debug) << "Debug message from module_a";
        LOG(info) << "Info message from module_a";
    }
}

namespace test_module_b {
    LOG_MODULE_NAME("module_b")
    LOG_MODULE_LEVEL(log_level::info)
    
    void test_function() {
        LOG(debug) << "Should not appear (below module level)";
        LOG(info) << "Info message from module_b";
        LOG(warn) << "Warning from module_b";
    }
}

TEST_CASE("Module configuration macros", "[log][modules]") {
    SECTION("Module-specific logging") {
        module_test_sink test_sink;
        auto& dispatcher = log_line_dispatcher::instance();
        auto* original_sink = dispatcher.get_sink(0);
        dispatcher.set_sink(0, &test_sink);
        
        test_module_a::test_function();
        test_module_b::test_function();
        
        auto entries = test_sink.get_entries();
        
        // Count messages from each module
        int module_a_count = 0;
        int module_b_count = 0;
        
        for (const auto& entry : entries) {
            if (entry.module_name == "module_a") module_a_count++;
            else if (entry.module_name == "module_b") module_b_count++;
        }
        
        // module_a should have 2 messages (debug and info, not trace)
        REQUIRE(module_a_count == 2);
        
        // module_b should have 2 messages (info and warn, not debug)
        REQUIRE(module_b_count == 2);
        
        dispatcher.set_sink(0, original_sink);
    }
}