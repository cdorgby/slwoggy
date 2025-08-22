#include <iostream>
#include <cstring>
#include <memory>
#include "slwoggy.hpp"

#include "log_filters.hpp"

using namespace slwoggy;
using namespace std::chrono_literals;

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
    [[maybe_unused]] bool show_detailed_stats = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            sink_type = argv[++i];
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0) {
#if defined(LOG_COLLECT_DISPATCHER_METRICS) || defined(LOG_COLLECT_BUFFER_POOL_METRICS) || defined(LOG_COLLECT_STRUCTURED_METRICS)
            show_detailed_stats = true;
#endif
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

    auto &dispatcher = log_line_dispatcher::instance();
    auto dedup       = std::make_shared<dedup_filter>(100ms);
    dispatcher.add_filter(dedup);

    for(int i = 0; i < 20; ++i) {
        LOG(info) << "Hello world " << " from main";
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

    // Test hex dump functionality
    uint8_t test_data[256];
    for (int i = 0; i < 256; i++) {
        test_data[i] = i;
    }
    
    // Test with duplicate data
    uint8_t dup_data[128];
    memset(dup_data, 0xAA, sizeof(dup_data));
    
    LOG(info).hex_dump_best_effort(test_data, 64, log_line_base::hex_dump_format::full);
    LOG(info).hex_dump_best_effort(dup_data, sizeof(dup_data), log_line_base::hex_dump_format::no_ascii);
    
    // Test different inline formats
    LOG(info) << "Default inline: ";
    LOG(info).hex_dump_best_effort(test_data, 16, log_line_base::hex_dump_format::inline_hex);
    
    LOG(info) << "With 0x prefix: ";
    hex_inline_config hex_0x = {"0x", "", " ", "", ""};
    LOG(info).hex_dump_best_effort(test_data, 16, log_line_base::hex_dump_format::inline_hex, 8, hex_0x);
    
    LOG(info) << "With brackets: ";
    hex_inline_config hex_brackets = {"", "", " ", "[", "]"};
    LOG(info).hex_dump_best_effort(test_data, 16, log_line_base::hex_dump_format::inline_hex, 8, hex_brackets);
    
    LOG(info) << "With dashes: ";
    hex_inline_config hex_dash = {"", "", "-", "", ""};
    LOG(info).hex_dump_best_effort(test_data, 16, log_line_base::hex_dump_format::inline_hex, 8, hex_dash);
    
    // Large 4KB test with multiple repeating patterns and randomness
    LOG(info) << "Testing large 4KB hex dump with patterns:";
    auto large_data = std::make_unique<uint8_t[]>(4096);
    
    // Fill with various patterns:
    // 0x000-0x1FF: Incremental pattern
    for (int i = 0; i < 512; i++) {
        large_data[i] = i & 0xFF;
    }
    
    // 0x200-0x3FF: Repeating 0xDE (512 bytes)
    memset(large_data.get() + 512, 0xDE, 512);
    
    // 0x400-0x5FF: Random-ish data
    for (int i = 1024; i < 1536; i++) {
        large_data[i] = (i * 31 + 17) & 0xFF;
    }
    
    // 0x600-0x7FF: Repeating 0xAD (512 bytes)
    memset(large_data.get() + 1536, 0xAD, 512);
    
    // 0x800-0x9FF: Another incremental pattern
    for (int i = 2048; i < 2560; i++) {
        large_data[i] = (i - 2048) & 0xFF;
    }
    
    // 0xA00-0xBFF: All zeros (512 bytes)
    memset(large_data.get() + 2560, 0x00, 512);
    
    // 0xC00-0xDFF: Repeating pattern AABBCCDD
    for (int i = 3072; i < 3584; i += 4) {
        large_data[i] = 0xAA;
        large_data[i+1] = 0xBB;
        large_data[i+2] = 0xCC;
        large_data[i+3] = 0xDD;
    }
    
    // 0xE00-0xFFF: More random-ish data
    for (int i = 3584; i < 4096; i++) {
        large_data[i] = ((i * 13) ^ (i >> 3)) & 0xFF;
    }
    
    // Dump the entire 4KB - this will test buffer boundaries and multiple star patterns
    LOG(info).hex_dump_full(large_data.get(), 4096, log_line_base::hex_dump_format::no_ascii);

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