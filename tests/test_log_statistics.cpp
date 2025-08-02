#include <catch2/catch_test_macros.hpp>
#include "log.hpp"
#include "log_buffer.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>

using namespace slwoggy;

// Test fixture for statistics testing
class StatsTestFixture {
public:
    StatsTestFixture() {
        // Remove all sinks to avoid interference
        // Remove sinks from slots 0 and 1 (common slots)
        log_line_dispatcher::instance().set_sink(0, nullptr);
        log_line_dispatcher::instance().set_sink(1, nullptr);
        
        // Ensure clean state before resetting stats
        log_line_dispatcher::instance().flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Reset all statistics at start of test
#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
        buffer_pool::instance().reset_stats();
#endif
#ifdef LOG_COLLECT_DISPATCHER_METRICS
        log_line_dispatcher::instance().reset_stats();
#endif
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        log_buffer_metadata_adapter::reset_drop_stats();
#endif
    }
    
    ~StatsTestFixture() {
        // Clean up after test
        log_line_dispatcher::instance().flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

#ifdef LOG_COLLECT_BUFFER_POOL_METRICS
TEST_CASE("Buffer pool statistics", "[statistics]") {
    StatsTestFixture fixture;
    
    SECTION("Basic metrics collection") {
        auto initial_stats = buffer_pool::instance().get_stats();
        
        // Verify initial state
        REQUIRE(initial_stats.total_buffers == BUFFER_POOL_SIZE);
        REQUIRE(initial_stats.available_buffers <= BUFFER_POOL_SIZE);
        REQUIRE(initial_stats.in_use_buffers == BUFFER_POOL_SIZE - initial_stats.available_buffers);
        REQUIRE(initial_stats.total_acquires >= 0);
        REQUIRE(initial_stats.acquire_failures >= 0);
        REQUIRE(initial_stats.total_releases >= 0);
        REQUIRE(initial_stats.usage_percent >= 0.0f);
        REQUIRE(initial_stats.usage_percent <= 100.0f);
        REQUIRE(initial_stats.pool_memory_kb > 0);
        REQUIRE(initial_stats.high_water_mark >= initial_stats.in_use_buffers);
        
        // Acquire 5 buffers
        std::vector<log_buffer*> buffers;
        const size_t acquire_count = 5;
        
        for (size_t i = 0; i < acquire_count; ++i) {
            auto* buffer = buffer_pool::instance().acquire();
            REQUIRE(buffer != nullptr);
            buffers.push_back(buffer);
        }
        
        auto after_acquire = buffer_pool::instance().get_stats();
        REQUIRE(after_acquire.total_acquires >= initial_stats.total_acquires + acquire_count);
        REQUIRE(after_acquire.in_use_buffers >= initial_stats.in_use_buffers + acquire_count);
        REQUIRE(after_acquire.available_buffers == BUFFER_POOL_SIZE - after_acquire.in_use_buffers);
        
        // Verify usage percent calculation
        float expected_usage = (after_acquire.in_use_buffers * 100.0f) / BUFFER_POOL_SIZE;
        REQUIRE(std::abs(after_acquire.usage_percent - expected_usage) < 0.1f);
        
        // Release buffers
        for (auto* buffer : buffers) {
            buffer->release();
        }
        
        auto after_release = buffer_pool::instance().get_stats();
        REQUIRE(after_release.total_releases >= initial_stats.total_releases + acquire_count);
        REQUIRE(after_release.in_use_buffers <= initial_stats.in_use_buffers);
    }
    
    SECTION("High water mark tracking") {
        auto initial_stats = buffer_pool::instance().get_stats();
        uint64_t initial_high_water = initial_stats.high_water_mark;
        
        // Acquire 50% of pool
        std::vector<log_buffer*> buffers;
        const size_t target_count = BUFFER_POOL_SIZE / 2;
        
        for (size_t i = 0; i < target_count; ++i) {
            auto* buffer = buffer_pool::instance().acquire();
            if (buffer) {
                buffers.push_back(buffer);
            }
        }
        
        auto stats = buffer_pool::instance().get_stats();
        REQUIRE(stats.high_water_mark >= buffers.size());
        REQUIRE(stats.high_water_mark >= initial_high_water);
        
        uint64_t peak_high_water = stats.high_water_mark;
        
        // Release all buffers
        for (auto* buffer : buffers) {
            buffer->release();
        }
        
        // High water mark should persist
        auto final_stats = buffer_pool::instance().get_stats();
        REQUIRE(final_stats.high_water_mark == peak_high_water);
    }
    
    SECTION("Pool exhaustion") {
        std::vector<log_buffer*> buffers;
        auto initial_stats = buffer_pool::instance().get_stats();
        
        // Try to acquire more than pool size
        for (size_t i = 0; i < BUFFER_POOL_SIZE + 10; ++i) {
            auto* buffer = buffer_pool::instance().acquire();
            if (buffer) {
                buffers.push_back(buffer);
            }
        }
        
        auto stats = buffer_pool::instance().get_stats();
        REQUIRE(stats.acquire_failures > initial_stats.acquire_failures);
        REQUIRE(buffers.size() <= BUFFER_POOL_SIZE);
        
        // Verify we got close to pool size (allowing for dispatcher usage)
        REQUIRE(buffers.size() >= BUFFER_POOL_SIZE - 10);
        
        // Clean up
        for (auto* buffer : buffers) {
            buffer->release();
        }
    }
    
    SECTION("Statistics reset") {
        // Generate some activity
        auto* buffer1 = buffer_pool::instance().acquire();
        REQUIRE(buffer1 != nullptr);
        auto* buffer2 = buffer_pool::instance().acquire();
        REQUIRE(buffer2 != nullptr);
        buffer1->release();
        buffer2->release();
        
        auto stats_before = buffer_pool::instance().get_stats();
        REQUIRE(stats_before.total_acquires >= 2);
        REQUIRE(stats_before.total_releases >= 2);
        
        // Reset statistics
        buffer_pool::instance().reset_stats();
        
        auto stats_after = buffer_pool::instance().get_stats();
        REQUIRE(stats_after.total_acquires == 0);
        REQUIRE(stats_after.total_releases == 0);
        REQUIRE(stats_after.acquire_failures == 0);
        // Note: buffers_in_use and high_water_mark are NOT reset as they reflect current state
    }
}
#endif // LOG_COLLECT_BUFFER_POOL_METRICS

#ifdef LOG_COLLECT_DISPATCHER_METRICS
TEST_CASE("Dispatcher statistics", "[statistics]") {
    StatsTestFixture fixture;
    
    SECTION("Basic dispatch metrics") {
        auto initial_stats = log_line_dispatcher::instance().get_stats();
        
        // Verify initial state
        REQUIRE(initial_stats.total_dispatched >= 0);
        REQUIRE(initial_stats.queue_enqueue_failures >= 0);
        REQUIRE(initial_stats.current_queue_size >= 0);
        REQUIRE(initial_stats.max_queue_size >= 0);
        REQUIRE(initial_stats.total_flushes >= 0);
        REQUIRE(initial_stats.messages_dropped >= 0);
        REQUIRE(initial_stats.queue_usage_percent >= 0.0f);
        REQUIRE(initial_stats.worker_iterations >= 0);
        REQUIRE(initial_stats.active_sinks >= 0);
        REQUIRE(initial_stats.avg_dispatch_time_us >= 0);
        
        // Log 10 messages
        const size_t log_count = 10;
        for (size_t i = 0; i < log_count; ++i) {
            LOG(info) << "Test message " << i << endl;
        }
        
        // Flush and wait for processing
        log_line_dispatcher::instance().flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        auto final_stats = log_line_dispatcher::instance().get_stats();
        REQUIRE(final_stats.total_dispatched >= initial_stats.total_dispatched + log_count);
        REQUIRE(final_stats.messages_dropped == initial_stats.messages_dropped); // No drops expected
        REQUIRE(final_stats.queue_enqueue_failures == initial_stats.queue_enqueue_failures); // No failures expected
        REQUIRE(final_stats.worker_iterations > initial_stats.worker_iterations);
    }
    
    SECTION("Queue max size tracking") {
        auto initial_stats = log_line_dispatcher::instance().get_stats();
        
        // Log a burst of messages
        const size_t burst_size = 100;
        for (size_t i = 0; i < burst_size; ++i) {
            LOG(info) << "Burst message " << i << endl;
        }
        
        // The max queue size should have increased (even if current size is 0)
        auto stats = log_line_dispatcher::instance().get_stats();
        REQUIRE(stats.max_queue_size >= initial_stats.max_queue_size);
        
        // After flush, current queue should be empty
        log_line_dispatcher::instance().flush();
        auto final_stats = log_line_dispatcher::instance().get_stats();
        REQUIRE(final_stats.current_queue_size == 0);
    }
    
    SECTION("Flush counter") {
        auto initial_stats = log_line_dispatcher::instance().get_stats();
        
        // Perform 5 explicit flushes
        const size_t flush_count = 5;
        for (size_t i = 0; i < flush_count; ++i) {
            LOG(info) << "Message " << i;
            log_line_dispatcher::instance().flush();
        }
        
        auto stats = log_line_dispatcher::instance().get_stats();
        REQUIRE(stats.total_flushes >= initial_stats.total_flushes + flush_count);
    }
    
    SECTION("Worker thread activity") {
        auto initial_stats = log_line_dispatcher::instance().get_stats();
        
        // Log some messages
        for (size_t i = 0; i < 20; ++i) {
            LOG(info) << "Message " << i << endl;
        }
        
        log_line_dispatcher::instance().flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        auto stats = log_line_dispatcher::instance().get_stats();
        // Worker thread should have processed some iterations
        REQUIRE(stats.worker_iterations > initial_stats.worker_iterations);
        
        // If we have sinks, dispatch time might be > 0, but without sinks it could be 0
        // So we just check it's non-negative
        REQUIRE(stats.avg_dispatch_time_us >= 0);
        
        // Verify uptime is reasonable
        auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(stats.uptime).count();
        REQUIRE(uptime_ms > 0);
    }
    
    SECTION("Statistics reset") {
        // Generate activity
        LOG(info) << "Before reset 1" << endl;
        LOG(info) << "Before reset 2" << endl;
        log_line_dispatcher::instance().flush();
        
        auto stats_before = log_line_dispatcher::instance().get_stats();
        REQUIRE(stats_before.total_dispatched > 0);
        REQUIRE(stats_before.total_flushes > 0);
        REQUIRE(stats_before.worker_iterations > 0);
        
        // Reset
        log_line_dispatcher::instance().reset_stats();
        
        auto stats_after = log_line_dispatcher::instance().get_stats();
        REQUIRE(stats_after.total_dispatched == 0);
        REQUIRE(stats_after.queue_enqueue_failures == 0);
        REQUIRE(stats_after.messages_dropped == 0);
        REQUIRE(stats_after.worker_iterations == 0);
        REQUIRE(stats_after.avg_dispatch_time_us == 0);
        REQUIRE(stats_after.max_queue_size == 0);
        REQUIRE(stats_after.total_flushes == 0);
    }
}
#endif // LOG_COLLECT_DISPATCHER_METRICS

#ifdef LOG_COLLECT_STRUCTURED_METRICS
TEST_CASE("Structured logging drop statistics", "[statistics]") {
    StatsTestFixture fixture;
    
    SECTION("Metadata drop tracking") {
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        auto initial_drops = log_buffer_metadata_adapter::get_drop_stats();
#else
        auto initial_drops = std::make_pair(0ULL, 0ULL);
#endif
        
        // Create a buffer and try to add too much metadata
        auto* buffer = buffer_pool::instance().acquire();
        REQUIRE(buffer != nullptr);
        
        auto metadata = buffer->get_metadata_adapter();
        metadata.reset();
        
        // Try to add a value that exceeds METADATA_RESERVE (256 bytes)
        std::string large_value(300, 'x');
        uint16_t key_id = structured_log_key_registry::instance().get_or_register_key(std::string_view("large_key"));
        bool added = metadata.add_kv(key_id, large_value);
        REQUIRE(added == false);
        
        buffer->release();
        
        // Check that drops were recorded
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        auto final_drops = log_buffer_metadata_adapter::get_drop_stats();
#else
        auto final_drops = std::make_pair(0ULL, 0ULL);
#endif
        REQUIRE(final_drops.first > initial_drops.first); // drop count increased
        REQUIRE(final_drops.second > initial_drops.second); // dropped bytes increased
    }
    
    SECTION("Multiple drops") {
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        auto initial_drops = log_buffer_metadata_adapter::get_drop_stats();
#else
        auto initial_drops = std::make_pair(0ULL, 0ULL);
#endif
        
        auto* buffer = buffer_pool::instance().acquire();
        REQUIRE(buffer != nullptr);
        
        auto metadata = buffer->get_metadata_adapter();
        metadata.reset();
        
        // Try to add several oversized entries
        std::string large_value(300, 'y');
        uint16_t key1 = structured_log_key_registry::instance().get_or_register_key(std::string_view("key1"));
        uint16_t key2 = structured_log_key_registry::instance().get_or_register_key(std::string_view("key2"));
        uint16_t key3 = structured_log_key_registry::instance().get_or_register_key(std::string_view("key3"));
        
        REQUIRE(metadata.add_kv(key1, large_value) == false);
        REQUIRE(metadata.add_kv(key2, large_value) == false);
        REQUIRE(metadata.add_kv(key3, large_value) == false);
        
        buffer->release();
        
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        auto final_drops = log_buffer_metadata_adapter::get_drop_stats();
#else
        auto final_drops = std::make_pair(0ULL, 0ULL);
#endif
        REQUIRE(final_drops.first >= initial_drops.first + 3);
        REQUIRE(final_drops.second >= initial_drops.second + (3 * large_value.size()));
    }
    
    SECTION("Reset drop statistics") {
        // Generate some drops
        auto* buffer = buffer_pool::instance().acquire();
        REQUIRE(buffer != nullptr);
        
        auto metadata = buffer->get_metadata_adapter();
        std::string large_value(300, 'z');
        uint16_t key = structured_log_key_registry::instance().get_or_register_key(std::string_view("drop_key"));
        metadata.add_kv(key, large_value);
        
        buffer->release();
        
        // Verify we have drops
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        auto drops_before = log_buffer_metadata_adapter::get_drop_stats();
#else
        auto drops_before = std::make_pair(0ULL, 0ULL);
#endif
        REQUIRE(drops_before.first > 0);
        REQUIRE(drops_before.second > 0);
        
        // Reset
        log_buffer_metadata_adapter::reset_drop_stats();
        
        // Verify reset
#ifdef LOG_COLLECT_STRUCTURED_METRICS
        auto drops_after = log_buffer_metadata_adapter::get_drop_stats();
#else
        auto drops_after = std::make_pair(0ULL, 0ULL);
#endif
        REQUIRE(drops_after.first == 0);
        REQUIRE(drops_after.second == 0);
    }
}
#endif // LOG_COLLECT_STRUCTURED_METRICS

TEST_CASE("Key registry statistics", "[statistics]") {
    SECTION("Registry stats") {
        auto& registry = structured_log_key_registry::instance();
        auto initial_stats = registry.get_stats();
        
        // Check basic fields
        REQUIRE(initial_stats.key_count >= 0);
        REQUIRE(initial_stats.max_keys == MAX_STRUCTURED_KEYS);
        REQUIRE(initial_stats.usage_percent >= 0.0f);
        REQUIRE(initial_stats.usage_percent <= 100.0f);
        REQUIRE(initial_stats.estimated_memory_kb >= 0);
        
        // Calculate expected usage percent
        float expected_usage = (initial_stats.key_count * 100.0f) / initial_stats.max_keys;
        REQUIRE(std::abs(initial_stats.usage_percent - expected_usage) < 0.1f);
        
        // Register 3 new unique keys
        size_t initial_count = initial_stats.key_count;
        registry.get_or_register_key(std::string_view("test_stats_key_1"));
        registry.get_or_register_key(std::string_view("test_stats_key_2"));
        registry.get_or_register_key(std::string_view("test_stats_key_3"));
        
        auto new_stats = registry.get_stats();
        REQUIRE(new_stats.key_count >= initial_count + 3);
        REQUIRE(new_stats.usage_percent > initial_stats.usage_percent);
        REQUIRE(new_stats.estimated_memory_kb >= initial_stats.estimated_memory_kb);
    }
}

TEST_CASE("Compilation without metrics", "[statistics]") {
    // This test verifies that code compiles and runs correctly
    // even when metrics collection macros are not defined
    LOG(info) << "Test message without metrics" << endl;
    log_line_dispatcher::instance().flush();
    
    // If we get here without compilation errors, the test passes
    REQUIRE(true);
}