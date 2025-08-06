/**
 * @file test_static_buffer.cpp
 * @brief Tests for C++20 static_buffer implementation
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include "generic_buffer_pool.hpp"
#include "static_buffer.hpp"
#include "static_buffer_impl.hpp"  // For release() implementation
#include <cstring>
#include <thread>
#include <vector>
#include <span>

using namespace slwoggy;

// Test buffer using C++20 static_buffer
using test_buffer = static_buffer<64, 0, 32>;  // 64 byte header, dynamic data, 32 byte footer

// Helper constant for tests - accounts for aligned buffer header
// The actual header size is implementation-dependent due to alignment
constexpr size_t BUFFER_HEADER_SIZE = 8;  // alignas(std::max_align_t) typically 8 bytes

TEST_CASE("static_buffer compile-time layout", "[static_buffer]")
{
    SECTION("compile-time layout properties")
    {
        // These are all compile-time checks
        static_assert(test_buffer::N_REGIONS == 3);
        static_assert(test_buffer::layout_type::region_count == 3);
        static_assert(test_buffer::layout_type::fixed_total() == 96);  // 64 + 32
        static_assert(test_buffer::layout_type::zero_count() == 1);
        static_assert(test_buffer::layout_type::has_dynamic_regions());
        
        // Check individual regions at compile time
        static_assert(test_buffer::is_dynamic_region<1>());
        static_assert(!test_buffer::is_dynamic_region<0>());
        static_assert(!test_buffer::is_dynamic_region<2>());
        
        REQUIRE(true);  // Just to have a runtime check
    }
    
    SECTION("region initialization with dynamic middle")
    {
        generic_buffer_pool pool(10, 512);
        char* storage = pool.acquire_storage();
        REQUIRE(storage != nullptr);
        
        test_buffer buffer(storage, 512, &pool);
        
        REQUIRE(buffer.region_count() == 3);
        
        // Check header region (64 bytes)
        auto& header = buffer.get_region<0>();
        REQUIRE(header.start == 0);
        REQUIRE(header.capacity == 64);
        REQUIRE(header.len == 0);
        
        // Check data region (should get remaining space minus footer)
        auto& data = buffer.get_region<1>();
        REQUIRE(data.start == 64);
        // The actual capacity depends on internal header alignment
        // Just verify it got the remaining space after header and footer
        size_t expected_remaining = 512 - 64 - 32 - BUFFER_HEADER_SIZE;
        REQUIRE(data.capacity == expected_remaining);
        
        // Check footer region (32 bytes)
        auto& footer = buffer.get_region<2>();
        REQUIRE(footer.start == data.start + data.capacity);
        REQUIRE(footer.capacity == 32);
        
        buffer.release();
    }
}

TEST_CASE("static_buffer with C++20 concepts", "[static_buffer]")
{
    SECTION("type-safe append operations")
    {
        generic_buffer_pool pool(10, 512);
        char* storage = pool.acquire_storage();
        test_buffer buffer(storage, 512, &pool);
        
        // Append typed data
        struct Header {
            uint32_t magic;
            uint32_t version;
        };
        
        Header h{0xDEADBEEF, 1};
        size_t written = buffer.append_region_typed(0, h);
        REQUIRE(written == sizeof(Header));
        
        // Verify data
        const Header* stored = reinterpret_cast<const Header*>(buffer.get_region_data<0>());
        REQUIRE(stored->magic == 0xDEADBEEF);
        REQUIRE(stored->version == 1);
        
        buffer.release();
    }
    
    SECTION("span-based operations")
    {
        generic_buffer_pool pool(10, 512);
        char* storage = pool.acquire_storage();
        test_buffer buffer(storage, 512, &pool);
        
        // Use C++20 span for append
        std::array<std::byte, 8> data{};
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = std::byte(i);
        }
        
        std::span<const std::byte> span{data};
        size_t written = buffer.append_region<1>(span);
        REQUIRE(written == 8);
        
        // Verify
        const char* region_data = buffer.get_region_data<1>();
        for (size_t i = 0; i < 8; ++i) {
            REQUIRE(static_cast<uint8_t>(region_data[i]) == i);
        }
        
        buffer.release();
    }
}

TEST_CASE("static_buffer various layouts", "[static_buffer]")
{
    generic_buffer_pool pool(10, 1024);
    
    SECTION("single dynamic region")
    {
        using single_buffer = static_buffer<0>;
        static_assert(single_buffer::N_REGIONS == 1);
        static_assert(single_buffer::layout_type::zero_count() == 1);
        
        char* storage = pool.acquire_storage();
        single_buffer buffer(storage, 1024, &pool);
        
        REQUIRE(buffer.region_count() == 1);
        auto& region = buffer.get_region<0>();
        // Account for aligned header
        REQUIRE(region.capacity == 1024 - BUFFER_HEADER_SIZE);
        
        buffer.release();
    }
    
    SECTION("all fixed regions")
    {
        using fixed_buffer = static_buffer<100, 200, 300, 400>;
        static_assert(fixed_buffer::N_REGIONS == 4);
        static_assert(fixed_buffer::layout_type::fixed_total() == 1000);
        static_assert(!fixed_buffer::layout_type::has_dynamic_regions());
        
        char* storage = pool.acquire_storage();
        fixed_buffer buffer(storage, 1024, &pool);
        
        REQUIRE(buffer.get_region<0>().capacity == 100);
        REQUIRE(buffer.get_region<1>().capacity == 200);
        REQUIRE(buffer.get_region<2>().capacity == 300);
        REQUIRE(buffer.get_region<3>().capacity == 400);
        
        buffer.release();
    }
    
    SECTION("multiple dynamic regions")
    {
        using multi_dynamic = static_buffer<100, 0, 0, 100>;
        static_assert(multi_dynamic::N_REGIONS == 4);
        static_assert(multi_dynamic::layout_type::fixed_total() == 200);
        static_assert(multi_dynamic::layout_type::zero_count() == 2);
        
        char* storage = pool.acquire_storage();
        multi_dynamic buffer(storage, 1024, &pool);
        
        REQUIRE(buffer.get_region<0>().capacity == 100);
        REQUIRE(buffer.get_region<3>().capacity == 100);
        
        // The two dynamic regions should split the remaining space
        // Account for aligned header
        size_t remaining = 1024 - BUFFER_HEADER_SIZE - 200;
        size_t each = remaining / 2;
        
        REQUIRE(buffer.get_region<1>().capacity == each);
        // Region 2 gets the remainder (including any odd byte)
        REQUIRE(buffer.get_region<2>().capacity == remaining - each);
        
        buffer.release();
    }
}

TEST_CASE("static_buffer type aliases", "[static_buffer]")
{
    generic_buffer_pool pool(10, 2048);
    
    SECTION("header_body_footer_buffer")
    {
        using hbf_buffer = header_body_footer_buffer<256, 128>;
        static_assert(hbf_buffer::N_REGIONS == 3);
        
        char* storage = pool.acquire_storage();
        hbf_buffer buffer(storage, 2048, &pool);
        
        REQUIRE(buffer.get_region<0>().capacity == 256);  // header
        REQUIRE(buffer.get_region<2>().capacity == 128);  // footer
        // Middle region gets the rest
        REQUIRE(buffer.get_region<1>().capacity > 0);
        
        buffer.release();
    }
    
    SECTION("metadata_buffer")
    {
        using meta_buffer = metadata_buffer<512>;
        static_assert(meta_buffer::N_REGIONS == 2);
        
        char* storage = pool.acquire_storage();
        meta_buffer buffer(storage, 2048, &pool);
        
        REQUIRE(buffer.get_region<0>().capacity == 512);  // metadata
        // Account for aligned header
        REQUIRE(buffer.get_region<1>().capacity == 2048 - BUFFER_HEADER_SIZE - 512);  // data
        
        buffer.release();
    }
}

TEST_CASE("static_buffer C++20 concepts", "[static_buffer][concepts]")
{
    SECTION("HasRegionCount concept")
    {
        using three_region = static_buffer<10, 20, 30>;
        using two_region = static_buffer<50, 0>;
        
        static_assert(HasRegionCount<three_region, 3>);
        static_assert(!HasRegionCount<three_region, 2>);
        static_assert(HasRegionCount<two_region, 2>);
        static_assert(!HasRegionCount<two_region, 3>);
        
        REQUIRE(true);  // Just to have a runtime check
    }
    
    SECTION("ValidRegionLayout concept")
    {
        struct valid_buffer {
            using layout_type = region_layout<10, 20, 30>;
        };
        
        static_assert(ValidRegionLayout<valid_buffer>);
        
        REQUIRE(true);
    }
}

TEST_CASE("static_buffer performance with compile-time optimization", "[static_buffer][benchmark]")
{
    generic_buffer_pool pool(100, 2048);
    
    BENCHMARK("static_buffer append (compile-time regions)")
    {
        char* storage = pool.acquire_storage();
        test_buffer buffer(storage, 2048, &pool);
        
        const char* data = "Test data for benchmarking";
        buffer.append_region<1>(data, strlen(data));
        
        buffer.release();
        return buffer.get_region<1>().len;
    };
    
    BENCHMARK("static_buffer with fixed regions (no runtime calc)")
    {
        using fixed = static_buffer<256, 512, 256>;
        
        char* storage = pool.acquire_storage();
        fixed buffer(storage, 2048, &pool);
        
        const char* data = "Fixed region test";
        buffer.append_region<1>(data, strlen(data));
        
        buffer.release();
        return buffer.get_region<1>().len;
    };
}