/**
 * @file test_log_filters.cpp
 * @brief Test suite for log filter functionality
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <catch2/catch_test_macros.hpp>

#include "log.hpp"
#include "log_filters.hpp"
#include "log_sink.hpp"

#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>

using namespace slwoggy;
using namespace std::chrono_literals;

// Test sink to capture messages
class test_filter_sink
{
private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    mutable std::vector<std::string> messages_;
    
    // Inner formatter that captures messages
    class capturing_formatter {
        test_filter_sink* parent_;
    public:
        explicit capturing_formatter(test_filter_sink* parent) : parent_(parent) {}
        
        size_t calculate_size(const log_buffer_base* buffer) const {
            if (!buffer || buffer->is_flush_marker()) return 0;
            return buffer->get_message().size();
        }
        
        size_t format(const log_buffer_base* buffer, char* output, size_t max_size) const {
            if (!buffer || buffer->is_flush_marker()) return 0;
            
            auto msg = buffer->get_message();
            size_t to_write = std::min(msg.size(), max_size);
            if (to_write > 0) {
                std::memcpy(output, msg.data(), to_write);
            }
            
            // Store the message
            {
                std::lock_guard<std::mutex> lock(parent_->mutex_);
                parent_->messages_.emplace_back(msg);
                parent_->cv_.notify_all();
            }
            
            return to_write;
        }
    };
    
    // Null writer that discards output
    class null_writer {
    public:
        void write(const char*, size_t) const {}
    };
    
    std::shared_ptr<log_sink> sink_;
    
public:
    test_filter_sink() {
        sink_ = std::make_shared<log_sink>(capturing_formatter{this}, null_writer{});
    }
    
    std::shared_ptr<log_sink> get_sink() const { return sink_; }
    
    size_t get_message_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_.size();
    }
    
    std::vector<std::string> get_messages() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return messages_;
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        messages_.clear();
    }
    
    bool wait_for_messages(size_t count, std::chrono::milliseconds timeout = 1000ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this, count] {
            return messages_.size() >= count;
        });
    }
};

TEST_CASE("Filter infrastructure", "[filters]")
{
    SECTION("Add and remove filters")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        
        // Start with no filters
        REQUIRE(dispatcher.filter_count() == 0);
        
        // Add a filter
        auto filter1 = std::make_shared<test_filter>(false);
        dispatcher.add_filter(filter1);
        REQUIRE(dispatcher.filter_count() == 1);
        
        // Add another filter
        auto filter2 = std::make_shared<test_filter>(false);
        dispatcher.add_filter(filter2);
        REQUIRE(dispatcher.filter_count() == 2);
        
        // Remove first filter
        dispatcher.remove_filter(0);
        REQUIRE(dispatcher.filter_count() == 1);
        
        // Clear all filters
        dispatcher.clear_filters();
        REQUIRE(dispatcher.filter_count() == 0);
    }
    
    SECTION("Filter processing - pass all")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add pass-all filter
        auto filter = std::make_shared<test_filter>(false);
        dispatcher.add_filter(filter);
        
        // Send messages
        for (int i = 0; i < 5; ++i)
        {
            LOG(info) << "Message " << i << endl;
        }
        
        dispatcher.flush();
        
        // All messages should pass through
        REQUIRE(sink.get_message_count() == 5);
        REQUIRE(filter->get_processed_count() == 5);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
    
    SECTION("Filter processing - drop all")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add drop-all filter
        auto filter = std::make_shared<test_filter>(true);
        dispatcher.add_filter(filter);
        
        // Send messages
        for (int i = 0; i < 5; ++i)
        {
            LOG(info) << "Message " << i << endl;
        }
        
        dispatcher.flush();
        
        // No messages should pass through
        REQUIRE(sink.get_message_count() == 0);
        REQUIRE(filter->get_processed_count() == 5);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
    
    SECTION("Multiple filters - chain processing")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add two filters - first passes, second drops
        auto filter1 = std::make_shared<test_filter>(false);
        auto filter2 = std::make_shared<test_filter>(true);
        dispatcher.add_filter(filter1);
        dispatcher.add_filter(filter2);
        
        // Send messages
        for (int i = 0; i < 5; ++i)
        {
            LOG(info) << "Message " << i << endl;
        }
        
        dispatcher.flush();
        
        // No messages should pass (second filter drops all)
        REQUIRE(sink.get_message_count() == 0);
        REQUIRE(filter1->get_processed_count() == 5);
        REQUIRE(filter2->get_processed_count() == 5);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
}

TEST_CASE("Deduplication filter", "[filters][dedup]")
{
    SECTION("Basic deduplication")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add dedup filter with 100ms window
        auto filter = std::make_shared<dedup_filter>(100ms);
        dispatcher.add_filter(filter);
        
        // Send duplicate messages
        LOG(info) << "Duplicate message" << endl;
        LOG(info) << "Duplicate message" << endl;
        LOG(info) << "Duplicate message" << endl;
        LOG(info) << "Different message" << endl;
        LOG(info) << "Different message" << endl;
        
        dispatcher.flush();
        
        // Should see only first of each duplicate
        REQUIRE(sink.get_message_count() == 2);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
    
    SECTION("Deduplication window expiry")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add dedup filter with 50ms window
        auto filter = std::make_shared<dedup_filter>(50ms);
        dispatcher.add_filter(filter);
        
        // Send message, wait for window to expire, send again
        LOG(info) << "Test message" << endl;
        dispatcher.flush();
        REQUIRE(sink.get_message_count() == 1);
        
        // Wait for dedup window to expire
        std::this_thread::sleep_for(60ms);
        
        // Same message should pass now
        LOG(info) << "Test message" << endl;
        dispatcher.flush();
        REQUIRE(sink.get_message_count() == 2);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
}

TEST_CASE("Sampler filter", "[filters][sampler]")
{
    SECTION("Statistical sampling")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add sampler filter with 50% rate
        auto filter = std::make_shared<sampler_filter>(0.5);
        dispatcher.add_filter(filter);
        
        // Send many messages
        const int message_count = 1000;
        for (int i = 0; i < message_count; ++i)
        {
            LOG(info) << "Message " << i << endl;
        }
        
        dispatcher.flush();
        
        // Should see approximately 50% of messages (with some variance)
        auto passed = sink.get_message_count();
        REQUIRE(passed > message_count * 0.4);
        REQUIRE(passed < message_count * 0.6);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
    
    SECTION("Boundary rates")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Test 0% sampling (drop all)
        auto filter_0 = std::make_shared<sampler_filter>(0.0);
        dispatcher.add_filter(filter_0);
        
        for (int i = 0; i < 10; ++i)
        {
            LOG(info) << "Message " << i << endl;
        }
        dispatcher.flush();
        REQUIRE(sink.get_message_count() == 0);
        
        // Test 100% sampling (pass all)
        dispatcher.clear_filters();
        sink.reset();
        
        auto filter_100 = std::make_shared<sampler_filter>(1.0);
        dispatcher.add_filter(filter_100);
        
        for (int i = 0; i < 10; ++i)
        {
            LOG(info) << "Message " << i << endl;
        }
        dispatcher.flush();
        REQUIRE(sink.get_message_count() == 10);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
}

TEST_CASE("Rate limit filter", "[filters][rate_limit]")
{
    SECTION("Basic rate limiting")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add rate limit filter - 10 messages per second
        auto filter = std::make_shared<rate_limit_filter>(10);
        dispatcher.add_filter(filter);
        
        // Send burst of messages
        for (int i = 0; i < 20; ++i)
        {
            LOG(info) << "Message " << i << endl;
        }
        
        dispatcher.flush();
        
        // Should see at most 10 messages (token bucket starts full)
        REQUIRE(sink.get_message_count() == 10);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
    
    SECTION("Token refill over time")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add rate limit filter - 100 messages per second
        auto filter = std::make_shared<rate_limit_filter>(100);
        dispatcher.add_filter(filter);
        
        // Send burst to exhaust tokens
        for (int i = 0; i < 100; ++i)
        {
            LOG(info) << "Burst " << i << endl;
        }
        dispatcher.flush();
        REQUIRE(sink.get_message_count() == 100);
        
        // Wait for tokens to refill (100ms = 10 tokens)
        std::this_thread::sleep_for(100ms);
        
        // Should be able to send more messages now
        sink.reset();
        for (int i = 0; i < 15; ++i)
        {
            LOG(info) << "After wait " << i << endl;
        }
        dispatcher.flush();
        
        // Should see approximately 10 messages (tokens refilled)
        auto count = sink.get_message_count();
        REQUIRE(count >= 9);  // Allow some timing variance
        REQUIRE(count <= 11);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
}

TEST_CASE("Filter combinations", "[filters][combination]")
{
    SECTION("Dedup + Rate limit")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add both filters
        auto dedup = std::make_shared<dedup_filter>(100ms);
        auto rate_limit = std::make_shared<rate_limit_filter>(5);
        dispatcher.add_filter(dedup);
        dispatcher.add_filter(rate_limit);
        
        // Send duplicate messages in burst
        for (int i = 0; i < 10; ++i)
        {
            LOG(info) << "Message A" << endl;
            LOG(info) << "Message B" << endl;
        }
        
        dispatcher.flush();
        
        // Dedup reduces to 2 unique messages, rate limit allows both
        REQUIRE(sink.get_message_count() == 2);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
    
    SECTION("All filter types")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add all filter types
        auto dedup = std::make_shared<dedup_filter>(100ms);
        auto sampler = std::make_shared<sampler_filter>(0.8);  // 80% pass rate
        auto rate_limit = std::make_shared<rate_limit_filter>(100);
        
        dispatcher.add_filter(dedup);
        dispatcher.add_filter(sampler);
        dispatcher.add_filter(rate_limit);
        
        // Send variety of messages
        for (int i = 0; i < 50; ++i)
        {
            LOG(info) << "Message " << (i % 10) << endl;  // 10 unique messages, repeated
        }
        
        dispatcher.flush();
        
        // Complex interaction:
        // - Dedup reduces to 10 unique messages
        // - Sampler passes ~80% of those (expected ~8)
        // - Rate limit allows all (well under 100)
        auto count = sink.get_message_count();
        REQUIRE(count >= 6);   // Allow for sampling variance
        REQUIRE(count <= 10);  // Can't exceed unique count
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
}

TEST_CASE("Filter RCU updates", "[filters][rcu]")
{
    SECTION("Add filter while logging")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Start logging thread
        std::atomic<bool> stop{false};
        std::thread logger([&stop]() {
            int counter = 0;
            while (!stop.load())
            {
                LOG(info) << "Continuous message " << counter++ << endl;
                std::this_thread::sleep_for(1ms);
            }
        });
        
        // Let some messages through
        std::this_thread::sleep_for(50ms);
        
        // Add filter while logging is active
        auto filter = std::make_shared<test_filter>(true);
        dispatcher.add_filter(filter);
        
        // Messages should now be filtered
        std::this_thread::sleep_for(50ms);
        
        // Stop logging
        stop.store(true);
        logger.join();
        
        dispatcher.flush();
        
        // Should have some messages before filter, none after
        // (exact count depends on timing)
        REQUIRE(sink.get_message_count() > 0);
        REQUIRE(filter->get_processed_count() > 0);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
    
    SECTION("Remove filter while logging")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add drop filter initially
        auto filter = std::make_shared<test_filter>(true);
        dispatcher.add_filter(filter);
        
        // Start logging thread
        std::atomic<bool> stop{false};
        std::thread logger([&stop]() {
            int counter = 0;
            while (!stop.load())
            {
                LOG(info) << "Continuous message " << counter++ << endl;
                std::this_thread::sleep_for(1ms);
            }
        });
        
        // Messages are being dropped
        std::this_thread::sleep_for(50ms);
        dispatcher.flush();
        auto initial_count = sink.get_message_count();
        REQUIRE(initial_count == 0);
        
        // Remove filter while logging is active
        dispatcher.clear_filters();
        
        // Messages should now pass through
        std::this_thread::sleep_for(50ms);
        
        // Stop logging
        stop.store(true);
        logger.join();
        
        dispatcher.flush();
        
        // Should have messages after filter removal
        REQUIRE(sink.get_message_count() > 0);
        
        // Cleanup
        dispatcher.set_sink(0, nullptr);
    }
}

TEST_CASE("Filter edge cases", "[filters][edge]")
{
    SECTION("Empty batch")
    {
        auto filter = std::make_shared<test_filter>(true);
        log_buffer_base* buffers[0];
        filter->process_batch(buffers, 0);
        REQUIRE(filter->get_processed_count() == 0);
    }
    
    SECTION("Null buffers in batch")
    {
        auto filter = std::make_shared<test_filter>(true);
        log_buffer_base* buffers[3] = {nullptr, nullptr, nullptr};
        filter->process_batch(buffers, 3);
        
        // Null buffers should not be processed
        REQUIRE(filter->get_processed_count() == 0);
    }
    
    SECTION("Flush markers")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Set up test sink
        test_filter_sink sink;
        dispatcher.set_sink(0, sink.get_sink());
        
        // Add drop-all filter
        auto filter = std::make_shared<test_filter>(true);
        dispatcher.add_filter(filter);
        
        // Flush should still work even with drop-all filter
        LOG(info) << "Message before flush" << endl;
        dispatcher.flush();
        
        // Message was dropped but flush completed
        REQUIRE(sink.get_message_count() == 0);
        
        // Cleanup
        dispatcher.clear_filters();
        dispatcher.set_sink(0, nullptr);
    }
}

TEST_CASE("Filter memory management", "[filters][memory]")
{
    SECTION("Filter lifecycle")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Create filter with internal state
        auto dedup = std::make_shared<dedup_filter>(100ms);
        
        // Add some state to the filter
        {
            log_buffer<2048> buffer;
            buffer.reset();
            buffer.write_raw("Test message");
            buffer.finalize();
            
            log_buffer_base* buffers[1] = {&buffer};
            dedup->process_batch(buffers, 1);
        }
        
        // Add to dispatcher
        dispatcher.add_filter(dedup);
        
        // Remove from dispatcher - should call shutdown()
        dispatcher.clear_filters();
        
        // Filter should clean up properly
        // (No crash, valgrind clean)
    }
    
    SECTION("Multiple filter references")
    {
        auto& dispatcher = log_line_dispatcher::instance();
        dispatcher.clear_filters();
        
        // Create shared filter
        auto filter = std::make_shared<test_filter>(false);
        
        // Add same filter multiple times (should work due to shared_ptr)
        dispatcher.add_filter(filter);
        dispatcher.add_filter(filter);
        
        REQUIRE(dispatcher.filter_count() == 2);
        
        // Remove one instance
        dispatcher.remove_filter(0);
        REQUIRE(dispatcher.filter_count() == 1);
        
        // Filter should still be alive
        REQUIRE(filter->get_processed_count() == 0);
        
        // Clear all
        dispatcher.clear_filters();
        REQUIRE(dispatcher.filter_count() == 0);
    }
}