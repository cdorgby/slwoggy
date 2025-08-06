/**
 * @file buffer_concept.hpp
 * @brief Abstract buffer interface for generic buffer system
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace slwoggy
{

// Forward declaration
class generic_buffer_pool;

/**
 * @brief Region descriptor for buffer memory areas
 */
struct buffer_region
{
    size_t start;           ///< Offset from buffer start
    size_t capacity;        ///< Maximum size of region
    size_t len;            ///< Current data length in region
    bool include_in_output; ///< Should this region be included in sink output?
};

/**
 * @brief Abstract interface for all buffer types
 * 
 * This interface defines the contract that all buffer implementations must follow.
 * It provides region-based memory management with support for type erasure.
 */
struct buffer_concept
{
    virtual ~buffer_concept() = default;
    
    // Reference counting
    virtual void acquire() = 0;
    virtual void release() = 0;
    
    // Region management
    virtual size_t region_count() const = 0;
    virtual const buffer_region* get_regions() const = 0;
    virtual buffer_region* get_regions() = 0;
    
    // Region data access
    virtual const char* get_region_data(size_t idx) const = 0;
    virtual char* get_region_data(size_t idx) = 0;
    
    // Region operations
    virtual size_t append_region(size_t idx, const void* data, size_t len) = 0;
    virtual size_t set_region(size_t idx, const void* data, size_t len, size_t offset) = 0;
    virtual void clear_region(size_t idx) = 0;
    
    // Buffer management
    virtual void reset() = 0;
    virtual size_t total_size() const = 0;
    
    // Pool reference
    virtual generic_buffer_pool* get_pool() const = 0;
    
    // Type identification (for debugging/stats)
    virtual const char* type_name() const = 0;
};

} // namespace slwoggy