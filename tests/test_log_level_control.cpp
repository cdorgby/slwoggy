#include <catch2/catch_test_macros.hpp>
#include "log.hpp"
#include <string>
#include <vector>
#include <algorithm>

using namespace slwoggy;

TEST_CASE("Module level control", "[level_control]") {
    auto& registry = log_module_registry::instance();
    
    SECTION("Basic module level control") {
        // Set and get module level
        registry.set_module_level("test_module", log_level::debug);
        REQUIRE(registry.get_module_level("test_module") == log_level::debug);
        
        // Change level
        registry.set_module_level("test_module", log_level::error);
        REQUIRE(registry.get_module_level("test_module") == log_level::error);
        
        // Non-existent module returns GLOBAL_MIN_LOG_LEVEL
        REQUIRE(registry.get_module_level("does_not_exist") == GLOBAL_MIN_LOG_LEVEL);
    }
    
    SECTION("Generic module") {
        // Generic module always exists
        auto original_level = registry.get_module_level("generic");
        
        registry.set_module_level("generic", log_level::warn);
        REQUIRE(registry.get_module_level("generic") == log_level::warn);
        
        // Restore original level
        registry.set_module_level("generic", original_level);
    }
    
    SECTION("Multiple modules") {
        registry.set_module_level("network", log_level::debug);
        registry.set_module_level("database", log_level::warn);
        registry.set_module_level("ui", log_level::info);
        
        REQUIRE(registry.get_module_level("network") == log_level::debug);
        REQUIRE(registry.get_module_level("database") == log_level::warn);
        REQUIRE(registry.get_module_level("ui") == log_level::info);
    }
    
    SECTION("Set all modules level") {
        // Create some modules
        registry.set_module_level("mod1", log_level::debug);
        registry.set_module_level("mod2", log_level::info);
        registry.set_module_level("mod3", log_level::error);
        
        // Set all to warn
        registry.set_all_modules_level(log_level::warn);
        
        REQUIRE(registry.get_module_level("mod1") == log_level::warn);
        REQUIRE(registry.get_module_level("mod2") == log_level::warn);
        REQUIRE(registry.get_module_level("mod3") == log_level::warn);
        REQUIRE(registry.get_module_level("generic") == log_level::warn);
        
        // New modules created after set_all should get the new default
        registry.set_module_level("mod4", log_level::fatal);
        REQUIRE(registry.get_module_level("mod4") == log_level::fatal);
    }
    
    SECTION("Reset all modules level") {
        // Set various levels
        registry.set_module_level("reset1", log_level::error);
        registry.set_module_level("reset2", log_level::fatal);
        
        // Reset all
        registry.reset_all_modules_level();
        
        REQUIRE(registry.get_module_level("reset1") == GLOBAL_MIN_LOG_LEVEL);
        REQUIRE(registry.get_module_level("reset2") == GLOBAL_MIN_LOG_LEVEL);
        REQUIRE(registry.get_module_level("generic") == GLOBAL_MIN_LOG_LEVEL);
    }
}

TEST_CASE("Configure from string", "[level_control]") {
    auto& registry = log_module_registry::instance();
    
    SECTION("Global level") {
        REQUIRE(registry.configure_from_string("info"));
        REQUIRE(registry.get_module_level("generic") == log_level::info);
        
        REQUIRE(registry.configure_from_string("debug"));
        REQUIRE(registry.get_module_level("generic") == log_level::debug);
        
        REQUIRE(registry.configure_from_string("error"));
        REQUIRE(registry.get_module_level("generic") == log_level::error);
    }
    
    SECTION("Module-specific levels") {
        REQUIRE(registry.configure_from_string("network=debug,database=warn"));
        REQUIRE(registry.get_module_level("network") == log_level::debug);
        REQUIRE(registry.get_module_level("database") == log_level::warn);
        
        // With spaces (should be stripped)
        REQUIRE(registry.configure_from_string("ui = info , audio = error"));
        REQUIRE(registry.get_module_level("ui") == log_level::info);
        REQUIRE(registry.get_module_level("audio") == log_level::error);
    }
    
    SECTION("Mixed global and module-specific") {
        REQUIRE(registry.configure_from_string("warn,special=debug"));
        REQUIRE(registry.get_module_level("generic") == log_level::warn);
        REQUIRE(registry.get_module_level("special") == log_level::debug);
    }
    
    SECTION("Wildcard patterns") {
        // Create some modules first
        registry.set_module_level("net_client", log_level::info);
        registry.set_module_level("net_server", log_level::info);
        registry.set_module_level("database", log_level::info);
        
        // Set all to error
        REQUIRE(registry.configure_from_string("*=error"));
        REQUIRE(registry.get_module_level("net_client") == log_level::error);
        REQUIRE(registry.get_module_level("net_server") == log_level::error);
        REQUIRE(registry.get_module_level("database") == log_level::error);
        
        // Pattern matching with trailing *
        REQUIRE(registry.configure_from_string("net*=debug"));
        REQUIRE(registry.get_module_level("net_client") == log_level::debug);
        REQUIRE(registry.get_module_level("net_server") == log_level::debug);
        REQUIRE(registry.get_module_level("database") == log_level::error); // unchanged
        
        // Pattern matching with leading *
        registry.set_module_level("client_net", log_level::info);
        registry.set_module_level("client_db", log_level::info);
        REQUIRE(registry.configure_from_string("*_net=warn"));
        REQUIRE(registry.get_module_level("client_net") == log_level::warn);
        REQUIRE(registry.get_module_level("net_client") == log_level::debug); // unchanged
    }
    
    SECTION("Invalid configurations") {
        // Invalid level names
        REQUIRE(registry.configure_from_string("invalid_level") == false);
        REQUIRE(registry.configure_from_string("network=invalid") == false);
        
        // Empty module name
        REQUIRE(registry.configure_from_string("=debug") == false);
        
        // Empty level
        REQUIRE(registry.configure_from_string("network=") == false);
        
        // But nolog/off are valid
        REQUIRE(registry.configure_from_string("network=nolog"));
        REQUIRE(registry.get_module_level("network") == log_level::nolog);
        
        REQUIRE(registry.configure_from_string("database=off"));
        REQUIRE(registry.get_module_level("database") == log_level::nolog);
    }
    
    SECTION("Case sensitivity") {
        // Level names are case insensitive
        REQUIRE(registry.configure_from_string("network=DEBUG"));
        REQUIRE(registry.get_module_level("network") == log_level::debug);
        
        REQUIRE(registry.configure_from_string("network=Info"));
        REQUIRE(registry.get_module_level("network") == log_level::info);
        
        // Module names are case sensitive
        registry.set_module_level("CaseSensitive", log_level::warn);
        REQUIRE(registry.get_module_level("CaseSensitive") == log_level::warn);
        REQUIRE(registry.get_module_level("casesensitive") == GLOBAL_MIN_LOG_LEVEL);
    }
}

TEST_CASE("Get all modules", "[level_control]") {
    auto& registry = log_module_registry::instance();
    
    SECTION("List registered modules") {
        // Clear any existing modules by resetting
        registry.reset_all_modules_level();
        
        // Create some modules
        registry.set_module_level("alpha", log_level::debug);
        registry.set_module_level("beta", log_level::info);
        registry.set_module_level("gamma", log_level::warn);
        
        auto modules = registry.get_all_modules();
        
        // Should have at least our modules plus generic
        REQUIRE(modules.size() >= 4);
        
        // Check our modules exist
        std::vector<std::string> module_names;
        for (auto* mod : modules) {
            module_names.push_back(mod->name);
        }
        
        REQUIRE(std::find(module_names.begin(), module_names.end(), "generic") != module_names.end());
        REQUIRE(std::find(module_names.begin(), module_names.end(), "alpha") != module_names.end());
        REQUIRE(std::find(module_names.begin(), module_names.end(), "beta") != module_names.end());
        REQUIRE(std::find(module_names.begin(), module_names.end(), "gamma") != module_names.end());
        
        // Verify levels
        for (auto* mod : modules) {
            if (std::string(mod->name) == "alpha") {
                REQUIRE(mod->level.load() == log_level::debug);
            } else if (std::string(mod->name) == "beta") {
                REQUIRE(mod->level.load() == log_level::info);
            } else if (std::string(mod->name) == "gamma") {
                REQUIRE(mod->level.load() == log_level::warn);
            }
        }
    }
}

TEST_CASE("Module creation on demand", "[level_control]") {
    auto& registry = log_module_registry::instance();
    
    SECTION("Modules created when referenced") {
        // Module doesn't exist yet
        REQUIRE(registry.get_module_level("new_module") == GLOBAL_MIN_LOG_LEVEL);
        
        // Setting level creates it
        registry.set_module_level("new_module", log_level::error);
        REQUIRE(registry.get_module_level("new_module") == log_level::error);
        
        // Now it exists in the list
        auto modules = registry.get_all_modules();
        bool found = false;
        for (auto* mod : modules) {
            if (std::string(mod->name) == "new_module") {
                found = true;
                REQUIRE(mod->level.load() == log_level::error);
                break;
            }
        }
        REQUIRE(found);
    }
}