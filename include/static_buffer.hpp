/**
 * @file static_buffer.hpp
 * @brief Compile-time buffer with C++20 template metaprogramming
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include "buffer_concept.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstring>
#include <algorithm>
#include <utility>
#include <span>

namespace slwoggy
{

// Forward declaration - full definition in generic_buffer_pool.hpp
class generic_buffer_pool;

/**
 * @brief Compile-time region layout calculator using C++20 features
 */
template<size_t... SIZES>
struct region_layout
{
    static constexpr size_t region_count = sizeof...(SIZES);
    static constexpr std::array<size_t, region_count> region_sizes = {SIZES...};
    
    // C++20 consteval with fold expressions for compile-time calculations
    static consteval size_t fixed_total() noexcept
    {
        return ((SIZES != 0 ? SIZES : 0) + ...);
    }
    
    static consteval size_t zero_count() noexcept
    {
        return ((SIZES == 0 ? 1 : 0) + ...);
    }
    
    static consteval size_t max_size() noexcept
    {
        size_t max = 0;
        ((max = (SIZES > max ? SIZES : max)), ...);
        return max;
    }
    
    // Check if layout is valid (at least one region)
    static consteval bool is_valid() noexcept
    {
        return region_count > 0;
    }
    
    // Check if layout has dynamic regions (zeros)
    static consteval bool has_dynamic_regions() noexcept
    {
        return zero_count() > 0;
    }
};

/**
 * @brief Concept for valid region layouts
 */
template<typename T>
concept ValidRegionLayout = requires {
    typename T::layout_type;
    { T::layout_type::is_valid() } -> std::convertible_to<bool>;
    requires T::layout_type::is_valid();
};

/**
 * @brief Compile-time buffer region info
 */
struct static_region_info
{
    size_t start;
    size_t capacity;
    
    constexpr static_region_info() noexcept : start(0), capacity(0) {}
    constexpr static_region_info(size_t s, size_t c) noexcept : start(s), capacity(c) {}
};

/**
 * @brief Compile-time region calculator with C++20 features
 */
template<size_t TOTAL_SIZE, size_t... REGION_SIZES>
class region_calculator
{
    static constexpr auto layout = region_layout<REGION_SIZES...>{};
    static constexpr size_t N = layout.region_count;
    
    // Calculate regions at compile time
    static consteval std::array<static_region_info, N> calculate_regions() noexcept
    {
        std::array<static_region_info, N> regions{};
        
        const size_t fixed = layout.fixed_total();
        const size_t zero_cnt = layout.zero_count();
        const size_t remaining = (TOTAL_SIZE > fixed) ? (TOTAL_SIZE - fixed) : 0;
        const size_t per_zero = (zero_cnt > 0) ? (remaining / zero_cnt) : 0;
        
        size_t offset = 0;
        size_t zeros_processed = 0;
        size_t remaining_for_zeros = remaining;
        
        for (size_t i = 0; i < N; ++i)
        {
            regions[i].start = offset;
            
            if (layout.region_sizes[i] == 0)
            {
                zeros_processed++;
                // Last zero region gets any remainder from division
                if (zeros_processed == zero_cnt)
                {
                    regions[i].capacity = remaining_for_zeros;
                }
                else
                {
                    regions[i].capacity = per_zero;
                    remaining_for_zeros -= per_zero;
                }
            }
            else
            {
                // Fixed size, but cap at remaining space
                regions[i].capacity = (offset + layout.region_sizes[i] <= TOTAL_SIZE) ?
                                     layout.region_sizes[i] : 
                                     (TOTAL_SIZE > offset ? TOTAL_SIZE - offset : 0);
            }
            
            offset += regions[i].capacity;
        }
        
        return regions;
    }
    
public:
    static constexpr auto regions = calculate_regions();
    
    // Compile-time accessors
    static consteval size_t region_start(size_t idx) noexcept
    {
        return idx < N ? regions[idx].start : 0;
    }
    
    static consteval size_t region_capacity(size_t idx) noexcept
    {
        return idx < N ? regions[idx].capacity : 0;
    }
    
    static consteval size_t total_used() noexcept
    {
        size_t total = 0;
        for (const auto& r : regions)
        {
            total += r.capacity;
        }
        return total;
    }
    
    // Compile-time validation
    static_assert(total_used() <= TOTAL_SIZE, 
                  "Region layout exceeds total buffer size");
    static_assert(layout.fixed_total() <= TOTAL_SIZE || layout.zero_count() == 0,
                  "Fixed regions exceed total size with no dynamic regions to absorb");
};

/**
 * @brief Static buffer with compile-time region layout using C++20
 * @tparam REGION_SIZES Sizes for each region (0 means "share remaining space equally")
 */
template<size_t... REGION_SIZES>
    requires (sizeof...(REGION_SIZES) > 0)  // C++20 requires clause
class static_buffer : public buffer_concept
{
public:
    using layout_type = region_layout<REGION_SIZES...>;
    static constexpr size_t N_REGIONS = layout_type::region_count;
    
private:
    // Intrusive buffer header for cleaner memory layout
    struct alignas(alignof(std::max_align_t)) buffer_header {
        std::atomic<int> ref_count;
        // Padding ensures data starts at max_align_t boundary
    };
    
    static_assert(alignof(buffer_header) >= alignof(std::atomic<int>),
                  "Buffer header alignment issue");
    
    buffer_header* header_;                        ///< Intrusive header at start of storage
    char* data_;                                    ///< Pointer to aligned usable buffer data
    size_t total_size_;                            ///< Total size of usable buffer
    std::array<buffer_region, N_REGIONS> regions_; ///< Runtime region info
    generic_buffer_pool* pool_;                    ///< Pool that owns this buffer's storage
    
    /**
     * @brief Shared region calculation logic - works for both compile-time and runtime
     * 
     * IMPORTANT: Region Overflow Behavior
     * If the total of fixed-size regions exceeds the available buffer size, regions
     * are silently capped to fit within available space. This is by design to:
     * - Prevent buffer overflows
     * - Allow graceful degradation when buffer sizes vary at runtime
     * - Support scenarios where the pool provides different sized buffers
     * 
     * Developers should verify their region requirements fit within the expected
     * buffer size. Use assertions in debug builds if strict size requirements exist.
     * 
     * @param region_sizes Array of requested region sizes (0 = dynamic)
     * @param actual_total_size Total available buffer space
     * @param regions_out Output array of calculated regions
     */
    template<typename RegionArray>
    static void calculate_region_layout(
        const std::array<size_t, N_REGIONS>& region_sizes,
        size_t actual_total_size,
        RegionArray& regions_out)
    {
        const size_t fixed = layout_type::fixed_total();
        const size_t zero_cnt = layout_type::zero_count();
        const size_t remaining = (actual_total_size > fixed) ? (actual_total_size - fixed) : 0;
        const size_t per_zero = (zero_cnt > 0) ? (remaining / zero_cnt) : 0;
        
        size_t offset = 0;
        size_t zeros_processed = 0;
        size_t remaining_for_zeros = remaining;
        
        for (size_t i = 0; i < N_REGIONS; ++i)
        {
            regions_out[i].start = offset;
            regions_out[i].len = 0;
            regions_out[i].include_in_output = true;
            
            if (region_sizes[i] == 0)
            {
                zeros_processed++;
                if (zeros_processed == zero_cnt)
                {
                    regions_out[i].capacity = remaining_for_zeros;
                }
                else
                {
                    regions_out[i].capacity = per_zero;
                    remaining_for_zeros -= per_zero;
                }
            }
            else
            {
                regions_out[i].capacity = std::min(region_sizes[i], 
                                                  actual_total_size - offset);
            }
            
            offset += regions_out[i].capacity;
        }
    }
    
    // Use if constexpr to optimize based on layout properties
    void initialize_regions()
    {
        if constexpr (layout_type::has_dynamic_regions())
        {
            // Has dynamic regions - use shared calculation logic
            calculate_region_layout(layout_type::region_sizes, total_size_, regions_);
        }
        else
        {
            // All fixed sizes - can use more compile-time optimization
            size_t offset = 0;
            const size_t usable_size = total_size_;
            
            // Use fold expression with index sequence for better optimization
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((regions_[Is] = {
                    .start = offset,
                    .capacity = std::min(layout_type::region_sizes[Is], usable_size - offset),
                    .len = 0,
                    .include_in_output = true
                }, offset += regions_[Is].capacity), ...);
            }(std::make_index_sequence<N_REGIONS>{});
        }
    }
    
public:
    /**
     * @brief Construct a static buffer
     * @param storage Pointer to pool-allocated storage
     * @param size Total size of storage
     * @param pool Pointer to the owning pool
     */
    static_buffer(char* storage, size_t size, generic_buffer_pool* pool)
        : pool_(pool)
    {
        static_assert(N_REGIONS > 0, "Buffer must have at least one region");
        
        assert(storage != nullptr);
        assert(size > sizeof(buffer_header));
        
        // Initialize intrusive header at beginning of storage
        header_ = reinterpret_cast<buffer_header*>(storage);
        header_->ref_count.store(1, std::memory_order_relaxed);
        
        // Actual data starts after header, which is aligned to max_align_t
        data_ = storage + sizeof(buffer_header);
        total_size_ = size - sizeof(buffer_header);
        
        // Initialize regions based on actual buffer size
        initialize_regions();
    }
    
    virtual ~static_buffer() = default;
    
    // Reference counting
    void acquire() override
    {
        header_->ref_count.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Region management
    size_t region_count() const override { return N_REGIONS; }
    
    const buffer_region* get_regions() const override { return regions_.data(); }
    buffer_region* get_regions() override { return regions_.data(); }
    
    // Compile-time region access with C++20 template syntax
    template<size_t INDEX>
        requires (INDEX < N_REGIONS)  // C++20 requires clause
    buffer_region& get_region()
    {
        return regions_[INDEX];
    }
    
    template<size_t INDEX>
        requires (INDEX < N_REGIONS)
    const buffer_region& get_region() const
    {
        return regions_[INDEX];
    }
    
    // Runtime region access
    buffer_region& get_region(size_t idx)
    {
        assert(idx < N_REGIONS);
        return regions_[idx];
    }
    
    const buffer_region& get_region(size_t idx) const
    {
        assert(idx < N_REGIONS);
        return regions_[idx];
    }
    
    // Region data access
    const char* get_region_data(size_t idx) const override
    {
        assert(idx < N_REGIONS);
        return data_ + regions_[idx].start;
    }
    
    char* get_region_data(size_t idx) override
    {
        assert(idx < N_REGIONS);
        return data_ + regions_[idx].start;
    }
    
    // Compile-time region data access
    template<size_t INDEX>
        requires (INDEX < N_REGIONS)
    char* get_region_data()
    {
        return data_ + regions_[INDEX].start;
    }
    
    template<size_t INDEX>
        requires (INDEX < N_REGIONS)
    const char* get_region_data() const
    {
        return data_ + regions_[INDEX].start;
    }
    
private:
    // Common implementation for writing to a region
    size_t write_to_region(size_t idx, const void* data, size_t len, size_t offset, bool extend_len)
    {
        assert(idx < N_REGIONS);
        auto& region = regions_[idx];
        
        if (offset >= region.capacity) return 0;
        
        size_t available = region.capacity - offset;
        size_t to_copy = std::min(len, available);
        
        if (to_copy > 0)
        {
            std::memcpy(data_ + region.start + offset, data, to_copy);
            
            if (extend_len)
            {
                // For set_region: extend len to cover written area
                region.len = std::max(region.len, offset + to_copy);
            }
            else
            {
                // For append_region: increment len
                region.len += to_copy;
            }
        }
        
        return to_copy;
    }

public:
    // Region operations with C++20 concepts
    template<typename T>
        requires std::is_trivially_copyable_v<T>
    size_t append_region_typed(size_t idx, const T& value)
    {
        return append_region(idx, &value, sizeof(T));
    }
    
    size_t append_region(size_t idx, const void* data, size_t len) override
    {
        assert(idx < N_REGIONS);
        return write_to_region(idx, data, len, regions_[idx].len, false);
    }
    
    // Compile-time append with better type safety
    template<size_t INDEX>
        requires (INDEX < N_REGIONS)
    size_t append_region(const void* data, size_t len)
    {
        return append_region(INDEX, data, len);
    }
    
    // C++20 span-based append
    template<size_t INDEX>
        requires (INDEX < N_REGIONS)
    size_t append_region(std::span<const std::byte> data)
    {
        return append_region(INDEX, data.data(), data.size());
    }
    
    size_t set_region(size_t idx, const void* data, size_t len, size_t offset) override
    {
        return write_to_region(idx, data, len, offset, true);
    }
    
    void clear_region(size_t idx) override
    {
        assert(idx < N_REGIONS);
        regions_[idx].len = 0;
    }
    
    template<size_t INDEX>
        requires (INDEX < N_REGIONS)
    void clear_region()
    {
        regions_[INDEX].len = 0;
    }
    
    void reset() override
    {
        for (auto& region : regions_)
        {
            region.len = 0;
        }
    }
    
    size_t total_size() const override { return total_size_; }
    
    generic_buffer_pool* get_pool() const override { return pool_; }
    
    const char* type_name() const override { return "static_buffer"; }
    
    // Release implementation must be defined after generic_buffer_pool is complete
    void release() override;
    
    // C++20 feature: Get compile-time info about the buffer layout
    static consteval auto get_layout_info()
    {
        return layout_type{};
    }
    
    // Check if a specific region is dynamic (size 0) at compile time
    template<size_t INDEX>
        requires (INDEX < N_REGIONS)
    static consteval bool is_dynamic_region()
    {
        return layout_type::region_sizes[INDEX] == 0;
    }
};

// C++20 deduction guide for easier usage
template<size_t... SIZES>
static_buffer(char*, size_t, generic_buffer_pool*) -> static_buffer<SIZES...>;

// Convenience type aliases with descriptive names
template<size_t HEADER_SIZE, size_t FOOTER_SIZE>
using header_body_footer_buffer = static_buffer<HEADER_SIZE, 0, FOOTER_SIZE>;

template<size_t METADATA_SIZE>
using metadata_buffer = static_buffer<METADATA_SIZE, 0>;

using single_region_buffer = static_buffer<0>;

template<size_t N>
using fixed_region_buffer = static_buffer<N>;

// C++20 concept for buffers with specific region counts
template<typename T, size_t N>
concept HasRegionCount = requires {
    { T::N_REGIONS } -> std::convertible_to<size_t>;
    requires T::N_REGIONS == N;
};

} // namespace slwoggy