#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <cstring>
#include <random>
#include "slwoggy.hpp"

using namespace slwoggy;

LOG_MODULE_NAME("main");

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -t <threads>      Number of threads (default: 5)\n"
              << "  -m <messages>     Messages per thread (default: 1000000)\n"
              << "  -s <size>         Message size in bytes (default: 50)\n"
              << "  -w <sink>         Sink type: raw, writev, json, stdout (default: writev)\n"
              << "  -f <file>         Output file (default: /tmp/log.txt)\n"
              << "  -d                Show detailed statistics\n"
              << "  -h                Show this help\n";
}

int main(int argc, char *argv[])
{
    // Default parameters
    int num_threads = 5;
    int messages_per_thread = 1000000;
    int message_size = 50;
    std::string sink_type = "raw";
    std::string output_file = "/dev/null";
    bool show_detailed_stats = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            num_threads = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            messages_per_thread = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            message_size = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            sink_type = argv[++i];
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0) {
            show_detailed_stats = true;
        } else if (strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate parameters
    if (num_threads < 1 || num_threads > 100) {
        std::cerr << "Error: threads must be between 1 and 100\n";
        return 1;
    }
    if (messages_per_thread < 1) {
        std::cerr << "Error: messages must be positive\n";
        return 1;
    }
    if (message_size < 10 || message_size > 10000) {
        std::cerr << "Error: message size must be between 10 and 10000 bytes\n";
        return 1;
    }

    // Pre-register structured log keys to avoid mutex contention during logging
    auto& key_registry = structured_log_key_registry::instance();
    key_registry.get_or_register_key(std::string_view("thread_id"));
    key_registry.get_or_register_key(std::string_view("iteration"));
    key_registry.get_or_register_key(std::string_view("hi"));
    key_registry.get_or_register_key(std::string_view("hello"));

    rotate_policy policy;
    policy.mode           = rotate_policy::kind::size;
    policy.max_bytes      = 1 * 1024 * 1024; // 1MB files
    policy.keep_files     = 10;
    //policy.compress       = true; // Enable compression

    // Create sink based on type
    if (sink_type == "raw") {
        log_line_dispatcher::instance().add_sink(make_raw_file_sink(output_file, policy));
    } else if (sink_type == "writev") {
        log_line_dispatcher::instance().add_sink(make_writev_file_sink(output_file, policy));
    } else if (sink_type == "json") {
        log_line_dispatcher::instance().add_sink(make_json_sink(output_file, policy));
    } else if (sink_type == "stdout") {
        // Use the default stdout sink
    } else {
        std::cerr << "Error: unknown sink type: " << sink_type << "\n";
        print_usage(argv[0]);
        return 1;
    }

    // Create threads that will blast logs simultaneously
    std::mutex start_mutex;
    std::condition_variable start_cv;
    bool start_signal = false;
    std::vector<std::thread> threads;
    
    // Generate array of random message sizes for more realistic testing
    constexpr size_t RANDOM_SIZE_COUNT = 1000;
    std::vector<int> random_sizes(RANDOM_SIZE_COUNT);
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        // Create distribution: 80% small (10-50 bytes), 20% larger (up to message_size)
        std::discrete_distribution<> size_type({80, 20});
        std::uniform_int_distribution<> small_size(10, 50);
        std::uniform_int_distribution<> large_size(51, message_size);
        
        for (size_t i = 0; i < RANDOM_SIZE_COUNT; ++i) {
            if (size_type(gen) == 0) {
                random_sizes[i] = small_size(gen);
            } else {
                random_sizes[i] = large_size(gen);
            }
        }
    }
    
    // Pre-create a large padding buffer to avoid allocations
    constexpr size_t MAX_PADDING = 10000;
    char padding_buffer[MAX_PADDING];
    std::memset(padding_buffer, 'X', MAX_PADDING);
    padding_buffer[MAX_PADDING - 1] = '\0';  // Ensure null termination

    // Thread function
    auto thread_func = [&](int thread_id) {
        // Wait for start signal
        {
            std::unique_lock<std::mutex> lock(start_mutex);
            start_cv.wait(lock, [&] { return start_signal; });
        }

        // Blast logs with variable sizes
        for (int i = 0; i < messages_per_thread; ++i)
        {
            // Get size for this message from pre-generated array
            int target_size = random_sizes[i % RANDOM_SIZE_COUNT];
            
            // Base message is ~20-25 chars: "Thread X iteration Y"
            const int base_size = 25;
            
            if (target_size <= base_size) {
                // Small message - no padding
                auto l = LOG(info);
                //l.printf("Thread %d iteration %d", thread_id, i);
                l.format("Thread {} iteration {}", thread_id, i);
                l.add("thread_id", thread_id);
                l.add("iteration", i);
            }
            else {
                // Add padding to reach target size using printf precision
                int pad_size = target_size - base_size;
                if (pad_size > MAX_PADDING - 1) pad_size = MAX_PADDING - 1;

                // Use %.*s to specify exact number of characters from padding buffer
                auto l = LOG(info);
                
                //l.printf("Thread %d iteration %d %.*s", thread_id, i, pad_size, padding_buffer);
                l.format("Thread {} iteration {} {}", thread_id, i, std::string_view(padding_buffer, pad_size));

                l.add("iteration", i);
                l.add("thread_id", thread_id);
            }
        }
    };

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(thread_func, i);
    }

    // Give threads time to reach wait state
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Signal all threads to start
    {
        std::lock_guard<std::mutex> lock(start_mutex);
        start_signal = true;
    }
    start_cv.notify_all();
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }

    log_line_dispatcher::instance().flush();

    // Print out metrics from dispatcher and buffer pool
#ifdef LOG_COLLECT_DISPATCHER_METRICS
    // For file sinks, we need an extra flush to ensure everything is written
    if (sink_type != "stdout")
    {
        log_line_dispatcher::instance().flush();
    }
    
    // Now get stats after all flushes are complete
    auto dispatcher_stats = log_line_dispatcher::instance().get_stats();

    std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // let the console catch up
    if (show_detailed_stats) {
        std::cerr << "========== DISPATCHER METRICS ==========\n";
        std::cerr << "Message Processing:\n";
        std::cerr << "  Uptime: " << std::chrono::duration_cast<std::chrono::milliseconds>(dispatcher_stats.uptime).count() << " ms\n";
        std::cerr << "  Total dispatched: " << dispatcher_stats.total_dispatched << "\n";
#ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
        std::cerr << "  Messages/second (1s): " << std::fixed << std::setprecision(2) << dispatcher_stats.messages_per_second_1s << "\n";
        std::cerr << "  Messages/second (10s): " << std::fixed << std::setprecision(2) << dispatcher_stats.messages_per_second_10s << "\n";
        std::cerr << "  Messages/second (60s): " << std::fixed << std::setprecision(2) << dispatcher_stats.messages_per_second_60s << "\n";
#endif
        std::cerr << "  Messages dropped: " << dispatcher_stats.messages_dropped << "\n";
        
        std::cerr << "Batch Processing:\n";
        std::cerr << "  Total batches: " << dispatcher_stats.total_batches << "\n";
        std::cerr << "  Min batch size: " << dispatcher_stats.min_batch_size << "\n";
        std::cerr << "  Avg batch size: " << std::fixed << std::setprecision(2) << dispatcher_stats.avg_batch_size << "\n";
        std::cerr << "  Max batch size: " << dispatcher_stats.max_batch_size << "\n";
        
        std::cerr << "Queue Performance:\n";
        std::cerr << "  Max queue size: " << dispatcher_stats.max_queue_size << "\n";
        std::cerr << "  Queue failures: " << dispatcher_stats.queue_enqueue_failures << "\n";
        std::cerr << "  Worker iterations: " << dispatcher_stats.worker_iterations << "\n";
        
        std::cerr << "Dispatch Timing:\n";
        std::cerr << "  Avg dispatch time: " << std::fixed << std::setprecision(2) << dispatcher_stats.avg_dispatch_time_us << " µs\n";
        std::cerr << "  Max dispatch time: " << dispatcher_stats.max_dispatch_time_us << " µs\n";
        
        std::cerr << "In-Flight Timing:\n";
        std::cerr << "  Min in-flight time: " << dispatcher_stats.min_inflight_time_us << " µs\n";
        std::cerr << "  Avg in-flight time: " << std::fixed << std::setprecision(2) << dispatcher_stats.avg_inflight_time_us << " µs\n";
        std::cerr << "  Max in-flight time: " << dispatcher_stats.max_inflight_time_us << " µs\n";
        
        std::cerr << "Dequeue Timing:\n";
        std::cerr << "  Min dequeue time: " << dispatcher_stats.min_dequeue_time_us << " µs\n";
        std::cerr << "  Avg dequeue time: " << std::fixed << std::setprecision(2) << dispatcher_stats.avg_dequeue_time_us << " µs\n";
        std::cerr << "  Max dequeue time: " << dispatcher_stats.max_dequeue_time_us << " µs\n";
        std::cerr << "========================================\n";
    }
#endif

#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
    if (show_detailed_stats) {
        auto pool_stats = buffer_pool::instance().get_stats();
        
        std::cerr << "========== BUFFER POOL METRICS ==========\n";
        std::cerr << "Pool Configuration:\n";
        std::cerr << "  Total buffers: " << pool_stats.total_buffers << "\n";
        std::cerr << "  Pool memory: " << pool_stats.pool_memory_kb << " KB\n";
        
        std::cerr << "Current Usage:\n";
        std::cerr << "  In use: " << pool_stats.in_use_buffers << "\n";
        std::cerr << "  Available: " << pool_stats.available_buffers << "\n";
        std::cerr << "  Usage percent: " << std::fixed << std::setprecision(1) << pool_stats.usage_percent << "%\n";
        std::cerr << "  High water mark: " << pool_stats.high_water_mark << "\n";
        
        std::cerr << "Lifetime Stats:\n";
        std::cerr << "  Total acquires: " << pool_stats.total_acquires << "\n";
        std::cerr << "  Total releases: " << pool_stats.total_releases << "\n";
        std::cerr << "  Acquire failures: " << pool_stats.acquire_failures << "\n";
        
        std::cerr << "Buffer Area Usage:\n";
        if (pool_stats.metadata_usage.sample_count > 0) {
            std::cerr << "  Metadata (header):\n";
            std::cerr << "    Min: " << pool_stats.metadata_usage.min_bytes << " bytes\n";
            std::cerr << "    Max: " << pool_stats.metadata_usage.max_bytes << " bytes\n";
            std::cerr << "    Avg: " << std::fixed << std::setprecision(1) 
                      << pool_stats.metadata_usage.avg_bytes << " bytes\n";
            
            std::cerr << "  Text (message):\n";
            std::cerr << "    Min: " << pool_stats.text_usage.min_bytes << " bytes\n";
            std::cerr << "    Max: " << pool_stats.text_usage.max_bytes << " bytes\n";
            std::cerr << "    Avg: " << std::fixed << std::setprecision(1) 
                      << pool_stats.text_usage.avg_bytes << " bytes\n";
            
            std::cerr << "  Total buffer usage:\n";
            std::cerr << "    Min: " << pool_stats.total_usage.min_bytes << " bytes ("
                      << std::fixed << std::setprecision(1) 
                      << (pool_stats.total_usage.min_bytes * 100.0 / LOG_BUFFER_SIZE) << "%)\n";
            std::cerr << "    Max: " << pool_stats.total_usage.max_bytes << " bytes ("
                      << std::fixed << std::setprecision(1) 
                      << (pool_stats.total_usage.max_bytes * 100.0 / LOG_BUFFER_SIZE) << "%)\n";
            std::cerr << "    Avg: " << std::fixed << std::setprecision(1) 
                      << pool_stats.total_usage.avg_bytes << " bytes ("
                      << std::fixed << std::setprecision(1) 
                      << (pool_stats.total_usage.avg_bytes * 100.0 / LOG_BUFFER_SIZE) << "%)\n";
            std::cerr << "    Samples: " << pool_stats.total_usage.sample_count << "\n";
        }
        std::cerr << "=========================================\n";
    }
#endif

#ifdef LOG_COLLECT_STRUCTURED_METRICS
    if (show_detailed_stats)
    {
        auto key_stats = structured_log_key_registry::instance().get_stats();
        std::cerr << "========== STRUCTURED LOG KEYS ==========\n";
        std::cerr << "  Total keys: " << key_stats.key_count << "\n";
        std::cerr << "  Max keys: " << key_stats.max_keys << "\n";
        std::cerr << "  Usage percent: " << std::fixed << std::setprecision(1) << key_stats.usage_percent << "%\n";
        std::cerr << "  Estimated memory: " << key_stats.estimated_memory_kb << " KB\n";
        std::cerr << "=========================================\n";
    }
#endif

    // Display rotation metrics if file rotation is being used
    if (show_detailed_stats && (sink_type == "raw" || sink_type == "writev" || sink_type == "json"))
    {
#ifdef LOG_COLLECT_ROTATION_METRICS
        auto rot_stats = rotation_metrics::instance().get_stats();
        
        std::cerr << "========== FILE ROTATION METRICS ==========\n";
        
        std::cerr << "Rotation Activity:\n";
        std::cerr << "  Total rotations: " << rot_stats.total_rotations << "\n";
        if (rot_stats.total_rotations > 0) {
            std::cerr << "  Avg rotation time: " << rot_stats.avg_rotation_time_us << " µs\n";
            std::cerr << "  Total rotation time: " << rot_stats.total_rotation_time_us << " µs\n";
        }
        
        std::cerr << "Data Loss Prevention:\n";
        std::cerr << "  Dropped records: " << rot_stats.dropped_records << "\n";
        std::cerr << "  Dropped bytes: " << rot_stats.dropped_bytes << "\n";
        
        std::cerr << "ENOSPC Emergency Cleanup:\n";
        std::cerr << "  Pending files deleted: " << rot_stats.enospc_pending_deleted << "\n";
        std::cerr << "  Compressed files deleted: " << rot_stats.enospc_gz_deleted << "\n";
        std::cerr << "  Raw log files deleted: " << rot_stats.enospc_raw_deleted << "\n";
        std::cerr << "  Total bytes freed: " << rot_stats.enospc_bytes_freed << "\n";
        
        std::cerr << "Error Conditions:\n";
        std::cerr << "  Zero-gap fallbacks: " << rot_stats.zero_gap_fallbacks << "\n";
        std::cerr << "  Compression failures: " << rot_stats.compression_failures << "\n";
        std::cerr << "  Prepare FD failures: " << rot_stats.prepare_fd_failures << "\n";
        std::cerr << "  Fsync failures: " << rot_stats.fsync_failures << "\n";
        
        std::cerr << "===========================================\n";
#endif
    }

#ifdef LOG_COLLECT_DISPATCHER_METRICS
    // Print summary line with key metrics
    std::cerr << "\nTEST: " << num_threads << " threads x " << messages_per_thread << " msgs x " << message_size
              << " bytes | sink=" << sink_type
#ifdef LOG_COLLECT_DISPATCHER_MSG_RATE
              << " | SUMMARY: msg/s=" << std::fixed << std::setprecision(0) << dispatcher_stats.messages_per_second_10s
#endif
              << " | dispatch: avg=" << std::setprecision(1) << dispatcher_stats.avg_dispatch_time_us
              << "µs max=" << dispatcher_stats.max_dispatch_time_us << "µs"
              << " | in-flight: avg=" << dispatcher_stats.avg_inflight_time_us
              << "µs max=" << dispatcher_stats.max_inflight_time_us << "µs\n";
#endif

    return 0;
}