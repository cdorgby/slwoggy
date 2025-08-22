/**
 * @file log_filter.hpp
 * @brief Log filter interface and base classes
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include "log_buffer.hpp"
#include <memory>
#include <vector>

namespace slwoggy
{

/**
 * @brief Base class for log filters
 *
 * Filters process buffers in batches and set the filtered_ flag on buffers
 * that should be dropped. The dispatcher handles all buffer lifecycle.
 */
class log_filter
{
  public:
    /**
     * @brief Process a batch of buffers and mark filtered ones
     *
     * Sets the filtered_ flag to true on buffers that should be dropped.
     * The dispatcher will skip these buffers when sending to sinks.
     *
     * @param buffers Array of buffer pointers (may contain nulls)
     * @param count Number of buffers in array
     *
     * @note Filters may call add_ref()/release() if they need to hold buffers
     * @note Filters directly modify buffer->filtered_ flag
     * @note nullptr buffers and flush markers should not be modified
     */
    virtual void process_batch(log_buffer_base **buffers, size_t count) = 0;

    /**
     * @brief Called when filter is being removed
     *
     * Used for cleanup of any internal state.
     * Filters holding buffer references must release them here.
     */
    virtual void shutdown() = 0;

    /**
     * @brief Get filter name for debugging/logging
     */
    virtual const char *name() const = 0;

    /**
     * @brief Reset filter state (for testing)
     */
    virtual void reset() {}

    virtual ~log_filter() = default;
};

/**
 * @brief Configuration for filters (immutable for RCU)
 */
struct filter_config
{
    std::vector<std::shared_ptr<log_filter>> filters;

    /**
     * @brief Create a copy for RCU update
     */
    std::unique_ptr<filter_config> copy() const
    {
        auto new_config     = std::make_unique<filter_config>();
        new_config->filters = filters; // Copies shared_ptrs
        return new_config;
    }
};

} // namespace slwoggy