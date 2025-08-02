#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
#include <iomanip>
#include "log.hpp"
#include "log_dispatcher.hpp"

using namespace slwoggy;

LOG_MODULE_NAME("main");

int main(int argc, char *argv[])
{
    // Pre-register structured log keys to avoid mutex contention during logging
    auto& key_registry = structured_log_key_registry::instance();
    key_registry.get_or_register_key(std::string_view("thread_id"));
    key_registry.get_or_register_key(std::string_view("iteration"));
    key_registry.get_or_register_key(std::string_view("hi"));
    key_registry.get_or_register_key(std::string_view("hello"));

    auto sink = make_stdout_sink();
    log_line_dispatcher::instance().add_sink(std::make_shared<log_sink>(std::move(sink)));

    // Create threads that will blast logs simultaneously
    std::mutex start_mutex;
    std::condition_variable start_cv;
    bool start_signal = false;
    std::vector<std::thread> threads;
    
    // Thread function
    auto thread_func = [&](int thread_id) {
        // Wait for start signal
        {
            std::unique_lock<std::mutex> lock(start_mutex);
            start_cv.wait(lock, [&] { return start_signal; });
        }
        
        // Blast logs
        for (int i = 0; i < 1000000; ++i)
        {
#if 0
            LOG(info).add("thread_id", thread_id)
                     .add("iteration", i)
                     .add("hi", "world")
                     .add("hello", "not world") 
                  << "Starting Data Center Operations "
                     "aaaaaaaaaaaaaaaaaaaaa "
                     "reeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\neeeeeeeeeeeeeeee"
                     "eeea"
                     "aaaaaaaaaaaaaaa lllllllllllllyyyyyyyyyyy "
                     "loooooooooooooooooooooooooooong message"
                  << endl;
#endif
            //LOG(info) << "Thread " << thread_id << " iteration " << i << endl;
            //LOG(info).format("thread {} iteration {}", thread_id, i) << endl;
            //LOG(info).add("thread_id", thread_id).add("iteration", i) << endl;
            LOG(info).printf("Thread %d iteration %d", thread_id, i);
        }
    };

    for (int i = 0; i < 4; ++i) {
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
#ifdef LOG_COLLECT_DISPATCHER_METRICS
        auto dispatcher_stats = log_line_dispatcher::instance().get_stats();
        std::cerr << "  Messages/second (1s): " << std::fixed << std::setprecision(2)
                  << dispatcher_stats.messages_per_second_1s << "\n";
#endif
        t.join();
    }
    
    // Print out metrics from dispatcher and buffer pool
#ifdef LOG_COLLECT_DISPATCHER_METRICS
    // Let logs finish processing
    auto dispatcher_stats = log_line_dispatcher::instance().get_stats();
    
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
#endif

#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
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
#endif


    return 0;
}