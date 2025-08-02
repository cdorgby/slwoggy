#include <catch2/catch_test_macros.hpp>
#include "log.hpp"
#include <vector>
#include <cstring>

using namespace slwoggy;

// Helper to count sites from current test file
static size_t count_test_file_sites() {
    auto& all_sites = log_site_registry::sites();
    size_t count = 0;
    for (const auto& site : all_sites) {
        if (std::strstr(site.file, "test_log_site_control.cpp")) {
            count++;
        }
    }
    return count;
}

// Helper to get all levels for sites in current test file
static std::vector<log_level> get_test_file_levels() {
    auto& all_sites = log_site_registry::sites();
    std::vector<log_level> levels;
    for (const auto& site : all_sites) {
        if (std::strstr(site.file, "test_log_site_control.cpp")) {
            levels.push_back(site.min_level);
        }
    }
    return levels;
}

TEST_CASE("Site registration and finding", "[site_control]") {
    // Clear any existing sites from previous tests
    log_site_registry::clear();
    
    SECTION("Sites are registered by LOG() calls") {
        // These LOG calls will register sites at specific line numbers
        const int line1 = __LINE__ + 1;
        LOG(info) << "Test message 1" << endl;
        const int line2 = __LINE__ + 1;
        LOG(debug) << "Test message 2" << endl;
        const int line3 = __LINE__ + 1;
        LOG(warn) << "Test message 3" << endl;
        
        // Find the sites we just registered
        auto* site1 = log_site_registry::find_site(SOURCE_FILE_NAME, line1);
        auto* site2 = log_site_registry::find_site(SOURCE_FILE_NAME, line2);
        auto* site3 = log_site_registry::find_site(SOURCE_FILE_NAME, line3);
        
        REQUIRE(site1 != nullptr);
        REQUIRE(site2 != nullptr);
        REQUIRE(site3 != nullptr);
        
        REQUIRE(site1->line == line1);
        REQUIRE(site1->min_level == log_level::info);
        
        REQUIRE(site2->line == line2);
        REQUIRE(site2->min_level == log_level::debug);
        
        REQUIRE(site3->line == line3);
        REQUIRE(site3->min_level == log_level::warn);
        
        // Non-existent site should return nullptr
        auto* not_found = log_site_registry::find_site(SOURCE_FILE_NAME, 999);
        REQUIRE(not_found == nullptr);
    }
    
    SECTION("Sites are accessible via sites()") {
        log_site_registry::clear();
        
        const int error_line = __LINE__ + 1;
        LOG(error) << "Error site" << endl;
        const int fatal_line = __LINE__ + 1;
        LOG(fatal) << "Fatal site" << endl;
        
        auto& all_sites = log_site_registry::sites();
        
        // Find our sites in the registry
        bool found_error = false;
        bool found_fatal = false;
        
        for (const auto& site : all_sites) {
            if (std::strstr(site.file, "test_log_site_control.cpp")) {
                if (site.line == error_line) {
                    found_error = true;
                    REQUIRE(site.min_level == log_level::error);
                } else if (site.line == fatal_line) {
                    found_fatal = true;
                    REQUIRE(site.min_level == log_level::fatal);
                }
            }
        }
        
        REQUIRE(found_error);
        REQUIRE(found_fatal);
    }
}

TEST_CASE("Site level control", "[site_control]") {
    log_site_registry::clear();
    
    SECTION("Set and get site level") {
        const int test_line = __LINE__ + 1;
        LOG(info) << "Test site" << endl;
        
        // Get initial level
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, test_line) == log_level::info);
        
        // Change the level
        bool result = log_site_registry::set_site_level(SOURCE_FILE_NAME, test_line, log_level::error);
        REQUIRE(result == true);
        
        // Verify it changed
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, test_line) == log_level::error);
        
        // Verify in sites() too
        auto* site = log_site_registry::find_site(SOURCE_FILE_NAME, test_line);
        REQUIRE(site != nullptr);
        REQUIRE(site->min_level == log_level::error);
        
        // Try to set level for non-existent site
        result = log_site_registry::set_site_level(SOURCE_FILE_NAME, 999, log_level::debug);
        REQUIRE(result == false);
        
        // Get level for non-existent site returns nolog
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, 999) == log_level::nolog);
    }
    
    SECTION("Multiple site updates") {
        const int line1 = __LINE__ + 1;
        LOG(debug) << "Site 1" << endl;
        const int line2 = __LINE__ + 1;
        LOG(info) << "Site 2" << endl;
        const int line3 = __LINE__ + 1;
        LOG(warn) << "Site 3" << endl;
        
        // Set different levels
        log_site_registry::set_site_level(SOURCE_FILE_NAME, line1, log_level::fatal);
        log_site_registry::set_site_level(SOURCE_FILE_NAME, line2, log_level::trace);
        log_site_registry::set_site_level(SOURCE_FILE_NAME, line3, log_level::error);
        
        // Verify each
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, line1) == log_level::fatal);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, line2) == log_level::trace);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, line3) == log_level::error);
    }
}

TEST_CASE("File level control", "[site_control]") {
    log_site_registry::clear();
    
    SECTION("Set file level with exact match") {
        // Create some sites
        LOG(debug) << "Debug msg" << endl;
        LOG(info) << "Info msg" << endl;
        LOG(warn) << "Warn msg" << endl;
        LOG(error) << "Error msg" << endl;
        
        size_t initial_count = count_test_file_sites();
        REQUIRE(initial_count >= 4);
        
        // Set all sites in this file to fatal
        size_t updated = log_site_registry::set_file_level(SOURCE_FILE_NAME, log_level::fatal);
        REQUIRE(updated == initial_count);
        
        // Verify all sites in this file are now fatal
        auto levels = get_test_file_levels();
        for (auto level : levels) {
            REQUIRE(level == log_level::fatal);
        }
    }
    
    SECTION("Set file level with wildcards") {
        log_site_registry::clear();
        
        // Create some sites
        const int line1 = __LINE__ + 1;
        LOG(trace) << "Trace 1" << endl;
        const int line2 = __LINE__ + 1;
        LOG(debug) << "Debug 1" << endl;
        const int line3 = __LINE__ + 1;
        LOG(info) << "Info 1" << endl;
        
        size_t test_file_count = count_test_file_sites();
        
        // Test prefix wildcard: "tests/test_*" (SOURCE_FILE_NAME includes path)
        size_t updated = log_site_registry::set_file_level("tests/test_*", log_level::warn);
        REQUIRE(updated >= test_file_count);  // Should match our test file
        
        // Verify our sites got updated
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, line1) == log_level::warn);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, line2) == log_level::warn);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, line3) == log_level::warn);
        
        // Test suffix wildcard: "*_control.cpp"
        updated = log_site_registry::set_file_level("*_control.cpp", log_level::error);
        REQUIRE(updated >= test_file_count);
        
        // Verify update
        auto levels = get_test_file_levels();
        for (auto level : levels) {
            REQUIRE(level == log_level::error);
        }
        
        // Test contains wildcard: "*log*"
        updated = log_site_registry::set_file_level("*log*", log_level::trace);
        REQUIRE(updated >= test_file_count);
        
        // Verify update
        levels = get_test_file_levels();
        for (auto level : levels) {
            REQUIRE(level == log_level::trace);
        }
        
        // Test non-matching wildcard
        updated = log_site_registry::set_file_level("other*", log_level::fatal);
        // Shouldn't match our test file, but might match other files
        
        // Our sites should still be trace
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, line1) == log_level::trace);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, line2) == log_level::trace);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, line3) == log_level::trace);
        
        // Test another non-matching pattern
        updated = log_site_registry::set_file_level("*xyz.cpp", log_level::fatal);
        
        // Our sites should still be trace
        levels = get_test_file_levels();
        for (auto level : levels) {
            REQUIRE(level == log_level::trace);
        }
    }
}

TEST_CASE("Set all sites level", "[site_control]") {
    log_site_registry::clear();
    
    SECTION("Update all registered sites") {
        // Create various sites
        const int l1 = __LINE__ + 1;
        LOG(trace) << "msg1" << endl;
        const int l2 = __LINE__ + 1;
        LOG(debug) << "msg2" << endl;
        const int l3 = __LINE__ + 1;
        LOG(info) << "msg3" << endl;
        const int l4 = __LINE__ + 1;
        LOG(warn) << "msg4" << endl;
        const int l5 = __LINE__ + 1;
        LOG(error) << "msg5" << endl;
        const int l6 = __LINE__ + 1;
        LOG(fatal) << "msg6" << endl;
        
        // Set all sites to warn level
        log_site_registry::set_all_sites_level(log_level::warn);
        
        // Verify all our sites are now warn
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, l1) == log_level::warn);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, l2) == log_level::warn);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, l3) == log_level::warn);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, l4) == log_level::warn);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, l5) == log_level::warn);
        REQUIRE(log_site_registry::get_site_level(SOURCE_FILE_NAME, l6) == log_level::warn);
        
        // Also verify via sites()
        auto& all_sites = log_site_registry::sites();
        for (const auto& site : all_sites) {
            REQUIRE(site.min_level == log_level::warn);
        }
    }
}

TEST_CASE("Clear sites", "[site_control]") {
    SECTION("Clear removes all sites") {
        // Add some sites
        LOG(info) << "Before clear 1" << endl;
        LOG(debug) << "Before clear 2" << endl;
        
        // Should have at least 2 sites
        auto& sites_before = log_site_registry::sites();
        REQUIRE(sites_before.size() >= 2);
        
        // Clear all
        log_site_registry::clear();
        
        // Should be empty
        auto& sites_after = log_site_registry::sites();
        REQUIRE(sites_after.empty());
        
        // Finding sites should return nullptr
        REQUIRE(log_site_registry::find_site(SOURCE_FILE_NAME, __LINE__) == nullptr);
        REQUIRE(log_site_registry::find_site(SOURCE_FILE_NAME, __LINE__ - 1) == nullptr);
    }
}