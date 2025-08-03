#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cstring>
#include "log.hpp"
#include "log_dispatcher.hpp"
#include "log_sinks.hpp"

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
    std::string sink_type = "json";
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

    // Create sink based on type
    if (sink_type == "raw") {
        log_line_dispatcher::instance().add_sink(make_raw_file_sink(output_file));
    } else if (sink_type == "writev") {
        log_line_dispatcher::instance().add_sink(make_writev_file_sink(output_file));
    } else if (sink_type == "json") {
        log_line_dispatcher::instance().add_sink(make_json_sink(output_file));
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
    
    // Create padding string for message size
    std::string padding;
    if (message_size > 30) {
        padding = std::string(message_size - 30, 'X');
    }

    // Thread function
    auto thread_func = [&](int thread_id) {
        // Wait for start signal
        {
            std::unique_lock<std::mutex> lock(start_mutex);
            start_cv.wait(lock, [&] { return start_signal; });
        }

        // Blast logs
        for (int i = 0; i < messages_per_thread; ++i)
        {
            if (padding.empty())
            {
                //LOG(info).add("i", i).add("tid", thread_id).printf("Thread %d iteration %d", thread_id, i);
                LOG(info).printf("Thread %d iteration %d", thread_id, i);
            }
            else
            {
                //LOG(info).add("tid", thread_id).add("i", i).printf("Thread %d iteration %d %s", thread_id, i, padding.c_str());
                LOG(info).printf("Thread %d iteration %d %s", thread_id, i, padding.c_str());
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
    // Let logs finish processing
    auto dispatcher_stats = log_line_dispatcher::instance().get_stats();

    if (sink_type == "stdout")
    {
        // For file sinks, we need to flush the dispatcher
        log_line_dispatcher::instance().flush();
    }

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
#else
        std::cerr << "  Messages/second: " << std::fixed << std::setprecision(2) << dispatcher_stats.messages_per_second << "\n";
#endif
        std::cerr << "  Messages dropped: " << dispatcher_stats.messages_dropped << "\n";
        
        std::cerr << "Batch Processing:\n";
        std::cerr << "  Total batches: " << dispatcher_stats.total_batches << "\n";
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
        std::cerr << "=========================================\n";
    }
#endif

#ifdef LOG_COLLECT_DISPATCHER_METRICS
    // Print summary line with key metrics
    std::cerr << "\nTEST: " << num_threads << " threads x " << messages_per_thread 
              << " msgs x " << message_size << " bytes | sink=" << sink_type
              << " | SUMMARY: msg/s=" << std::fixed << std::setprecision(0) << dispatcher_stats.messages_per_second_10s
              << " | dispatch: avg=" << std::setprecision(1) << dispatcher_stats.avg_dispatch_time_us 
              << "µs max=" << dispatcher_stats.max_dispatch_time_us << "µs"
              << " | in-flight: avg=" << dispatcher_stats.avg_inflight_time_us 
              << "µs max=" << dispatcher_stats.max_inflight_time_us << "µs\n";
#endif

    return 0;
}