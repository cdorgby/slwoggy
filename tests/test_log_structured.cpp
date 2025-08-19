#include <catch2/catch_test_macros.hpp>
#include "log.hpp"
#include <thread>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <condition_variable>

using namespace slwoggy;

// Test sink that captures structured data and text
class test_structured_sink {
public:
    struct captured_log {
        log_level level;
        std::string file;
        uint32_t line;
        std::string text;
        std::map<std::string, std::string> metadata;
    };

    // Formatter that captures log data
    class capturing_formatter {
        test_structured_sink* parent_;
        
    public:
        capturing_formatter(test_structured_sink* parent) : parent_(parent) {}
        
        size_t calculate_size(const log_buffer_base* buffer) const {
            // We don't actually format, just capture
            return 0;
        }
        
        size_t format(const log_buffer_base* buffer, char* output, size_t size) const {
            if (!buffer) return 0;

            captured_log entry;
            entry.level = buffer->level_;
            entry.file = std::string(buffer->file_);
            entry.line = buffer->line_;
            // Get just the text content, not the full buffer
            entry.text = std::string(buffer->get_text());
            
            // Extract metadata
            auto metadata = buffer->get_metadata_adapter();
            
            auto iter = metadata.get_iterator();
            
            while (iter.has_next()) {
                auto kv = iter.next();
                auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);
                entry.metadata[std::string(key_name)] = std::string(kv.value);
            }

            {
                std::lock_guard<std::mutex> lock(parent_->mutex);
                parent_->logs.push_back(std::move(entry));
            }
            parent_->cv.notify_all();

            return 0;
        }
    };
    
    // Null writer (we don't actually write anywhere)
    struct null_writer {
        void write(const char*, size_t) const {}
    };

private:
    std::shared_ptr<log_sink> sink_;
    
public:
    mutable std::vector<captured_log> logs;
    mutable std::mutex mutex;
    mutable std::condition_variable cv;

    test_structured_sink() {
        sink_ = std::make_shared<log_sink>(capturing_formatter{this}, null_writer{});
    }
    
    std::shared_ptr<log_sink> get_sink() const { return sink_; }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        logs.clear();
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(mutex);
        return logs.size();
    }

    const captured_log& get(size_t index) const {
        std::lock_guard<std::mutex> lock(mutex);
        return logs.at(index);
    }
    
    bool wait_for_logs(size_t expected_count, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) const {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [this, expected_count] {
            return logs.size() >= expected_count;
        });
    }
};

// Global test sink management for structured logging tests
class GlobalTestSinkManager {
public:
    static test_structured_sink* get_sink() {
        static GlobalTestSinkManager instance;
        return instance.sink_;
    }
    
    static void reset() {
        get_sink()->clear();
        log_line_dispatcher::instance().flush();
    }
    
private:
    test_structured_sink* sink_;
    
    GlobalTestSinkManager() {
        sink_ = new test_structured_sink();
        // Use slot 1 for test sink (slot 0 is stdout)
        log_line_dispatcher::instance().set_sink(1, sink_->get_sink());
    }
    
    ~GlobalTestSinkManager() {
        // Remove test sink
        log_line_dispatcher::instance().set_sink(1, nullptr);
        // Flush to ensure worker thread is done with the sink
        log_line_dispatcher::instance().flush();
        // Give worker thread time to finish with old config
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        delete sink_;
    }
    
    // Delete copy/move constructors
    GlobalTestSinkManager(const GlobalTestSinkManager&) = delete;
    GlobalTestSinkManager& operator=(const GlobalTestSinkManager&) = delete;
    GlobalTestSinkManager(GlobalTestSinkManager&&) = delete;
    GlobalTestSinkManager& operator=(GlobalTestSinkManager&&) = delete;
};

// Helper to get the global test sink
test_structured_sink* get_test_sink() {
    return GlobalTestSinkManager::get_sink();
}

TEST_CASE("Structured log key registry", "[structured]") {
    SECTION("Basic key registration") {
        auto& registry = structured_log_key_registry::instance();
        
        uint16_t id1 = registry.get_or_register_key(std::string_view("user_id"));
        uint16_t id2 = registry.get_or_register_key(std::string_view("request_id"));
        uint16_t id3 = registry.get_or_register_key(std::string_view("user_id")); // Same key
        
        REQUIRE(id1 != id2);
        REQUIRE(id1 == id3); // Same key should return same ID
        
        REQUIRE(registry.get_key(id1) == "user_id");
        REQUIRE(registry.get_key(id2) == "request_id");
        REQUIRE(registry.get_key(9999) == "unknown");
    }

    SECTION("Internal keys pre-registration") {
        auto& registry = structured_log_key_registry::instance();
        
        // Verify internal keys are pre-registered with correct IDs
        REQUIRE(registry.get_or_register_key(structured_log_key_registry::INTERNAL_KEY_NAME_TS) == structured_log_key_registry::INTERNAL_KEY_TS);
        REQUIRE(registry.get_or_register_key(structured_log_key_registry::INTERNAL_KEY_NAME_LEVEL) == structured_log_key_registry::INTERNAL_KEY_LEVEL);
        REQUIRE(registry.get_or_register_key(structured_log_key_registry::INTERNAL_KEY_NAME_MODULE) == structured_log_key_registry::INTERNAL_KEY_MODULE);
        REQUIRE(registry.get_or_register_key(structured_log_key_registry::INTERNAL_KEY_NAME_FILE) == structured_log_key_registry::INTERNAL_KEY_FILE);
        REQUIRE(registry.get_or_register_key(structured_log_key_registry::INTERNAL_KEY_NAME_LINE) == structured_log_key_registry::INTERNAL_KEY_LINE);
        
        // Verify the IDs are as expected
        REQUIRE(structured_log_key_registry::INTERNAL_KEY_TS == 0);
        REQUIRE(structured_log_key_registry::INTERNAL_KEY_LEVEL == 1);
        REQUIRE(structured_log_key_registry::INTERNAL_KEY_MODULE == 2);
        REQUIRE(structured_log_key_registry::INTERNAL_KEY_FILE == 3);
        REQUIRE(structured_log_key_registry::INTERNAL_KEY_LINE == 4);
        
        // Verify reverse lookup works
        REQUIRE(registry.get_key(0) == structured_log_key_registry::INTERNAL_KEY_NAME_TS);
        REQUIRE(registry.get_key(1) == structured_log_key_registry::INTERNAL_KEY_NAME_LEVEL);
        REQUIRE(registry.get_key(2) == structured_log_key_registry::INTERNAL_KEY_NAME_MODULE);
        REQUIRE(registry.get_key(3) == structured_log_key_registry::INTERNAL_KEY_NAME_FILE);
        REQUIRE(registry.get_key(4) == structured_log_key_registry::INTERNAL_KEY_NAME_LINE);
        
        // Verify user keys start at ID 5 or higher
        uint16_t user_key_id = registry.get_or_register_key("test_user_key");
        REQUIRE(user_key_id >= structured_log_key_registry::FIRST_USER_KEY_ID);
    }

    SECTION("Thread safety") {
        auto& registry = structured_log_key_registry::instance();
        std::vector<std::thread> threads;
        std::atomic<int> success_count{0};
        
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&registry, &success_count, i]() {
                try {
                    std::string key = "thread_key_" + std::to_string(i);
                    uint16_t id = registry.get_or_register_key(std::string_view(key));
                    if (registry.get_key(id) == key) {
                        success_count++;
                    }
                } catch (...) {
                    // Ignore exceptions in test
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(success_count == 10);
    }
}

TEST_CASE("Metadata adapter", "[structured]") {
    auto* buffer = buffer_pool::instance().acquire(false);
    REQUIRE(buffer != nullptr);
    buffer->reset();
    auto adapter = buffer->get_metadata_adapter();
    
    SECTION("Add and retrieve key-value pairs") {
        REQUIRE(adapter.add_kv(1, "value1"));
        REQUIRE(adapter.add_kv(2, "value2"));
        REQUIRE(adapter.add_kv(3, "123"));
        
        // Get count from buffer
        REQUIRE(buffer->get_kv_count() == 3);
        
        std::vector<std::pair<uint16_t, std::string>> found;
        auto iter = adapter.get_iterator();
        while (iter.has_next()) {
            auto kv = iter.next();
            found.emplace_back(kv.key_id, std::string(kv.value));
        }
        
        REQUIRE(found.size() == 3);
        // Note: New KV pairs are added at the beginning, so order is reversed
        REQUIRE(found[0] == std::make_pair(uint16_t(3), std::string("123")));
        REQUIRE(found[1] == std::make_pair(uint16_t(2), std::string("value2")));
        REQUIRE(found[2] == std::make_pair(uint16_t(1), std::string("value1")));
        
        buffer->release();
    }
    
    SECTION("Metadata size limits") {
        // Try to add data that would collide with text area
        // Fill text area to leave minimal space
        std::string text(LOG_BUFFER_SIZE - 100, 'x');
        buffer->write_raw(text);
        
        // Now try to add metadata that won't fit
        std::string large_value(150, 'y');
        REQUIRE(adapter.add_kv(1, large_value) == false);
        
        // Should still be empty
        REQUIRE(buffer->get_kv_count() == 0);
        
        buffer->release();
    }
    
    SECTION("Reset clears metadata") {
        adapter.add_kv(1, "test");
        REQUIRE(buffer->get_kv_count() == 1);
        
        buffer->reset();
        REQUIRE(buffer->get_kv_count() == 0);
        
        buffer->release();
    }
}

TEST_CASE("Basic structured logging", "[structured]") {
    GlobalTestSinkManager::reset();
    auto* test_sink = get_test_sink();

    LOG(info).add("user_id", 12345).add("action", "login").add("ip", "192.168.1.1") << "User logged in" << endl;

    log_line_dispatcher::instance().flush();
    REQUIRE(test_sink->wait_for_logs(1));
    
    REQUIRE(test_sink->count() == 1);
    auto &log = test_sink->get(0);

    REQUIRE(log.level == log_level::info);
    // The text includes the full formatted log line
    REQUIRE(log.text.find("User logged in") != std::string::npos);
    REQUIRE(log.metadata.size() == 3);
    REQUIRE(log.metadata.at("user_id") == "12345");
    REQUIRE(log.metadata.at("action") == "login");
    REQUIRE(log.metadata.at("ip") == "192.168.1.1");
}

TEST_CASE("Structured logging value types", "[structured]") {
    SECTION("Different value types") {
        GlobalTestSinkManager::reset();
        auto* test_sink = get_test_sink();

        LOG(debug)
                .add("int_val", 42)
                .add("float_val", 3.14f)
                .add("double_val", 2.71828)
                .add("bool_val", true)
                .add("string_val", std::string("test"))
            << "Testing value types" << endl;

        log_line_dispatcher::instance().flush();
        REQUIRE(test_sink->wait_for_logs(1));
        
        REQUIRE(test_sink->count() == 1);
        auto& log = test_sink->get(0);
        
        REQUIRE(log.metadata.at("int_val") == "42");
        REQUIRE(log.metadata.at("float_val").substr(0, 4) == "3.14");
        REQUIRE(log.metadata.at("double_val").substr(0, 7) == "2.71828");
        REQUIRE(log.metadata.at("bool_val") == "true");
        REQUIRE(log.metadata.at("string_val") == "test");
    }
}

TEST_CASE("Structured logging empty and format", "[structured]") {
    SECTION("Empty structured data") {
        GlobalTestSinkManager::reset();
        auto* test_sink = get_test_sink();

        LOG(warn) << "No structured data" << endl;

        log_line_dispatcher::instance().flush();
        REQUIRE(test_sink->wait_for_logs(1));
        REQUIRE(test_sink->count() == 1);
        auto &log = test_sink->get(0);

        REQUIRE(log.text.find("No structured data") != std::string::npos);
        REQUIRE(log.metadata.empty());
    }

    SECTION("Structured data with format")
    {
        GlobalTestSinkManager::reset();
        auto* test_sink = get_test_sink();

        int count = 5;
        LOG(info).add("count", count).add("operation", "process").format("Processed {} items", count) << endl;

        log_line_dispatcher::instance().flush();
        REQUIRE(test_sink->wait_for_logs(1));
        REQUIRE(test_sink->count() == 1);
        auto& log = test_sink->get(0);
        
        REQUIRE(log.text.find("Processed 5 items") != std::string::npos);
        REQUIRE(log.metadata.at("count") == "5");
        REQUIRE(log.metadata.at("operation") == "process");
    }
}

TEST_CASE("Structured logging multiple logs", "[structured]") {
    SECTION("Multiple logs with structured data") {
        GlobalTestSinkManager::reset();
        auto* test_sink = get_test_sink();
        
        LOG(info).add("req_id", "123") << "Request start" << endl;
        LOG(debug).add("req_id", "123").add("step", 1) << "Processing" << endl;
        LOG(info).add("req_id", "123").add("duration_ms", 45) << "Request complete" << endl;
        
        log_line_dispatcher::instance().flush();
        REQUIRE(test_sink->wait_for_logs(3));
        
        REQUIRE(test_sink->count() == 3);
        
        // All should have req_id
        for (size_t i = 0; i < 3; ++i) {
            REQUIRE(test_sink->get(i).metadata.count("req_id") == 1);
            REQUIRE(test_sink->get(i).metadata.at("req_id") == "123");
        }
        
        // Check specific metadata
        REQUIRE(test_sink->get(1).metadata.at("step") == "1");
        REQUIRE(test_sink->get(2).metadata.at("duration_ms") == "45");
    }
}

TEST_CASE("Structured logging edge cases", "[structured]") {
    SECTION("Very long key names") {
        GlobalTestSinkManager::reset();
        auto* test_sink = get_test_sink();
        
        std::string long_key(100, 'k');
        LOG(info).add(long_key, "value") << "Long key test" << endl;
        
        log_line_dispatcher::instance().flush();
        REQUIRE(test_sink->wait_for_logs(1));
        
        REQUIRE(test_sink->count() == 1);
        auto& log = test_sink->get(0);
        REQUIRE(log.metadata.at(long_key) == "value");
    }
    
    SECTION("Special characters in values") {
        GlobalTestSinkManager::reset();
        auto* test_sink = get_test_sink();

        LOG(info)
                .add("newline", "line1\nline2")
                .add("tab", "col1\tcol2")
                .add("quotes", "\"quoted\"")
                .add("unicode", "emoji: 🚀")
            << "Special chars" << endl;

        log_line_dispatcher::instance().flush();
        REQUIRE(test_sink->wait_for_logs(1));
        REQUIRE(test_sink->count() == 1);
        auto& log = test_sink->get(0);
        REQUIRE(log.metadata.at("newline") == "line1\nline2");
        REQUIRE(log.metadata.at("tab") == "col1\tcol2");
        REQUIRE(log.metadata.at("quotes") == "\"quoted\"");
        REQUIRE(log.metadata.at("unicode") == "emoji: 🚀");
    }
    
    SECTION("Same key multiple times") {
        GlobalTestSinkManager::reset();
        auto* test_sink = get_test_sink();

        LOG(info).add("key", "value1").add("key", "value2") // Should overwrite
            << "Duplicate key" << endl;

        log_line_dispatcher::instance().flush();
        REQUIRE(test_sink->wait_for_logs(1));

        REQUIRE(test_sink->count() == 1);
        auto& log = test_sink->get(0);
        // The test sink uses std::map::operator[] which overwrites duplicates
        // Since we add KV pairs at the beginning, the order is reversed,
        // so "value1" is processed last and becomes the final value
        REQUIRE(log.metadata.size() == 1);
        REQUIRE(log.metadata.at("key") == "value1");
    }

}

TEST_CASE("Log buffer with metadata", "[structured]") {
    SECTION("Buffer text starts after metadata") {
        auto* buffer = buffer_pool::instance().acquire(false);
        REQUIRE(buffer != nullptr);
        
        // Add some metadata
        auto metadata = buffer->get_metadata_adapter();
        metadata.add_kv(1, "test");
        
        // Write text
        buffer->write_raw("Hello world");
        
        // Check that text is in the right place
        auto text = buffer->get_text();
        REQUIRE(text == "Hello world");
        
        // Check raw buffer layout
        REQUIRE(buffer->len() == 11);
        
        buffer->release();
    }
    
    SECTION("Flush marker detection with metadata offset") {
        auto* buffer = buffer_pool::instance().acquire(false);
        buffer->level_ = log_level::nolog;
        buffer->reset();
        
        REQUIRE(buffer->is_flush_marker() == true);
        
        // Add text - no longer a flush marker
        buffer->write_raw("x");
        REQUIRE(buffer->is_flush_marker() == false);
        
        buffer->release();
    }
}