#include <iostream>
#include <cstring>
#include "log.hpp"
#include "log_dispatcher.hpp"
#include "log_sinks.hpp"

using namespace slwoggy;

LOG_MODULE_NAME("main");

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -w <sink>         Sink type: raw, writev, json, stdout (default: writev)\n"
              << "  -f <file>         Output file (default: /tmp/log.txt)\n"
              << "  -d                Show detailed statistics\n"
              << "  -h                Show this help\n";
}

int main(int argc, char *argv[])
{
    // Default parameters
    std::string sink_type = "raw";
    std::string output_file = "/tmp/log.txt";
    bool show_detailed_stats = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
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

    LOG(fatal).fmtprint("Starting log blast test with sink type: {}", sink_type);
    LOG(error).printf("Starting log blast test with sink type: %s", sink_type.c_str());
    LOG(warn).printfmt("blah %s", sink_type.c_str());
    LOG(info) << "Starting log blast test with sink type: " << sink_type << endl;
    LOG(debug).format("Output file: {}", output_file);
    LOG(trace).add("sink_type", sink_type).add("output_file", output_file) << "Log blast test "
                                                                              "initialized\nblah";
    LOG_STRUCTURED(trace).add("sink_type", sink_type).add("output_file", output_file) << "Log blast test "
                                                                                         "initialized\nblah";

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
                      << (pool_stats.total_usage.min_bytes * 100.0 / buffer_pool::BUFFER_SIZE) << "%)\n";
            std::cerr << "    Max: " << pool_stats.total_usage.max_bytes << " bytes ("
                      << std::fixed << std::setprecision(1) 
                      << (pool_stats.total_usage.max_bytes * 100.0 / buffer_pool::BUFFER_SIZE) << "%)\n";
            std::cerr << "    Avg: " << std::fixed << std::setprecision(1) 
                      << pool_stats.total_usage.avg_bytes << " bytes ("
                      << std::fixed << std::setprecision(1) 
                      << (pool_stats.total_usage.avg_bytes * 100.0 / buffer_pool::BUFFER_SIZE) << "%)\n";
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

#ifdef LOG_COLLECT_DISPATCHER_METRICS
    // Print summary line with key metrics
    std::cerr << "\nTEST: sink=" << sink_type
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