/**
 * @file rotation_demo.cpp
 * @brief Comprehensive demonstration of file rotation functionality
 * @author dorgby.net
 *
 * This demo showcases all rotation features:
 * - Size-based rotation
 * - Time-based rotation
 * - Combined size/time rotation
 * - Retention policies
 * - Zero-gap rotation
 * - Metrics reporting
 * - ENOSPC handling
 * - Multi-threaded logging
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <filesystem>
#include <atomic>
#include <random>

#include "slwoggy.hpp"

using namespace slwoggy;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

// Global control flags
std::atomic<bool> stop_threads{false};
std::atomic<uint64_t> total_messages{0};

void print_header(const std::string &title)
{
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << " " << title << "\n";
    std::cout << std::string(60, '=') << "\n\n";
}

void print_metrics()
{
#ifdef LOG_COLLECT_ROTATION_METRICS
    auto &metrics = rotation_metrics::instance();

    std::cout << "\nRotation Metrics:\n";
    std::cout << "  Total rotations: " << metrics.rotations_total.load() << "\n";
    std::cout << "  Dropped records: " << metrics.dropped_records_total.load() << "\n";
    std::cout << "  Dropped bytes: " << metrics.dropped_bytes_total.load() << "\n";

    if (metrics.rotation_duration_us_count > 0)
    {
        auto avg_us = metrics.rotation_duration_us_sum / metrics.rotation_duration_us_count;
        std::cout << "  Avg rotation time: " << avg_us << " us\n";
    }

    if (metrics.zero_gap_fallback_total > 0)
    {
        std::cout << "  Zero-gap fallbacks: " << metrics.zero_gap_fallback_total.load() << "\n";
    }

    if (metrics.enospc_deletions_raw > 0 || metrics.enospc_deletions_gz > 0)
    {
        std::cout << "  ENOSPC deletions: raw=" << metrics.enospc_deletions_raw.load()
                  << " gz=" << metrics.enospc_deletions_gz.load() << " bytes=" << metrics.enospc_deleted_bytes.load() << "\n";
    }

    std::cout << std::flush;
#else
    std::cout << "\nRotation metrics not collected (Release build)\n";
#endif
}

void count_rotated_files(const std::string &base_path)
{
    fs::path base(base_path);
    fs::path dir = base.parent_path();
    if (dir.empty()) dir = ".";
    std::string stem = base.stem().string();

    size_t count      = 0;
    size_t total_size = 0;

    for (const auto &entry : fs::directory_iterator(dir))
    {
        if (entry.is_regular_file())
        {
            std::string filename = entry.path().filename().string();
            if (filename.find(stem + "-") == 0)
            {
                count++;
                total_size += entry.file_size();
            }
        }
    }

    std::cout << "  Rotated files: " << count << " (total size: " << total_size / 1024 << " KB)\n";
}

// Worker thread for continuous logging
void logging_worker(int thread_id, int messages_per_second)
{
    auto delay = std::chrono::milliseconds(1000 / messages_per_second);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(50, 200);

    while (!stop_threads.load())
    {
        int msg_size = size_dist(gen);
        std::string padding(msg_size, 'X');

        LOG(info) << "Thread-" << thread_id << " msg#" << total_messages.fetch_add(1) << " " << padding;

        std::this_thread::sleep_for(delay);
    }
}

// Demo 1: Size-based rotation
void demo_size_rotation()
{
    print_header("Demo 1: Size-Based Rotation");

    std::string log_dir = "/tmp/rotation_demo";
    fs::create_directories(log_dir);
    std::string log_file = log_dir + "/size_rotation.log";

    // Note: Not removing old files to demonstrate retention policy

    // Configure rotation: rotate at 100KB, keep 5 files
    rotate_policy policy;
    policy.mode       = rotate_policy::kind::size;
    policy.max_bytes  = 100 * 1024; // 100KB
    policy.keep_files = 5;

    // Create rotating sink
    auto sink = std::make_shared<log_sink>(raw_formatter{true, true}, file_writer(log_file, policy));

    // Replace default sink with our rotating sink
    log_line_dispatcher::instance().set_sink(0, sink);

    std::cout << "Configuration:\n";
    std::cout << "  Max file size: 100 KB\n";
    std::cout << "  Keep files: 5\n";
    std::cout << "  Log directory: " << log_dir << "\n\n";

    // Generate logs to trigger rotation
    std::cout << "Generating logs to trigger rotation...\n";
    for (int i = 0; i < 1000; ++i)
    {
        LOG(info) << "Size rotation test message " << i << " - Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                  << "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";

        if (i % 100 == 0) { std::cout << "  Generated " << i << " messages\n"; }
    }

    log_line_dispatcher::instance().flush();
    std::this_thread::sleep_for(200ms);

    count_rotated_files(log_file);
    print_metrics();
}

// Demo 2: Time-based rotation with second-level precision
void demo_time_rotation()
{
    print_header("Demo 2: Time-Based Rotation (Second-Level Precision)");

    std::string log_dir  = "/tmp/rotation_demo";
    std::string log_file = log_dir + "/time_rotation.log";

    // Note: Not removing old files to demonstrate retention policy

    // Configure rotation: rotate every 10 seconds
    rotate_policy policy;
    policy.mode       = rotate_policy::kind::time;
    policy.every      = std::chrono::seconds(10); // 10 second rotation for demo
    policy.keep_files = 10;

    // Create rotating sink
    auto sink = std::make_shared<log_sink>(raw_formatter{true, true}, file_writer(log_file, policy));

    // Replace default sink with our rotating sink
    log_line_dispatcher::instance().set_sink(0, sink);

    std::cout << "Configuration:\n";
    std::cout << "  Rotation interval: 10 seconds\n";
    std::cout << "  Keep files: 10\n";
    std::cout << "  Log directory: " << log_dir << "\n\n";

    std::cout << "Logging for 25 seconds to trigger 2 time-based rotations...\n";

    auto start    = std::chrono::steady_clock::now();
    int msg_count = 0;

    while (std::chrono::steady_clock::now() - start < 25s)
    {
        LOG(info) << "Time rotation test message " << msg_count++ 
                  << " at " << std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::steady_clock::now() - start).count() << "s";
        std::this_thread::sleep_for(100ms);

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed > 0 && elapsed % 5 == 0 && msg_count % 10 == 0)
        {
            std::cout << "  Elapsed: " << elapsed << "s, Messages: " << msg_count;
            
            // Check if rotation happened
#ifdef LOG_COLLECT_ROTATION_METRICS
            auto &metrics = rotation_metrics::instance();
            auto rotations = metrics.rotations_total.load();
#else
            auto rotations = 0;
#endif
            if (rotations > 0) {
                std::cout << " (Rotations: " << rotations << ")";
            }
            std::cout << "\n";
        }
    }

    log_line_dispatcher::instance().flush();
    std::this_thread::sleep_for(200ms);

    count_rotated_files(log_file);
    print_metrics();
}

// Demo 3: Combined size and time rotation
void demo_combined_rotation()
{
    print_header("Demo 3: Combined Size OR Time Rotation");

    std::string log_dir  = "/tmp/rotation_demo";
    std::string log_file = log_dir + "/combined_rotation.log";

    // Note: Not removing old files to demonstrate retention policy

    // Configure rotation: rotate at 50KB OR every 15 seconds
    rotate_policy policy;
    policy.mode       = rotate_policy::kind::size_or_time;
    policy.max_bytes  = 50 * 1024;               // 50KB
    policy.every      = std::chrono::seconds(15); // 15 second rotation
    policy.keep_files = 8;

    // Create rotating sink
    auto sink = std::make_shared<log_sink>(raw_formatter{}, file_writer(log_file, policy));

    // Replace default sink with our rotating sink
    log_line_dispatcher::instance().set_sink(0, sink);

    std::cout << "Configuration:\n";
    std::cout << "  Max file size: 50 KB\n";
    std::cout << "  Max time: 15 seconds\n";
    std::cout << "  Keep files: 8\n";
    std::cout << "  Log directory: " << log_dir << "\n\n";

    std::cout << "Generating variable-rate logs...\n";

    // Burst of logs (should trigger size rotation)
    std::cout << "  Phase 1: Burst logging (size trigger)...\n";
    for (int i = 0; i < 500; ++i) { LOG(info) << "Burst message " << i << " with padding: " << std::string(100, 'B'); }

    // Slow logging (should trigger time rotation)
    std::cout << "  Phase 2: Slow logging (time trigger)...\n";
    for (int i = 0; i < 20; ++i)
    {
        LOG(info) << "Slow message " << i;
        std::this_thread::sleep_for(600ms);
    }

    log_line_dispatcher::instance().flush();
    std::this_thread::sleep_for(200ms);

    count_rotated_files(log_file);
    print_metrics();
}

// Demo 4: Retention policies
void demo_retention_policies()
{
    print_header("Demo 4: Retention Policies");

    std::string log_dir  = "/tmp/rotation_demo";
    std::string log_file = log_dir + "/retention.log";

    // Note: Not removing old files to demonstrate retention policy

    // Configure with multiple retention policies
    rotate_policy policy;
    policy.mode            = rotate_policy::kind::size;
    policy.max_bytes       = 20 * 1024;             // 20KB files
    policy.keep_files      = 3;                     // Keep only 3 files
    policy.max_total_bytes = 80 * 1024;             // Max 80KB total
    policy.max_age         = std::chrono::seconds(3600); // Files older than 1 hour (3600s)

    // Create rotating sink
    auto sink = std::make_shared<log_sink>(raw_formatter{.add_newline = true}, file_writer(log_file, policy));

    // Replace default sink with our rotating sink
    log_line_dispatcher::instance().set_sink(0, sink);

    std::cout << "Configuration:\n";
    std::cout << "  Max file size: 20 KB\n";
    std::cout << "  Keep files: 3\n";
    std::cout << "  Max total size: 80 KB\n";
    std::cout << "  Max age: 1 hour\n";
    std::cout << "  Log directory: " << log_dir << "\n\n";

    std::cout << "Creating multiple rotations to test retention...\n";

    // Generate 6 rotations (but only 3 should be kept)
    for (int rotation = 0; rotation < 6; ++rotation)
    {
        std::cout << "  Creating rotation " << (rotation + 1) << "...\n";
        for (int i = 0; i < 150; ++i)
        {
            LOG(info) << "Retention test rotation " << rotation << " message " << i << " " << std::string(80, 'R');
        }
        std::this_thread::sleep_for(100ms);
    }

    log_line_dispatcher::instance().flush();
    std::this_thread::sleep_for(500ms);

    count_rotated_files(log_file);
    print_metrics();
}

// Demo 5: Multi-threaded stress test
void demo_multithreaded()
{
    print_header("Demo 5: Multi-threaded Stress Test");

    std::string log_dir  = "/tmp/rotation_demo";
    std::string log_file = log_dir + "/multithread.log";

    // Note: Not removing old files to demonstrate retention policy

    // Configure for high throughput
    rotate_policy policy;
    policy.mode           = rotate_policy::kind::size;
    policy.max_bytes      = 1024 * 1024; // 1MB files
    policy.keep_files     = 10;
    policy.sync_on_rotate = true; // Ensure durability

    // Use optimized writer for better performance
    auto sink = std::make_shared<log_sink>(raw_formatter{}, writev_file_writer(log_file, policy));

    // Replace default sink with our rotating sink
    log_line_dispatcher::instance().set_sink(0, sink);

    std::cout << "Configuration:\n";
    std::cout << "  Max file size: 1 MB\n";
    std::cout << "  Keep files: 10\n";
    std::cout << "  Sync on rotate: true\n";
    std::cout << "  Worker threads: 4\n";
    std::cout << "  Duration: 10 seconds\n";
    std::cout << "  Log directory: " << log_dir << "\n\n";

    // Reset counter
    total_messages.store(0);
    stop_threads.store(false);

    // Start worker threads
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) { workers.emplace_back(logging_worker, i, 100); }

    // Monitor progress
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < 10s)
    {
        std::this_thread::sleep_for(2s);
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        std::cout << "  Progress: " << elapsed << "s, Messages: " << total_messages.load() << "\n";
    }

    // Stop workers
    stop_threads.store(true);
    for (auto &t : workers) { t.join(); }

    log_line_dispatcher::instance().flush();
    std::this_thread::sleep_for(500ms);

    std::cout << "\nResults:\n";
    std::cout << "  Total messages logged: " << total_messages.load() << "\n";

    auto duration     = std::chrono::steady_clock::now() - start;
    auto duration_sec = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    if (duration_sec > 0) { std::cout << "  Throughput: " << (total_messages.load() / duration_sec) << " msg/sec\n"; }

    count_rotated_files(log_file);
    print_metrics();
}

// Demo 6: ENOSPC simulation (optional - requires small partition)
void demo_enospc_handling()
{
    print_header("Demo 6: ENOSPC Handling (Simulation)");

    std::cout << "This demo simulates disk space exhaustion scenarios.\n";
    std::cout << "For actual testing, mount a small tmpfs:\n";
    std::cout << "  sudo mkdir -p /mnt/small\n";
    std::cout << "  sudo mount -t tmpfs -o size=1M tmpfs /mnt/small\n\n";

    std::string log_dir  = "/tmp/rotation_demo"; // Use regular /tmp for demo
    std::string log_file = log_dir + "/enospc.log";

    // Configure with small files to simulate many rotations
    rotate_policy policy;
    policy.mode       = rotate_policy::kind::size;
    policy.max_bytes  = 10 * 1024; // 10KB files
    policy.keep_files = 100;       // Try to keep many files

    // Create rotating sink
    auto sink = std::make_shared<log_sink>(raw_formatter{}, file_writer(log_file, policy));

    // Replace default sink with our rotating sink
    log_line_dispatcher::instance().set_sink(0, sink);

    std::cout << "Configuration:\n";
    std::cout << "  Max file size: 10 KB\n";
    std::cout << "  Keep files: 100 (will trigger cleanup)\n";
    std::cout << "  Log directory: " << log_dir << "\n\n";

    std::cout << "Generating logs to test cleanup behavior...\n";

    for (int i = 0; i < 500; ++i)
    {
        LOG(info) << "ENOSPC test message " << i << " " << std::string(100, 'E');

        if (i % 100 == 0)
        {
            std::cout << "  Generated " << i << " messages\n";

            // Check if ENOSPC cleanup happened
#ifdef LOG_COLLECT_ROTATION_METRICS
            auto &metrics = rotation_metrics::instance();
            if (metrics.enospc_deletions_raw > 0 || metrics.enospc_deletions_gz > 0)
#else
            if (false)
#endif
            {
                std::cout << "  ENOSPC cleanup triggered!\n";
                break;
            }
        }
    }

    log_line_dispatcher::instance().flush();
    std::this_thread::sleep_for(200ms);

    count_rotated_files(log_file);
    print_metrics();
}

// Demo 7: Compression
void demo_compression()
{
    print_header("Demo 7: Gzip Compression");

    std::string log_dir = "/tmp/rotation_demo";
    fs::create_directories(log_dir);
    std::string log_file = log_dir + "/compress.log";

    // Clean up old compressed files for clear demo
    for (const auto &entry : fs::directory_iterator(log_dir))
    {
        if (entry.path().filename().string().find("compress-") == 0) { fs::remove(entry.path()); }
    }

    // Configure rotation with compression enabled
    rotate_policy policy;
    policy.mode       = rotate_policy::kind::size;
    policy.max_bytes  = 50 * 1024; // 50KB files
    policy.keep_files = 5;
    policy.compress   = true; // Enable gzip compression

    // Create rotating sink
    auto sink = std::make_shared<log_sink>(raw_formatter{true, true}, file_writer(log_file, policy));

    // Replace default sink with our rotating sink
    log_line_dispatcher::instance().set_sink(0, sink);

    std::cout << "Configuration:\n";
    std::cout << "  Max file size: 50 KB\n";
    std::cout << "  Keep files: 5\n";
    std::cout << "  Compression: ENABLED (gzip)\n";
    std::cout << "  Log directory: " << log_dir << "\n\n";

    // Generate logs to trigger rotation and compression
    std::cout << "Generating logs to trigger rotation with compression...\n";
    for (int i = 0; i < 2000; ++i)
    {
        // Use repetitive text for good compression ratio
        LOG(info) << "Compression test message " << i << " - "
                  << "The quick brown fox jumps over the lazy dog. "
                  << "The quick brown fox jumps over the lazy dog. "
                  << "The quick brown fox jumps over the lazy dog.";

        if (i % 500 == 0 && i > 0) { std::cout << "  Generated " << i << " messages\n"; }
    }

    log_line_dispatcher::instance().flush();
    std::this_thread::sleep_for(1s); // Give compression time to complete

    // Show compression results
    std::cout << "\nCompression Results:\n";
    size_t uncompressed_total = 0;
    size_t compressed_total   = 0;
    int compressed_count      = 0;

    for (const auto &entry : fs::directory_iterator(log_dir))
    {
        std::string filename = entry.path().filename().string();
        if (filename.starts_with("compress-"))
        {
            if (filename.ends_with(".gz"))
            {
                compressed_total += entry.file_size();
                compressed_count++;
                std::cout << "  " << filename << ": " << (entry.file_size() / 1024) << " KB (compressed)\n";
            }
            else if (filename.ends_with(".log"))
            {
                uncompressed_total += entry.file_size();
                std::cout << "  " << filename << ": " << (entry.file_size() / 1024) << " KB\n";
            }
        }
    }

    if (compressed_count > 0)
    {
        // Estimate original size based on current log file size
        size_t estimated_original = compressed_count * 50 * 1024; // Each was ~50KB before compression
        float ratio               = 100.0f - (compressed_total * 100.0f / estimated_original);
        std::cout << "\n  Compression ratio: ~" << std::fixed << std::setprecision(1) << ratio << "% reduction\n";
        std::cout << "  " << compressed_count << " files compressed\n";
    }

    print_metrics();
}

int main(int argc, char *argv[])
{
    std::cout << "slwoggy File Rotation Demo\n";
    std::cout << "==========================\n";
    std::cout << "\nThis demo showcases all file rotation features.\n";

    if (argc > 1)
    {
        std::string demo = argv[1];
        if (demo == "1" || demo == "size") { demo_size_rotation(); }
        else if (demo == "2" || demo == "time") { demo_time_rotation(); }
        else if (demo == "3" || demo == "combined") { demo_combined_rotation(); }
        else if (demo == "4" || demo == "retention") { demo_retention_policies(); }
        else if (demo == "5" || demo == "multithread") { demo_multithreaded(); }
        else if (demo == "6" || demo == "enospc") { demo_enospc_handling(); }
        else if (demo == "7" || demo == "compress") { demo_compression(); }
        else
        {
            std::cout << "\nUsage: " << argv[0] << " [demo_number|demo_name]\n";
            std::cout << "  1 or size      - Size-based rotation\n";
            std::cout << "  2 or time      - Time-based rotation\n";
            std::cout << "  3 or combined  - Combined size/time rotation\n";
            std::cout << "  4 or retention - Retention policies\n";
            std::cout << "  5 or multithread - Multi-threaded stress test\n";
            std::cout << "  6 or enospc    - ENOSPC handling\n";
            std::cout << "  7 or compress  - Gzip compression\n";
        }
    }
    else
    {
        // Run all demos
        demo_size_rotation();
        demo_time_rotation();
        demo_combined_rotation();
        demo_retention_policies();
        demo_multithreaded();
        demo_enospc_handling();
        demo_compression();

        print_header("Final Summary");
        print_metrics();

        // Dump detailed metrics to log
#ifdef LOG_COLLECT_ROTATION_METRICS
        rotation_metrics::instance().dump_metrics();
#endif
        log_line_dispatcher::instance().flush();
    }

    std::cout << "\nDemo complete.\n";
    return 0;
}