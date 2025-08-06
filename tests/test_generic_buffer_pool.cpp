/**
 * @file test_generic_buffer_pool.cpp
 * @brief Comprehensive tests for generic_buffer_pool
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "generic_buffer_pool.hpp"
#include "static_buffer.hpp"
#include "static_buffer_impl.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <random>

using namespace slwoggy;

TEST_CASE("generic_buffer_pool basic operations", "[generic_buffer_pool]")
{
    SECTION("pool construction and initialization")
    {
        generic_buffer_pool pool(100, 1024);
        
        REQUIRE(pool.get_buffer_count() == 100);
        REQUIRE(pool.get_buffer_size() == 1024);
        
        auto stats = pool.get_stats();
        REQUIRE(stats.total_buffers == 100);
        REQUIRE(stats.available_buffers == 100);
        REQUIRE(stats.in_use_buffers == 0);
        REQUIRE(stats.total_acquires == 0);
        REQUIRE(stats.acquire_failures == 0);
    }
    
    SECTION("acquire and return single buffer")
    {
        generic_buffer_pool pool(10, 256);
        
        char* storage = pool.acquire_storage();
        REQUIRE(storage != nullptr);
        
        auto stats = pool.get_stats();
        REQUIRE(stats.in_use_buffers == 1);
        REQUIRE(stats.available_buffers == 9);
        REQUIRE(stats.total_acquires == 1);
        
        pool.return_storage(storage);
        
        stats = pool.get_stats();
        REQUIRE(stats.in_use_buffers == 0);
        REQUIRE(stats.available_buffers == 10);
        REQUIRE(stats.total_releases == 1);
    }
    
    SECTION("acquire all buffers")
    {
        const size_t count = 50;
        generic_buffer_pool pool(count, 256);
        std::vector<char*> buffers;
        
        // Acquire all buffers
        for (size_t i = 0; i < count; ++i)
        {
            char* storage = pool.acquire_storage();
            REQUIRE(storage != nullptr);
            buffers.push_back(storage);
        }
        
        auto stats = pool.get_stats();
        REQUIRE(stats.in_use_buffers == count);
        REQUIRE(stats.available_buffers == 0);
        
        // Try to acquire one more - should fail
        char* extra = pool.acquire_storage();
        REQUIRE(extra == nullptr);
        
        stats = pool.get_stats();
        REQUIRE(stats.acquire_failures == 1);
        
        // Return all buffers
        for (char* storage : buffers)
        {
            pool.return_storage(storage);
        }
        
        stats = pool.get_stats();
        REQUIRE(stats.in_use_buffers == 0);
        REQUIRE(stats.available_buffers == count);
    }
    
    SECTION("pool exhaustion handling")
    {
        generic_buffer_pool pool(5, 256);
        std::vector<char*> buffers;
        
        // Acquire all buffers
        for (size_t i = 0; i < 5; ++i)
        {
            buffers.push_back(pool.acquire_storage());
        }
        
        // Try to acquire more - all should fail
        for (size_t i = 0; i < 10; ++i)
        {
            REQUIRE(pool.acquire_storage() == nullptr);
        }
        
        auto stats = pool.get_stats();
        REQUIRE(stats.acquire_failures == 10);
        
        // Return one buffer
        pool.return_storage(buffers[0]);
        
        // Now we should be able to acquire one
        char* new_buffer = pool.acquire_storage();
        REQUIRE(new_buffer != nullptr);
    }
    
    SECTION("high water mark tracking")
    {
        generic_buffer_pool pool(100, 256);
        std::vector<char*> buffers;
        
        // Acquire 50 buffers
        for (size_t i = 0; i < 50; ++i)
        {
            buffers.push_back(pool.acquire_storage());
        }
        
        auto stats = pool.get_stats();
        REQUIRE(stats.high_water_mark == 50);
        
        // Return 25 buffers
        for (size_t i = 0; i < 25; ++i)
        {
            pool.return_storage(buffers[i]);
        }
        buffers.erase(buffers.begin(), buffers.begin() + 25);
        
        // High water mark should still be 50
        stats = pool.get_stats();
        REQUIRE(stats.high_water_mark == 50);
        
        // Acquire 40 more (total will be 65)
        for (size_t i = 0; i < 40; ++i)
        {
            buffers.push_back(pool.acquire_storage());
        }
        
        stats = pool.get_stats();
        REQUIRE(stats.high_water_mark == 65);
    }
    
    SECTION("buffer storage alignment")
    {
        generic_buffer_pool pool(10, 2048);
        std::vector<char*> buffers;
        
        // Acquire several buffers
        for (size_t i = 0; i < 5; ++i)
        {
            buffers.push_back(pool.acquire_storage());
        }
        
        // Check that buffers are properly spaced
        for (size_t i = 1; i < buffers.size(); ++i)
        {
            ptrdiff_t diff = buffers[i] - buffers[i-1];
            REQUIRE(std::abs(diff) == 2048);
        }
    }
}

TEST_CASE("generic_buffer_pool concurrent operations", "[generic_buffer_pool][concurrent]")
{
    SECTION("concurrent acquire and release")
    {
        const size_t num_threads = 8;
        const size_t ops_per_thread = 1000;
        generic_buffer_pool pool(100, 256);
        
        std::atomic<size_t> successful_acquires{0};
        std::atomic<size_t> failed_acquires{0};
        
        auto worker = [&]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> hold_time(0, 10);
            
            for (size_t i = 0; i < ops_per_thread; ++i)
            {
                char* storage = pool.acquire_storage();
                if (storage)
                {
                    successful_acquires.fetch_add(1);
                    
                    // Hold buffer for random time
                    std::this_thread::sleep_for(std::chrono::microseconds(hold_time(gen)));
                    
                    pool.return_storage(storage);
                }
                else
                {
                    failed_acquires.fetch_add(1);
                }
            }
        };
        
        std::vector<std::thread> threads;
        for (size_t i = 0; i < num_threads; ++i)
        {
            threads.emplace_back(worker);
        }
        
        for (auto& t : threads)
        {
            t.join();
        }
        
        auto stats = pool.get_stats();
        REQUIRE(stats.total_acquires == successful_acquires.load());
        REQUIRE(stats.acquire_failures == failed_acquires.load());
        REQUIRE(stats.total_releases == successful_acquires.load());
        REQUIRE(stats.in_use_buffers == 0);  // All returned
    }
    
    SECTION("stress test with contention")
    {
        const size_t num_threads = 16;
        const size_t ops_per_thread = 5000;
        generic_buffer_pool pool(50, 512);  // Limited pool to create contention
        
        std::atomic<bool> start{false};
        std::atomic<size_t> total_ops{0};
        
        auto worker = [&]() {
            // Wait for start signal
            while (!start.load()) 
            {
                std::this_thread::yield();
            }
            
            for (size_t i = 0; i < ops_per_thread; ++i)
            {
                char* storage = pool.acquire_storage();
                if (storage)
                {
                    // Do some work with the buffer
                    storage[0] = 'A';
                    storage[pool.get_buffer_size() - 1] = 'Z';
                    
                    total_ops.fetch_add(1);
                    pool.return_storage(storage);
                }
                
                // Small delay to allow other threads
                if (i % 100 == 0)
                {
                    std::this_thread::yield();
                }
            }
        };
        
        std::vector<std::thread> threads;
        for (size_t i = 0; i < num_threads; ++i)
        {
            threads.emplace_back(worker);
        }
        
        // Start all threads simultaneously
        start.store(true);
        
        for (auto& t : threads)
        {
            t.join();
        }
        
        auto stats = pool.get_stats();
        REQUIRE(stats.in_use_buffers == 0);
        REQUIRE(stats.total_releases == stats.total_acquires);
        REQUIRE(total_ops.load() > 0);
    }
}

TEST_CASE("generic_buffer_pool statistics", "[generic_buffer_pool]")
{
    SECTION("statistics accuracy")
    {
        generic_buffer_pool pool(15, 512);
        
        // Perform known operations
        std::vector<char*> buffers;
        for (size_t i = 0; i < 15; ++i)
        {
            buffers.push_back(pool.acquire_storage());
        }
        
        for (size_t i = 0; i < 5; ++i)
        {
            pool.return_storage(buffers[i]);
        }
        
        // Try to acquire beyond capacity
        for (size_t i = 0; i < 10; ++i)
        {
            char* s = pool.acquire_storage();
            if (s) buffers.push_back(s);
        }
        
        auto stats = pool.get_stats();
        REQUIRE(stats.total_buffers == 15);
        REQUIRE(stats.in_use_buffers == 15);  // 15 original - 5 returned + 5 reacquired
        REQUIRE(stats.available_buffers == 0);
        REQUIRE(stats.total_acquires == 20);  // 15 + 5 successful reacquires
        REQUIRE(stats.acquire_failures == 5);  // Last 5 attempts failed
        REQUIRE(stats.total_releases == 5);
        REQUIRE(stats.high_water_mark == 15);
        
        // Clean up
        for (char* s : buffers)
        {
            if (s) pool.return_storage(s);
        }
    }
    
    SECTION("reset statistics")
    {
        generic_buffer_pool pool(10, 256);
        
        // Perform some operations
        char* s1 = pool.acquire_storage();
        char* s2 = pool.acquire_storage();
        pool.return_storage(s1);
        
        auto stats = pool.get_stats();
        REQUIRE(stats.total_acquires == 2);
        REQUIRE(stats.total_releases == 1);
        
        // Reset statistics
        pool.reset_stats();
        
        stats = pool.get_stats();
        REQUIRE(stats.total_acquires == 0);
        REQUIRE(stats.total_releases == 0);
        REQUIRE(stats.acquire_failures == 0);
        REQUIRE(stats.high_water_mark == 1);  // Current in-use count
        
        pool.return_storage(s2);
    }
}

TEST_CASE("generic_buffer_pool performance", "[generic_buffer_pool][benchmark]")
{
    BENCHMARK("acquire/release single-threaded")
    {
        generic_buffer_pool pool(1000, 2048);
        
        char* storage = pool.acquire_storage();
        pool.return_storage(storage);
        
        return storage;
    };
    
    BENCHMARK("acquire/release with contention")
    {
        static generic_buffer_pool pool(100, 2048);
        
        char* storage = pool.acquire_storage();
        if (storage)
        {
            pool.return_storage(storage);
        }
        
        return storage;
    };
}

TEST_CASE("generic_buffer_pool memory safety", "[generic_buffer_pool]")
{
    SECTION("buffer isolation")
    {
        generic_buffer_pool pool(10, 256);
        
        // Acquire two buffers
        char* buffer1 = pool.acquire_storage();
        char* buffer2 = pool.acquire_storage();
        
        REQUIRE(buffer1 != buffer2);
        ptrdiff_t diff = buffer2 - buffer1;
        REQUIRE(std::abs(diff) >= 256);
        
        // Write to buffers
        std::memset(buffer1, 'A', 256);
        std::memset(buffer2, 'B', 256);
        
        // Check no overlap
        REQUIRE(buffer1[0] == 'A');
        REQUIRE(buffer1[255] == 'A');
        REQUIRE(buffer2[0] == 'B');
        REQUIRE(buffer2[255] == 'B');
        
        pool.return_storage(buffer1);
        pool.return_storage(buffer2);
    }
    
    SECTION("reuse after return")
    {
        generic_buffer_pool pool(5, 256);
        
        // Acquire and mark buffer
        char* buffer1 = pool.acquire_storage();
        buffer1[0] = 'X';
        buffer1[255] = 'Y';
        
        // Return buffer
        pool.return_storage(buffer1);
        
        // Acquire again - might get same buffer
        char* buffer2 = pool.acquire_storage();
        
        // Buffer should be reusable (content may or may not be cleared)
        buffer2[0] = 'A';
        buffer2[255] = 'B';
        
        pool.return_storage(buffer2);
    }
}