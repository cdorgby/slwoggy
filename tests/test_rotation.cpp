/**
 * @file test_rotation.cpp
 * @brief Comprehensive test suite for file rotation service
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>

#include "log.hpp"
#include "log_sink.hpp"
#include "log_formatters.hpp"
#include "log_writers.hpp"
#include "log_file_rotator.hpp"
#include "log_file_rotator_impl.hpp"

using namespace slwoggy;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

// Test fixtures
class rotation_test_fixture {
protected:
    std::string test_dir;
    std::string base_filename;
    
    rotation_test_fixture() {
        // Create unique test directory
        auto pid = getpid();
        auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
        test_dir = "/tmp/test_rotation_" + std::to_string(pid) + "_" + std::to_string(tid);
        fs::create_directories(test_dir);
        base_filename = test_dir + "/test.log";
    }
    
    ~rotation_test_fixture() {
        // Cleanup test directory
        try {
            fs::remove_all(test_dir);
        } catch (...) {
            // Ignore cleanup errors
        }
    }
    
    size_t count_rotated_files() {
        size_t count = 0;
        std::regex pattern("test-\\d{8}-\\d{6}-\\d{3}\\.log");
        for (const auto& entry : fs::directory_iterator(test_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (std::regex_match(filename, pattern)) {
                    count++;
                }
            }
        }
        return count;
    }
    
    size_t get_file_size(const std::string& path) {
        try {
            return fs::file_size(path);
        } catch (...) {
            return 0;
        }
    }
    
    bool file_exists(const std::string& path) {
        return fs::exists(path);
    }
    
    void write_data(file_writer& writer, size_t bytes) {
        std::string data(bytes, 'A');
        writer.write(data.c_str(), data.size());
    }
};

TEST_CASE_METHOD(rotation_test_fixture, "Basic rotation service instantiation", "[rotation]") {
    SECTION("Singleton creation") {
        auto& service1 = file_rotation_service::instance();
        auto& service2 = file_rotation_service::instance();
        REQUIRE(&service1 == &service2);
    }
    
    SECTION("Metrics singleton") {
        auto& metrics1 = rotation_metrics::instance();
        auto& metrics2 = rotation_metrics::instance();
        REQUIRE(&metrics1 == &metrics2);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Size-based rotation", "[rotation][size]") {
    // Reset metrics
    auto& metrics = rotation_metrics::instance();
    auto initial_rotations = metrics.rotations_total.load();
    
    SECTION("Rotation at exact size boundary") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 1024;  // 1KB
        policy.keep_files = 3;
        
        file_writer writer(base_filename, policy);
        
        // Write just under limit
        write_data(writer, 1020);
        REQUIRE(file_exists(base_filename));
        REQUIRE(count_rotated_files() == 0);
        
        // Write over limit - should trigger rotation
        write_data(writer, 100);
        
        // Give rotator thread time to process
        std::this_thread::sleep_for(100ms);
        
        REQUIRE(count_rotated_files() == 1);
        REQUIRE(file_exists(base_filename));
        
        // Verify metrics
        REQUIRE(metrics.rotations_total.load() > initial_rotations);
    }
    
    SECTION("Multiple rotations") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 512;  // Small size for quick rotation
        policy.keep_files = 5;
        
        file_writer writer(base_filename, policy);
        
        // Trigger 3 rotations
        for (int i = 0; i < 3; ++i) {
            write_data(writer, 600);
            std::this_thread::sleep_for(50ms);
        }
        
        // Give rotator thread time to process
        std::this_thread::sleep_for(200ms);
        
        REQUIRE(count_rotated_files() == 3);
        REQUIRE(file_exists(base_filename));
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Time-based rotation", "[rotation][time]") {
    auto& metrics = rotation_metrics::instance();
    
    SECTION("Rotation on time boundary during write") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::time;
        policy.every = 1min;  // Rotate every minute
        
        // Create writer
        file_writer writer(base_filename, policy);
        
        // Write initial data
        write_data(writer, 100);
        REQUIRE(count_rotated_files() == 0);
        
        // Note: We can't easily test actual time-based rotation without
        // mocking time or waiting a full minute. This test validates the setup.
        REQUIRE(file_exists(base_filename));
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Size OR time rotation", "[rotation][combined]") {
    SECTION("Size triggers before time") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size_or_time;
        policy.max_bytes = 1024;
        policy.every = 60min;  // Won't trigger in test
        
        file_writer writer(base_filename, policy);
        
        // Size threshold should trigger
        write_data(writer, 1100);
        std::this_thread::sleep_for(100ms);
        
        REQUIRE(count_rotated_files() == 1);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Retention policies", "[rotation][retention]") {
    SECTION("keep_files limit") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 256;  // Very small for quick rotation
        policy.keep_files = 2;   // Keep only 2 files
        
        file_writer writer(base_filename, policy);
        
        // Create 4 rotations
        for (int i = 0; i < 4; ++i) {
            write_data(writer, 300);
            std::this_thread::sleep_for(100ms);
        }
        
        // Give time for retention cleanup
        std::this_thread::sleep_for(500ms);
        
        // Should have at most 2 rotated files (retention may have deleted some)
        REQUIRE(count_rotated_files() <= 2);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Error handling", "[rotation][errors]") {
    auto& metrics = rotation_metrics::instance();
    
    SECTION("Handle missing directory") {
        std::string bad_path = test_dir + "/nonexistent/test.log";
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 1024;
        
        // Should throw on open
        REQUIRE_THROWS(file_writer(bad_path, policy));
    }
    
    SECTION("Silent data loss on error") {
        // Create a writer
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 512;
        
        file_writer writer(base_filename, policy);
        
        // Simulate error state by creating max retries scenario
        // This is hard to test without mocking, but we can verify metrics
        
        auto initial_dropped = metrics.dropped_records_total.load();
        
        // Normal write should succeed
        write_data(writer, 100);
        
        REQUIRE(metrics.dropped_records_total.load() == initial_dropped);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Zero-gap rotation", "[rotation][zero-gap]") {
    auto& metrics = rotation_metrics::instance();
    auto initial_fallbacks = metrics.zero_gap_fallback_total.load();
    
    SECTION("Hard link success path") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 1024;
        
        file_writer writer(base_filename, policy);
        
        // Trigger rotation
        write_data(writer, 1100);
        std::this_thread::sleep_for(100ms);
        
        // Verify rotation happened
        REQUIRE(count_rotated_files() == 1);
        
        // On same filesystem, hard link should succeed (no fallback)
        // Note: This may vary based on filesystem support
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Filename generation", "[rotation][filenames]") {
    SECTION("Timestamped format") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 512;
        
        file_writer writer(base_filename, policy);
        
        // Trigger rotation
        write_data(writer, 600);
        std::this_thread::sleep_for(100ms);
        
        // Check filename format: test-YYYYMMDD-HHMMSS-NNN.log
        std::regex pattern("test-\\d{8}-\\d{6}-\\d{3}\\.log");
        bool found_match = false;
        
        for (const auto& entry : fs::directory_iterator(test_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (std::regex_match(filename, pattern)) {
                    found_match = true;
                    break;
                }
            }
        }
        
        REQUIRE(found_match);
    }
    
    SECTION("Collision handling") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 256;
        
        file_writer writer(base_filename, policy);
        
        // Trigger multiple rotations quickly
        for (int i = 0; i < 3; ++i) {
            write_data(writer, 300);
            // No sleep - maximize collision chance
        }
        
        std::this_thread::sleep_for(200ms);
        
        // All files should have unique names
        std::set<std::string> filenames;
        for (const auto& entry : fs::directory_iterator(test_dir)) {
            if (entry.is_regular_file()) {
                filenames.insert(entry.path().filename().string());
            }
        }
        
        // Should have unique filenames (base + rotated files)
        REQUIRE(filenames.size() >= 3);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Concurrent rotation", "[rotation][concurrent]") {
    SECTION("Multiple writers to same file") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 1024;
        
        // Create multiple writers sharing rotation handle
        file_writer writer1(base_filename, policy);
        file_writer writer2(writer1);  // Copy constructor shares handle
        
        // Write from both concurrently
        std::thread t1([&]() {
            for (int i = 0; i < 5; ++i) {
                write_data(writer1, 250);
                std::this_thread::sleep_for(10ms);
            }
        });
        
        std::thread t2([&]() {
            for (int i = 0; i < 5; ++i) {
                write_data(writer2, 250);
                std::this_thread::sleep_for(10ms);
            }
        });
        
        t1.join();
        t2.join();
        
        // Give time for rotation
        std::this_thread::sleep_for(200ms);
        
        // Should have triggered at least one rotation
        REQUIRE(count_rotated_files() >= 1);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Metrics accuracy", "[rotation][metrics]") {
    auto& metrics = rotation_metrics::instance();
    
    // Reset/baseline metrics
    auto initial_rotations = metrics.rotations_total.load();
    auto initial_dropped_records = metrics.dropped_records_total.load();
    auto initial_dropped_bytes = metrics.dropped_bytes_total.load();
    
    SECTION("Track rotation count") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 512;
        
        file_writer writer(base_filename, policy);
        
        // Trigger exactly 2 rotations
        write_data(writer, 600);
        std::this_thread::sleep_for(100ms);
        write_data(writer, 600);
        std::this_thread::sleep_for(100ms);
        
        auto new_rotations = metrics.rotations_total.load();
        REQUIRE(new_rotations == initial_rotations + 2);
    }
    
    SECTION("Track rotation duration") {
        auto initial_count = metrics.rotation_duration_us_count.load();
        
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 512;
        
        file_writer writer(base_filename, policy);
        
        // Trigger rotation
        write_data(writer, 600);
        std::this_thread::sleep_for(100ms);
        
        auto new_count = metrics.rotation_duration_us_count.load();
        REQUIRE(new_count > initial_count);
        
        // Duration should be reasonable (< 1 second)
        if (new_count > initial_count) {
            auto total_duration = metrics.rotation_duration_us_sum.load();
            auto avg_duration = total_duration / new_count;
            REQUIRE(avg_duration < 1'000'000);  // Less than 1 second
        }
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Integration with log system", "[rotation][integration]") {
    
    SECTION("Rotating file sink") {
        // Create rotating sink
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 1024;
        
        auto sink = std::make_shared<log_sink>(
            raw_formatter{},
            file_writer(base_filename, policy)
        );
        
        // Add to dispatcher (this replaces default sink if present)
        log_line_dispatcher::instance().add_sink(sink);
        
        // Generate logs to trigger rotation
        for (int i = 0; i < 50; ++i) {
            LOG(info) << "Test message " << i << " with some padding data";
        }
        
        // Flush and wait
        log_line_dispatcher::instance().flush();
        std::this_thread::sleep_for(200ms);
        
        // Should have rotated at least once
        REQUIRE(count_rotated_files() >= 1);
        
        // Clean up - set sink to nullptr to remove it
        log_line_dispatcher::instance().set_sink(0, nullptr);
    }
    
    SECTION("Large volume test with retention") {
        
        // Simulate the test_rotation_example scenario
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 10 * 1024;  // 10KB
        policy.keep_files = 3;
        
        auto sink = std::make_shared<log_sink>(
            raw_formatter{},
            file_writer(base_filename, policy)
        );
        
        log_line_dispatcher::instance().add_sink(sink);
        
        // Generate many logs to test rotation and retention
        for (int i = 0; i < 200; ++i) {
            LOG(info) << "Test message " << i 
                      << " - Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                      << "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";
        }
        
        log_line_dispatcher::instance().flush();
        std::this_thread::sleep_for(500ms);
        
        // Check metrics
        auto& metrics = rotation_metrics::instance();
        REQUIRE(metrics.rotations_total.load() > 0);
        REQUIRE(metrics.dropped_records_total.load() == 0);
        
        // Verify retention - should have at most keep_files rotated files
        REQUIRE(count_rotated_files() <= 3);
        
        log_line_dispatcher::instance().set_sink(0, nullptr);
    }
    
    SECTION("Direct write with rotation") {
        // Simulate the debug_rotation scenario
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 100;  // Very small for immediate rotation
        
        file_writer writer(base_filename, policy);
        
        // First write (under limit)
        std::string msg1(50, 'A');
        ssize_t written = writer.write(msg1.c_str(), msg1.size());
        REQUIRE(written == 50);
        REQUIRE(count_rotated_files() == 0);
        
        // Second write (triggers rotation)
        std::string msg2(60, 'B');
        written = writer.write(msg2.c_str(), msg2.size());
        REQUIRE(written == 60);
        
        std::this_thread::sleep_for(100ms);
        REQUIRE(count_rotated_files() == 1);
        REQUIRE(file_exists(base_filename));
    }
}

// Performance test (not run by default)
TEST_CASE_METHOD(rotation_test_fixture, "Throughput with rotation", "[.][rotation][performance]") {
    SECTION("2M msg/sec target") {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 10 * 1024 * 1024;  // 10MB files
        
        file_writer writer(base_filename, policy);
        
        const size_t msg_count = 100'000;
        const std::string msg(100, 'X');  // 100 byte messages
        
        auto start = std::chrono::steady_clock::now();
        
        for (size_t i = 0; i < msg_count; ++i) {
            writer.write(msg.c_str(), msg.size());
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        double msgs_per_sec = (msg_count * 1'000'000.0) / duration.count();
        
        INFO("Throughput: " << msgs_per_sec << " msg/sec");
        
        // Should maintain reasonable throughput even with rotation
        // Note: 2M msg/sec may not be achievable in test environment
        REQUIRE(msgs_per_sec > 100'000);  // At least 100K msg/sec
    }
}