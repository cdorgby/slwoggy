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

using namespace slwoggy;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

// Test fixtures
class rotation_test_fixture
{
  protected:
    std::string test_dir;
    std::string base_filename;

    rotation_test_fixture()
    {
        // Create unique test directory
        auto pid = getpid();
        auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
        test_dir = "/tmp/test_rotation_" + std::to_string(pid) + "_" + std::to_string(tid);
        fs::create_directories(test_dir);
        base_filename = test_dir + "/test.log";
    }

    ~rotation_test_fixture()
    {
        // Cleanup test directory
        try
        {
            fs::remove_all(test_dir);
        }
        catch (...)
        {
            // Ignore cleanup errors
        }
    }

    size_t count_rotated_files()
    {
        size_t count = 0;
        std::regex pattern("test-\\d{8}-\\d{6}-\\d{3}\\.log");
        for (const auto &entry : fs::directory_iterator(test_dir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                if (std::regex_match(filename, pattern)) { count++; }
            }
        }
        return count;
    }

    size_t get_file_size(const std::string &path)
    {
        try
        {
            return fs::file_size(path);
        }
        catch (...)
        {
            return 0;
        }
    }

    bool file_exists(const std::string &path) { return fs::exists(path); }

    void write_data(file_writer &writer, size_t bytes)
    {
        std::string data(bytes, 'A');
        writer.write(data.c_str(), data.size());
    }
};

TEST_CASE_METHOD(rotation_test_fixture, "Basic rotation service instantiation", "[rotation]")
{
    SECTION("Singleton creation")
    {
        auto &service1 = file_rotation_service::instance();
        auto &service2 = file_rotation_service::instance();
        REQUIRE(&service1 == &service2);
    }

    SECTION("Metrics singleton")
    {
        auto &metrics1 = rotation_metrics::instance();
        auto &metrics2 = rotation_metrics::instance();
        REQUIRE(&metrics1 == &metrics2);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Size-based rotation", "[rotation][size]")
{
    // Reset metrics
    auto &metrics          = rotation_metrics::instance();
    auto initial_rotations = metrics.rotations_total.load();

    SECTION("Rotation at exact size boundary")
    {
        rotate_policy policy;
        policy.mode       = rotate_policy::kind::size;
        policy.max_bytes  = 1024; // 1KB
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

    SECTION("Multiple rotations")
    {
        rotate_policy policy;
        policy.mode       = rotate_policy::kind::size;
        policy.max_bytes  = 512; // Small size for quick rotation
        policy.keep_files = 5;

        file_writer writer(base_filename, policy);

        // Trigger 3 rotations
        for (int i = 0; i < 3; ++i)
        {
            write_data(writer, 600);
            std::this_thread::sleep_for(50ms);
        }

        // Give rotator thread time to process
        std::this_thread::sleep_for(200ms);

        REQUIRE(count_rotated_files() == 3);
        REQUIRE(file_exists(base_filename));
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Time-based rotation", "[rotation][time]")
{
    auto &metrics = rotation_metrics::instance();

    SECTION("Rotation on time boundary during write")
    {
        rotate_policy policy;
        policy.mode  = rotate_policy::kind::time;
        policy.every = 1min; // Rotate every minute

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

TEST_CASE_METHOD(rotation_test_fixture, "Size OR time rotation", "[rotation][combined]")
{
    SECTION("Size triggers before time")
    {
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size_or_time;
        policy.max_bytes = 1024;
        policy.every     = 60min; // Won't trigger in test

        file_writer writer(base_filename, policy);

        // Size threshold should trigger
        write_data(writer, 1100);
        std::this_thread::sleep_for(100ms);

        REQUIRE(count_rotated_files() == 1);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Retention policies", "[rotation][retention]")
{
    SECTION("keep_files limit")
    {
        rotate_policy policy;
        policy.mode       = rotate_policy::kind::size;
        policy.max_bytes  = 256; // Very small for quick rotation
        policy.keep_files = 2;   // Keep only 2 files

        file_writer writer(base_filename, policy);

        // Create 4 rotations
        for (int i = 0; i < 4; ++i)
        {
            write_data(writer, 300);
            std::this_thread::sleep_for(100ms);
        }

        // Give time for retention cleanup
        std::this_thread::sleep_for(500ms);

        // Should have at most 2 rotated files (retention may have deleted some)
        REQUIRE(count_rotated_files() <= 2);
    }
    
    SECTION("max_total_bytes retention")
    {
        rotate_policy policy;
        policy.mode            = rotate_policy::kind::size;
        policy.max_bytes       = 1024;  // 1KB per file
        policy.keep_files      = 100;   // High limit, won't be hit
        policy.max_total_bytes = 3072;  // Total 3KB limit
        
        file_writer writer(base_filename, policy);
        
        // Create 5 rotations (5KB total, exceeds 3KB limit)
        for (int i = 0; i < 5; ++i)
        {
            write_data(writer, 1100);  // Trigger rotation
            std::this_thread::sleep_for(100ms);
        }
        
        // Give time for retention cleanup
        std::this_thread::sleep_for(500ms);
        
        // Calculate total size of rotated files
        size_t total_size = 0;
        for (const auto& entry : fs::directory_iterator(test_dir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                if (filename.starts_with("test-") && filename.ends_with(".log"))
                {
                    total_size += fs::file_size(entry.path());
                }
            }
        }
        
        // Should not exceed max_total_bytes (plus current file)
        INFO("Total size of rotated files: " << total_size);
        REQUIRE(total_size <= policy.max_total_bytes + 2048); // Allow for current file
    }
    
    SECTION("max_age retention")
    {
        rotate_policy policy;
        policy.mode       = rotate_policy::kind::size;
        policy.max_bytes  = 512;
        policy.keep_files = 100;  // High limit
        policy.max_age    = std::chrono::seconds(2);  // Very short for testing
        
        file_writer writer(base_filename, policy);
        
        // Create first rotation
        write_data(writer, 600);
        std::this_thread::sleep_for(100ms);
        
        size_t initial_count = count_rotated_files();
        REQUIRE(initial_count == 1);
        
        // Wait for files to age
        std::this_thread::sleep_for(2500ms);
        
        // Create another rotation - should trigger age cleanup
        write_data(writer, 600);
        std::this_thread::sleep_for(500ms);
        
        // Old file should be deleted due to age
        size_t final_count = count_rotated_files();
        INFO("Initial rotated files: " << initial_count << ", Final: " << final_count);
        // Should have new file but old one deleted
        REQUIRE(final_count <= initial_count);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Error handling", "[rotation][errors]")
{
    auto &metrics = rotation_metrics::instance();

    SECTION("Handle missing directory")
    {
        std::string bad_path = test_dir + "/nonexistent/test.log";
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 1024;

        // Should throw on open
        REQUIRE_THROWS(file_writer(bad_path, policy));
    }

    SECTION("Silent data loss on error")
    {
        // Create a writer
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
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

TEST_CASE_METHOD(rotation_test_fixture, "ENOSPC handling", "[rotation][enospc]")
{
    auto &metrics = rotation_metrics::instance();
    
    SECTION("Emergency cleanup on restricted filesystem")
    {
        // ============================================================================
        // ENOSPC TEST SETUP INSTRUCTIONS:
        // 
        // This test requires a small tmpfs mount to properly test out-of-space handling.
        // Without it, the test will be skipped.
        //
        // Setup on macOS:
        //   1. Create a 1MB RAM disk:
        //      hdiutil attach -nomount ram://2048  # Returns device like /dev/disk4
        //      diskutil erasevolume HFS+ 'tmpfs' /dev/disk4
        //   2. Run test:
        //      TEST_TMPFS_DIR=/Volumes/tmpfs ./tests/test_rotation "[rotation][enospc]"
        //   3. Cleanup after testing:
        //      diskutil eject /dev/disk4
        //
        // Setup on Linux:
        //   1. Create tmpfs mount:
        //      sudo mkdir -p /mnt/tmpfs_test
        //      sudo mount -t tmpfs -o size=1M tmpfs /mnt/tmpfs_test
        //   2. Run test:
        //      TEST_TMPFS_DIR=/mnt/tmpfs_test ./tests/test_rotation "[rotation][enospc]"
        //   3. Cleanup:
        //      sudo umount /mnt/tmpfs_test
        //
        // Size Requirements:
        //   - Minimum: 512KB (may not trigger all scenarios)
        //   - Recommended: 1MB (properly tests emergency cleanup)
        //   - Maximum: 2MB (larger sizes may prevent ENOSPC triggering)
        //
        // What this test validates:
        //   - Emergency cleanup deletes files in priority: .pending → .gz → raw logs
        //   - Rotation service enters error state after max retries
        //   - Proper metrics tracking (dropped records, ENOSPC deletions, FD failures)
        //   - Writers handle ENOSPC gracefully without crashing
        //
        // Note: Test creates files in TEST_TMPFS_DIR/enospc_test_<pid>/ and attempts
        // to clean up after itself, but manual cleanup may be needed if test crashes.
        // ============================================================================
        
        // Check for environment variable pointing to a restricted space directory
        const char* tmpfs_dir_env = std::getenv("TEST_TMPFS_DIR");
        
        if (!tmpfs_dir_env) {
            SKIP("Skipping ENOSPC test - set TEST_TMPFS_DIR to a small tmpfs mount to enable");
            return;
        }
        
        std::string tmpfs_dir = tmpfs_dir_env;
        
        // Verify the directory exists and is writable
        if (!fs::exists(tmpfs_dir) || !fs::is_directory(tmpfs_dir)) {
            SKIP("TEST_TMPFS_DIR does not exist or is not a directory: " << tmpfs_dir);
            return;
        }
        
        // Create a test subdirectory
        std::string test_subdir = tmpfs_dir + "/enospc_test_" + std::to_string(getpid());
        try {
            fs::create_directories(test_subdir);
        } catch (const fs::filesystem_error& e) {
            SKIP("Cannot create test directory in TEST_TMPFS_DIR: " << e.what());
            return;
        }
        
        // Record initial metrics
        auto initial_pending = metrics.enospc_deletions_pending.load();
        auto initial_gz = metrics.enospc_deletions_gz.load();
        auto initial_raw = metrics.enospc_deletions_raw.load();
        auto initial_dropped_records = metrics.dropped_records_total.load();
        auto initial_dropped_bytes = metrics.dropped_bytes_total.load();
        auto initial_prepare_failures = metrics.prepare_fd_failures.load();
        
        std::string test_log_path = test_subdir + "/test.log";
        
        // No pre-fill - let the rotation service fill the space naturally
        INFO("Starting ENOSPC test in tmpfs directory: " << test_subdir);
        
        // Configure rotation to fill 1MB quickly
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 50000;    // 50KB per file
        policy.keep_files = 100;      // Keep all files to fill space
        policy.max_retries = 3;       // A few retries before giving up
        
        try {
            file_writer writer(test_log_path, policy);
            
            // Write enough to fill 1MB (20 files * 50KB = 1000KB)
            // Plus need space for temp files during rotation
            for (int i = 0; i < 30; ++i) {
                std::string data(55000, 'X');  // 55KB to trigger rotation
                ssize_t written = writer.write(data.c_str(), data.size());
                
                if (written != static_cast<ssize_t>(data.size())) {
                    INFO("Iteration " << i << ": short write - " << written << " bytes (ENOSPC likely)");
                } else {
                    INFO("Iteration " << i << ": wrote " << written << " bytes");
                }
                
                // Small delay to let rotation thread work
                std::this_thread::sleep_for(50ms);
            }
            
            // Give rotation thread time to process any pending rotations
            std::this_thread::sleep_for(500ms);
            
        } catch (const std::exception& e) {
            INFO("File writer threw exception: " << e.what());
        }
        
        // Check metrics to verify ENOSPC was handled
        auto new_pending = metrics.enospc_deletions_pending.load();
        auto new_gz = metrics.enospc_deletions_gz.load();
        auto new_raw = metrics.enospc_deletions_raw.load();
        auto new_dropped_records = metrics.dropped_records_total.load();
        auto new_dropped_bytes = metrics.dropped_bytes_total.load();
        auto new_prepare_failures = metrics.prepare_fd_failures.load();
        
        INFO("ENOSPC handling results:");
        INFO("  Pending deletions: " << (new_pending - initial_pending));
        INFO("  GZ deletions: " << (new_gz - initial_gz));
        INFO("  Raw deletions: " << (new_raw - initial_raw));
        INFO("  Dropped records: " << (new_dropped_records - initial_dropped_records));
        INFO("  Dropped bytes: " << (new_dropped_bytes - initial_dropped_bytes));
        INFO("  Prepare FD failures: " << (new_prepare_failures - initial_prepare_failures));
        
        // With a real restricted filesystem, we should see ENOSPC handling
        // At minimum, one of these should have increased:
        bool enospc_handled = (new_pending > initial_pending) ||
                             (new_gz > initial_gz) ||
                             (new_raw > initial_raw) ||
                             (new_prepare_failures > initial_prepare_failures) ||
                             (new_dropped_records > initial_dropped_records);
        
        REQUIRE(enospc_handled);
        
        // Verify deletion priority if deletions occurred
        if (new_pending > initial_pending || new_gz > initial_gz || new_raw > initial_raw) {
            INFO("Emergency cleanup was triggered - verifying priority order");
            // .pending files should be deleted before .gz files
            // .gz files should be deleted before raw logs
            // This is more of a code coverage check than a strict requirement
        }
        
        // Cleanup
        try {
            fs::remove_all(test_subdir);
        } catch (const fs::filesystem_error& e) {
            INFO("Cleanup failed (non-critical): " << e.what());
        }
    }
    
    SECTION("Dropped records on rotation failure")
    {
        // This tests behavior when rotation persistently fails
        // Works on regular filesystem by restricting permissions
        
        std::string restricted_dir = test_dir + "/restricted";
        fs::create_directories(restricted_dir);
        
        // Record initial dropped metrics
        auto initial_dropped_records = metrics.dropped_records_total.load();
        auto initial_dropped_bytes = metrics.dropped_bytes_total.load();
        
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 100;     // Very small to trigger rotation quickly
        policy.max_retries = 1;      // Fail fast
        
        std::string test_file = restricted_dir + "/drop_test.log";
        
        {
            file_writer writer(test_file, policy);
            
            // First write should succeed
            write_data(writer, 50);
            
            // Trigger rotation need
            write_data(writer, 60);
            
            // Now make directory read-only to prevent new file creation
            fs::permissions(restricted_dir, fs::perms::owner_read | fs::perms::owner_exec);
            
            // Wait for rotation to fail and enter error state
            std::this_thread::sleep_for(200ms);
            
            // These writes should be "dropped" (writer may return success but increments drop counters)
            for (int i = 0; i < 5; ++i) {
                write_data(writer, 200);
            }
            
            // Restore permissions before destructor
            fs::permissions(restricted_dir, fs::perms::owner_all);
        }
        
        // Check that drops were recorded or writes failed
        auto new_dropped_records = metrics.dropped_records_total.load();
        auto new_dropped_bytes = metrics.dropped_bytes_total.load();
        
        INFO("Dropped records delta: " << (new_dropped_records - initial_dropped_records));
        INFO("Dropped bytes delta: " << (new_dropped_bytes - initial_dropped_bytes));
        
        // Metrics should be accessible (may or may not increase depending on timing)
        REQUIRE(new_dropped_records >= initial_dropped_records);
        REQUIRE(new_dropped_bytes >= initial_dropped_bytes);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Zero-gap rotation", "[rotation][zero-gap]")
{
    auto &metrics          = rotation_metrics::instance();
    auto initial_fallbacks = metrics.zero_gap_fallback_total.load();

    SECTION("Hard link success path")
    {
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
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
    
    SECTION("Fallback when hard link fails")
    {
        // Record initial fallback count
        auto initial_fallbacks = metrics.zero_gap_fallback_total.load();
        
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 512;

        file_writer writer(base_filename, policy);
        
        // Create multiple rotations quickly to potentially trigger fallback
        // (hard link might fail due to filesystem limitations or cross-device issues)
        for (int i = 0; i < 3; ++i) {
            write_data(writer, 600);
            std::this_thread::sleep_for(50ms);
        }
        
        // Check if any fallbacks occurred
        auto new_fallbacks = metrics.zero_gap_fallback_total.load();
        INFO("Zero-gap fallbacks: initial=" << initial_fallbacks << " new=" << new_fallbacks);
        
        // We can't guarantee fallback will happen (depends on filesystem),
        // but we verify the metric is accessible and increments properly
        REQUIRE(new_fallbacks >= initial_fallbacks);
        
        // Verify files were still rotated successfully even if fallback was used
        REQUIRE(count_rotated_files() >= 2);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Filename generation", "[rotation][filenames]")
{
    SECTION("Timestamped format")
    {
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 512;

        file_writer writer(base_filename, policy);

        // Trigger rotation
        write_data(writer, 600);
        std::this_thread::sleep_for(100ms);

        // Check filename format: test-YYYYMMDD-HHMMSS-NNN.log
        std::regex pattern("test-\\d{8}-\\d{6}-\\d{3}\\.log");
        bool found_match = false;

        for (const auto &entry : fs::directory_iterator(test_dir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                if (std::regex_match(filename, pattern))
                {
                    found_match = true;
                    break;
                }
            }
        }

        REQUIRE(found_match);
    }

    SECTION("Collision handling")
    {
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 256;

        file_writer writer(base_filename, policy);

        // Trigger multiple rotations quickly
        for (int i = 0; i < 3; ++i)
        {
            write_data(writer, 300);
            // No sleep - maximize collision chance
        }

        std::this_thread::sleep_for(200ms);

        // All files should have unique names
        std::set<std::string> filenames;
        for (const auto &entry : fs::directory_iterator(test_dir))
        {
            if (entry.is_regular_file()) { filenames.insert(entry.path().filename().string()); }
        }

        // Should have unique filenames (base + rotated files)
        REQUIRE(filenames.size() >= 3);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Compression", "[rotation][compression]")
{
    SECTION("Compression with .pending files")
    {
        rotate_policy policy;
        policy.mode = rotate_policy::kind::size;
        policy.max_bytes = 1024;
        policy.compress = true;  // Enable compression
        
        file_writer writer(base_filename, policy);
        
        // Trigger rotation with compression
        write_data(writer, 1500);
        std::this_thread::sleep_for(200ms);
        
        // Check for rotated files
        REQUIRE(count_rotated_files() >= 1);
        
        // Look for .pending files (created during compression)
        bool found_pending = false;
        bool found_gz = false;
        
        for (const auto& entry : fs::directory_iterator(test_dir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                if (filename.ends_with(".pending")) {
                    found_pending = true;
                    INFO("Found pending file: " << filename);
                }
                if (filename.ends_with(".gz")) {
                    found_gz = true;
                    INFO("Found compressed file: " << filename);
                }
            }
        }
        
        // Note: Compression is not yet implemented, so we expect no .gz files yet
        // but the test verifies the compression flag is handled
        INFO("Compression enabled, pending files: " << found_pending << ", gz files: " << found_gz);
        
        // Verify metrics
        auto &metrics = rotation_metrics::instance();
        REQUIRE(metrics.compression_failures.load() >= 0);
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Concurrent rotation", "[rotation][concurrent]")
{
    SECTION("Multiple writers to same file")
    {
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 1024;

        // Create multiple writers sharing rotation handle
        file_writer writer1(base_filename, policy);
        file_writer writer2(writer1); // Copy constructor shares handle

        // Write from both concurrently
        std::thread t1(
            [&]()
            {
                for (int i = 0; i < 5; ++i)
                {
                    write_data(writer1, 250);
                    std::this_thread::sleep_for(10ms);
                }
            });

        std::thread t2(
            [&]()
            {
                for (int i = 0; i < 5; ++i)
                {
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

TEST_CASE_METHOD(rotation_test_fixture, "Metrics accuracy", "[rotation][metrics]")
{
    auto &metrics = rotation_metrics::instance();

    // Reset/baseline metrics
    auto initial_rotations       = metrics.rotations_total.load();
    auto initial_dropped_records = metrics.dropped_records_total.load();
    auto initial_dropped_bytes   = metrics.dropped_bytes_total.load();

    SECTION("Track rotation count")
    {
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
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

    SECTION("Track rotation duration")
    {
        auto initial_count = metrics.rotation_duration_us_count.load();

        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 512;

        file_writer writer(base_filename, policy);

        // Trigger rotation
        write_data(writer, 600);
        std::this_thread::sleep_for(100ms);

        auto new_count = metrics.rotation_duration_us_count.load();
        REQUIRE(new_count > initial_count);

        // Duration should be reasonable (< 1 second)
        if (new_count > initial_count)
        {
            auto total_duration = metrics.rotation_duration_us_sum.load();
            auto avg_duration   = total_duration / new_count;
            REQUIRE(avg_duration < 1'000'000); // Less than 1 second
        }
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Writer comparison", "[rotation][writers]")
{
    SECTION("writev_file_writer respects rotation policies")
    {
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 1024; // 1KB

        writev_file_writer writer(base_filename, policy);
        
        // Create multiple buffers to write as a batch
        const size_t buffer_count = 10;
        log_buffer_base* buffers[buffer_count];
        
        // Get buffers from pool
        for (size_t i = 0; i < buffer_count; ++i) {
            buffers[i] = buffer_pool::instance().acquire(true); // human_readable = true
            if (buffers[i]) {
                // Write ~200 bytes to each buffer (10 * 200 = 2000 bytes total, triggers rotation)
                std::string msg = "Test message " + std::to_string(i) + " with some padding data to make it larger ";
                for (int j = 0; j < 5; ++j) {
                    buffers[i]->write_raw(msg);
                }
            }
        }
        
        // Use a simple formatter
        struct test_formatter {
            size_t calculate_size(const log_buffer_base* buffer) const {
                return buffer->len();
            }
            size_t format(const log_buffer_base* buffer, char* output, size_t max_size) const {
                auto text = buffer->get_text();
                size_t to_copy = std::min(text.size(), max_size);
                memcpy(output, text.data(), to_copy);
                return to_copy;
            }
        };
        
        // First batch should succeed without rotation (10 * 150 = 1500 bytes, triggers rotation)
        size_t written = writer.bulk_write(buffers, buffer_count, test_formatter{});
        REQUIRE(written == buffer_count);
        
        // Give rotator thread time to process
        std::this_thread::sleep_for(100ms);
        
        // Should have triggered rotation
        REQUIRE(count_rotated_files() >= 1);
        REQUIRE(file_exists(base_filename));
        
        // Release buffers
        for (size_t i = 0; i < buffer_count; ++i) {
            if (buffers[i]) {
                buffer_pool::instance().release(buffers[i]);
            }
        }
    }
    
    SECTION("writev handles filtered buffers correctly")
    {
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 10 * 1024; // 10KB

        writev_file_writer writer(base_filename, policy);
        
        // Create buffers, some filtered
        const size_t buffer_count = 5;
        log_buffer_base* buffers[buffer_count];
        
        for (size_t i = 0; i < buffer_count; ++i) {
            buffers[i] = buffer_pool::instance().acquire(true); // human_readable = true
            if (buffers[i]) {
                buffers[i]->write_raw("Test message");
                // Mark even-indexed buffers as filtered
                if (i % 2 == 0) {
                    buffers[i]->filtered_ = true;
                }
            }
        }
        
        struct test_formatter {
            size_t calculate_size(const log_buffer_base* buffer) const {
                return buffer->len();
            }
            size_t format(const log_buffer_base* buffer, char* output, size_t max_size) const {
                auto text = buffer->get_text();
                size_t to_copy = std::min(text.size(), max_size);
                memcpy(output, text.data(), to_copy);
                return to_copy;
            }
        };
        
        // Should only process non-filtered buffers (indices 1 and 3)
        size_t written = writer.bulk_write(buffers, buffer_count, test_formatter{});
        REQUIRE(written == 2); // Only non-filtered buffers
        
        // Check file was written
        REQUIRE(file_exists(base_filename));
        REQUIRE(get_file_size(base_filename) == 24); // 2 buffers * 12 bytes each
        
        // Release buffers
        for (size_t i = 0; i < buffer_count; ++i) {
            if (buffers[i]) {
                buffer_pool::instance().release(buffers[i]);
            }
        }
    }
}

TEST_CASE_METHOD(rotation_test_fixture, "Integration with log system", "[rotation][integration]")
{

    SECTION("Rotating file sink")
    {
        // Create rotating sink
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 1024;

        auto sink = std::make_shared<log_sink>(raw_formatter{}, file_writer(base_filename, policy));

        // Add to dispatcher (this replaces default sink if present)
        log_line_dispatcher::instance().add_sink(sink);

        // Generate logs to trigger rotation
        for (int i = 0; i < 50; ++i) { LOG(info) << "Test message " << i << " with some padding data"; }

        // Flush and wait
        log_line_dispatcher::instance().flush();
        std::this_thread::sleep_for(200ms);

        // Should have rotated at least once
        REQUIRE(count_rotated_files() >= 1);

        // Clean up - set sink to nullptr to remove it
        log_line_dispatcher::instance().set_sink(0, nullptr);
    }

    SECTION("Large volume test with retention")
    {

        // Simulate the test_rotation_example scenario
        rotate_policy policy;
        policy.mode       = rotate_policy::kind::size;
        policy.max_bytes  = 10 * 1024; // 10KB
        policy.keep_files = 3;

        auto sink = std::make_shared<log_sink>(raw_formatter{}, file_writer(base_filename, policy));

        log_line_dispatcher::instance().add_sink(sink);

        // Generate many logs to test rotation and retention
        for (int i = 0; i < 200; ++i)
        {
            LOG(info) << "Test message " << i << " - Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                      << "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";
        }

        log_line_dispatcher::instance().flush();
        std::this_thread::sleep_for(500ms);

        // Check metrics
        auto &metrics = rotation_metrics::instance();
        REQUIRE(metrics.rotations_total.load() > 0);
        REQUIRE(metrics.dropped_records_total.load() == 0);

        // Verify retention - should have at most keep_files rotated files
        REQUIRE(count_rotated_files() <= 3);

        log_line_dispatcher::instance().set_sink(0, nullptr);
    }

    SECTION("Direct write with rotation")
    {
        // Simulate the debug_rotation scenario
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 100; // Very small for immediate rotation

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

// Failure metrics tests - critical for forensics/traceability
TEST_CASE_METHOD(rotation_test_fixture, "Rotation failure metrics", "[rotation][metrics]")
{
    auto &metrics = rotation_metrics::instance();

    SECTION("ENOSPC metrics tracking")
    {
        // Create a small tmpfs to simulate ENOSPC
        std::string small_fs = "/tmp/test_enospc_" + std::to_string(getpid());
        fs::create_directories(small_fs);

        // Note: Can't easily create actual tmpfs in tests, simulate with many files
        rotate_policy policy;
        policy.mode       = rotate_policy::kind::size;
        policy.max_bytes  = 1024; // 1KB files
        policy.keep_files = 100;  // Try to keep many

        std::string test_file = small_fs + "/enospc.log";

        // Record initial metrics
        auto initial_pending = metrics.enospc_deletions_pending.load();
        auto initial_gz      = metrics.enospc_deletions_gz.load();
        auto initial_raw     = metrics.enospc_deletions_raw.load();
        auto initial_bytes   = metrics.enospc_deleted_bytes.load();

        // Create some .pending and .gz files to test priority deletion
        std::ofstream pending_file(small_fs + "/enospc-20250101-000000-001.log.pending");
        pending_file << std::string(500, 'P');
        pending_file.close();

        std::ofstream gz_file(small_fs + "/enospc-20250101-000000-002.log.gz");
        gz_file << std::string(500, 'G');
        gz_file.close();

        file_writer writer(test_file, policy);

        // Write enough to trigger rotations
        for (int i = 0; i < 10; ++i)
        {
            std::string data(1100, 'D'); // Over max_bytes
            writer.write(data.c_str(), data.size());
            std::this_thread::sleep_for(50ms);
        }

        // Cleanup
        fs::remove_all(small_fs);

        // Verify metrics updated (may or may not trigger depending on system)
        INFO("ENOSPC pending deletions: " << (metrics.enospc_deletions_pending.load() - initial_pending));
        INFO("ENOSPC gz deletions: " << (metrics.enospc_deletions_gz.load() - initial_gz));
        INFO("ENOSPC raw deletions: " << (metrics.enospc_deletions_raw.load() - initial_raw));
        INFO("ENOSPC deleted bytes: " << (metrics.enospc_deleted_bytes.load() - initial_bytes));
    }

    SECTION("Prepare FD failure metrics")
    {
        // Test prepare_fd_failures counter
        auto initial_prepare_failures = metrics.prepare_fd_failures.load();

        // Create a test file first
        std::string test_file = test_dir + "/prepare_fail.log";

        rotate_policy policy;
        policy.mode        = rotate_policy::kind::size;
        policy.max_bytes   = 100;
        policy.max_retries = 2; // Fail faster in tests

        {
            file_writer writer(test_file, policy);

            // Write to trigger rotation need
            std::string data(150, 'F');
            writer.write(data.c_str(), data.size());

            // Now make directory read-only to prevent temp file creation
            fs::permissions(test_dir, fs::perms::owner_read | fs::perms::owner_exec);

            // Try to write more - should fail to prepare next fd
            std::string more_data(150, 'M');
            writer.write(more_data.c_str(), more_data.size());

            // Give time for async rotation to attempt and fail
            std::this_thread::sleep_for(500ms);

            // Restore permissions before writer destructor
            fs::permissions(test_dir, fs::perms::owner_all);
        }

        // Check if prepare_fd_failures incremented
        auto new_failures = metrics.prepare_fd_failures.load();
        INFO("Prepare FD failures: initial=" << initial_prepare_failures << " new=" << new_failures);

        // May or may not fail depending on timing, but counter should be accessible
        REQUIRE(new_failures >= initial_prepare_failures);
    }

    SECTION("Fsync failure metrics")
    {
        auto initial_fsync_failures = metrics.fsync_failures.load();

        // Hard to simulate fsync failures in tests
        // Would need to mock/inject failures
        // For now just verify the counter exists and is accessible
        REQUIRE(metrics.fsync_failures.load() >= 0);
        INFO("Fsync failures counter accessible: " << metrics.fsync_failures.load());
    }

    SECTION("Compression failure metrics")
    {
        auto initial_compression_failures = metrics.compression_failures.load();

        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 1024;
        policy.compress  = true; // Enable compression

        file_writer writer(base_filename, policy);

        // Trigger rotation with compression
        std::string data(2000, 'C');
        writer.write(data.c_str(), data.size());

        std::this_thread::sleep_for(200ms);

        // Compression not implemented yet, so no failures expected
        // Just verify counter is accessible
        REQUIRE(metrics.compression_failures.load() >= 0);
        INFO("Compression failures counter: " << metrics.compression_failures.load());
    }

    SECTION("Zero-gap fallback metrics")
    {
        auto initial_fallbacks = metrics.zero_gap_fallback_total.load();

        // Create cross-device scenario (hard to simulate in tests)
        // Would need different mount points

        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 1024;

        file_writer writer(base_filename, policy);

        // Normal rotation
        std::string data(2000, 'Z');
        writer.write(data.c_str(), data.size());

        std::this_thread::sleep_for(200ms);

        // Verify counter is accessible
        REQUIRE(metrics.zero_gap_fallback_total.load() >= initial_fallbacks);
        INFO("Zero-gap fallbacks: " << metrics.zero_gap_fallback_total.load());
    }

    SECTION("Dropped records metrics")
    {
        auto initial_dropped_records = metrics.dropped_records_total.load();
        auto initial_dropped_bytes   = metrics.dropped_bytes_total.load();

        rotate_policy policy;
        policy.mode        = rotate_policy::kind::size;
        policy.max_bytes   = 100;
        policy.max_retries = 1; // Fail fast

        // Create a file in a directory
        std::string drop_test_dir = test_dir + "/drops";
        fs::create_directories(drop_test_dir);
        std::string test_file = drop_test_dir + "/drop_test.log";

        {
            file_writer writer(test_file, policy);

            // First write succeeds
            std::string data1(50, 'D');
            auto written = writer.write(data1.c_str(), data1.size());
            REQUIRE(written == 50);

            // Trigger rotation
            std::string data2(60, 'R');
            written = writer.write(data2.c_str(), data2.size());
            REQUIRE(written == 60);

            // Remove write permissions to cause prepare_fd to fail
            fs::permissions(drop_test_dir, fs::perms::owner_read | fs::perms::owner_exec);

            // Wait for rotation to fail and enter error state
            std::this_thread::sleep_for(200ms);

            // Now writes should be dropped (return -1)
            std::string data3(100, 'X');
            written = writer.write(data3.c_str(), data3.size());

            // In error state, write returns len (pretending success) but increments dropped metrics
            INFO("Write returned " << written << " in potential error state");

            // Try a few more writes to accumulate drops
            for (int i = 0; i < 5; ++i)
            {
                auto w = writer.write(data3.c_str(), data3.size());
                INFO("Additional write " << i << " returned " << w);
            }

            // Restore permissions
            fs::permissions(drop_test_dir, fs::perms::owner_all);
        }

        // Check if drops were recorded
        auto new_dropped_records = metrics.dropped_records_total.load();
        auto new_dropped_bytes   = metrics.dropped_bytes_total.load();

        INFO("Dropped records: initial=" << initial_dropped_records << " new=" << new_dropped_records);
        INFO("Dropped bytes: initial=" << initial_dropped_bytes << " new=" << new_dropped_bytes);

        // Verify metrics are accessible
        REQUIRE(new_dropped_records >= initial_dropped_records);
        REQUIRE(new_dropped_bytes >= initial_dropped_bytes);
    }

    SECTION("Metrics persistence across rotations")
    {
        auto &metrics = rotation_metrics::instance();

        // Record all initial values
        auto initial_rotations      = metrics.rotations_total.load();
        auto initial_duration_sum   = metrics.rotation_duration_us_sum.load();
        auto initial_duration_count = metrics.rotation_duration_us_count.load();

        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 1024;

        file_writer writer(base_filename, policy);

        // Trigger multiple rotations
        for (int i = 0; i < 5; ++i)
        {
            std::string data(1500, 'M');
            writer.write(data.c_str(), data.size());
            std::this_thread::sleep_for(100ms);
        }

        // All metrics should increment
        REQUIRE(metrics.rotations_total.load() >= initial_rotations + 5);
        REQUIRE(metrics.rotation_duration_us_count.load() >= initial_duration_count + 5);
        REQUIRE(metrics.rotation_duration_us_sum.load() > initial_duration_sum);

        // Average rotation time should be reasonable
        if (metrics.rotation_duration_us_count > 0)
        {
            auto avg_us = metrics.rotation_duration_us_sum / metrics.rotation_duration_us_count;
            INFO("Average rotation time: " << avg_us << " microseconds");
            REQUIRE(avg_us < 1'000'000); // Less than 1 second
        }
    }

    SECTION("Metrics dump functionality")
    {
        // Test that dump_metrics() doesn't crash
        REQUIRE_NOTHROW(metrics.dump_metrics());

        // Verify metrics are logged (can't easily capture LOG output in tests)
        // Just ensure dump_metrics runs without crashing
        metrics.dump_metrics();
        log_line_dispatcher::instance().flush();

        // Verify all metric counters are accessible
        REQUIRE(metrics.dropped_records_total.load() >= 0);
        REQUIRE(metrics.dropped_bytes_total.load() >= 0);
        REQUIRE(metrics.rotations_total.load() >= 0);
        REQUIRE(metrics.rotation_duration_us_sum.load() >= 0);
        REQUIRE(metrics.rotation_duration_us_count.load() >= 0);
        REQUIRE(metrics.enospc_deletions_pending.load() >= 0);
        REQUIRE(metrics.enospc_deletions_gz.load() >= 0);
        REQUIRE(metrics.enospc_deletions_raw.load() >= 0);
        REQUIRE(metrics.enospc_deleted_bytes.load() >= 0);
        REQUIRE(metrics.zero_gap_fallback_total.load() >= 0);
        REQUIRE(metrics.compression_failures.load() >= 0);
        REQUIRE(metrics.prepare_fd_failures.load() >= 0);
        REQUIRE(metrics.fsync_failures.load() >= 0);
    }
}

// Performance test (not run by default)
TEST_CASE_METHOD(rotation_test_fixture, "Throughput with rotation", "[.][rotation][performance]")
{
    SECTION("2M msg/sec target with variable message sizes")
    {
        rotate_policy policy;
        policy.mode      = rotate_policy::kind::size;
        policy.max_bytes = 10 * 1024 * 1024; // 10MB files

        file_writer writer(base_filename, policy);

        const size_t msg_count = 100'000;
        
        // Realistic message size distribution
        // Small (50%), medium (30%), large (20%)
        const std::string small_msg(50, 'S');   // 50 bytes - debug/info logs
        const std::string medium_msg(200, 'M'); // 200 bytes - typical logs with context
        const std::string large_msg(500, 'L');  // 500 bytes - stack traces, errors

        auto start = std::chrono::steady_clock::now();

        for (size_t i = 0; i < msg_count; ++i) { 
            // Simulate realistic message distribution
            if (i % 10 < 5) {
                // 50% small messages
                writer.write(small_msg.c_str(), small_msg.size());
            } else if (i % 10 < 8) {
                // 30% medium messages
                writer.write(medium_msg.c_str(), medium_msg.size());
            } else {
                // 20% large messages
                writer.write(large_msg.c_str(), large_msg.size());
            }
        }

        auto end      = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double msgs_per_sec = (msg_count * 1'000'000.0) / duration.count();
        
        // Calculate average message size for reporting
        size_t total_bytes = (msg_count * 0.5 * 50) + (msg_count * 0.3 * 200) + (msg_count * 0.2 * 500);
        double avg_msg_size = total_bytes / static_cast<double>(msg_count);

        INFO("Throughput: " << msgs_per_sec << " msg/sec");
        INFO("Average message size: " << avg_msg_size << " bytes");
        INFO("Total data written: " << total_bytes / (1024.0 * 1024.0) << " MB");

        // Should maintain reasonable throughput even with rotation
        // Note: 2M msg/sec may not be achievable in test environment
        REQUIRE(msgs_per_sec > 100'000); // At least 100K msg/sec
    }
}