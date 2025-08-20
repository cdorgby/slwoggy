/**
 * @file log_sink_filters.hpp
 * @brief Per-sink filter interfaces for selective log processing
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include "log_types.hpp"
#include "log_buffer.hpp"
#include <vector>
#include <string>
#include <memory>
#include <algorithm>

namespace slwoggy
{

/**
 * @brief Base filter that accepts all buffers (no filtering)
 * 
 * This is the default filter used when no specific filter is provided.
 * It has zero overhead as the compiler can inline and optimize away the check.
 */
struct no_filter final
{
    /**
     * @brief Always returns true - no filtering
     * @param buffer The buffer to check (unused)
     * @return Always true
     */
    bool should_process(const log_buffer_base*) const noexcept { return true; }
};

/**
 * @brief Filter based on minimum log level
 * 
 * Only processes buffers with log level >= min_level.
 * Useful for console sinks that only show warnings and errors.
 * 
 * @code
 * auto console_sink = make_stdout_sink(level_filter{log_level::warn});
 * // Only warnings, errors, and fatal messages go to console
 * @endcode
 */
struct level_filter final
{
    log_level min_level;
    
    /**
     * @brief Check if buffer meets minimum level requirement
     * @param buffer The buffer to check
     * @return true if buffer level >= min_level
     */
    bool should_process(const log_buffer_base* buffer) const noexcept
    {
        if (!buffer) [[unlikely]] return false;
        return buffer->level_ >= min_level;
    }
};

/**
 * @brief Filter based on maximum log level
 * 
 * Only processes buffers with log level <= max_level.
 * Useful for debug sinks that should not show errors (which might go elsewhere).
 * 
 * @code
 * auto debug_sink = make_file_sink("debug.log", max_level_filter{log_level::debug});
 * // Only trace and debug messages go to debug log
 * @endcode
 */
struct max_level_filter final
{
    log_level max_level;
    
    /**
     * @brief Check if buffer is at or below maximum level
     * @param buffer The buffer to check
     * @return true if buffer level <= max_level
     */
    bool should_process(const log_buffer_base* buffer) const noexcept
    {
        if (!buffer) [[unlikely]] return false;
        return buffer->level_ <= max_level;
    }
};

/**
 * @brief Filter based on log level range
 * 
 * Only processes buffers with min_level <= level <= max_level.
 * Useful for separating different severity levels to different files.
 * 
 * @code
 * auto info_sink = make_file_sink("info.log", 
 *     level_range_filter{log_level::debug, log_level::info});
 * // Only debug and info messages go to info.log
 * @endcode
 */
struct level_range_filter final
{
    log_level min_level;
    log_level max_level;
    
    /**
     * @brief Check if buffer is within level range
     * @param buffer The buffer to check
     * @return true if min_level <= buffer level <= max_level
     */
    bool should_process(const log_buffer_base* buffer) const noexcept
    {
        if (!buffer) [[unlikely]] return false;
        return buffer->level_ >= min_level && buffer->level_ <= max_level;
    }
};

/**
 * @brief Filter based on module name
 * 
 * Only processes buffers from specific modules.
 * Module names must match exactly (case-sensitive).
 * 
 * @code
 * auto network_sink = make_file_sink("network.log", 
 *     module_filter{{"network", "http", "websocket"}});
 * // Only logs from network, http, and websocket modules
 * @endcode
 */
struct module_filter final
{
    std::vector<std::string> allowed_modules;
    
    /**
     * @brief Check if buffer is from an allowed module
     * @param buffer The buffer to check
     * @return true if buffer's module is in allowed list
     * 
     * @note This requires extracting module info from buffer metadata,
     *       which may have a small performance cost
     */
    bool should_process(const log_buffer_base* buffer) const noexcept
    {
        // TODO: Implement module filtering when we have access to module info
        // For now, accept all buffers (no filtering)
        return buffer != nullptr;
    }
};

/**
 * @brief Composite filter that requires ALL sub-filters to pass
 * 
 * Combines multiple filters with AND logic.
 * Buffer is processed only if all filters return true.
 * 
 * @code
 * // Only ERROR and above from network module
 * auto filter = and_filter{}
 *     .add(level_filter{log_level::error})
 *     .add(module_filter{{"network"}});
 * @endcode
 */
struct and_filter final
{
    std::vector<std::shared_ptr<void>> filters;
    std::vector<bool (*)(const void*, const log_buffer_base*)> checkers;
    
    /**
     * @brief Add a filter to the AND chain
     * @tparam Filter The filter type
     * @param filter The filter to add
     * @return Reference to this for chaining
     */
    template<typename Filter>
    and_filter& add(Filter filter)
    {
        filters.reserve(filters.size() + 1);
        checkers.reserve(checkers.size() + 1);
        
        auto ptr = std::make_shared<Filter>(std::move(filter));
        filters.push_back(ptr);
        
        // Store a type-erased checker function
        checkers.push_back([](const void* f, const log_buffer_base* buf) -> bool {
            return static_cast<const Filter*>(f)->should_process(buf);
        });
        
        return *this;
    }
    
    /**
     * @brief Check if buffer passes ALL filters
     * @param buffer The buffer to check
     * @return true if all filters return true
     */
    bool should_process(const log_buffer_base* buffer) const noexcept
    {
        if (!buffer) [[unlikely]] return false;
        
        // Empty AND filter accepts all (all zero conditions are met)
        if (filters.empty()) return true;
        
        for (size_t i = 0; i < filters.size(); ++i)
        {
            if (!checkers[i](filters[i].get(), buffer))
                return false;
        }
        return true;
    }
};

/**
 * @brief Composite filter that requires ANY sub-filter to pass
 * 
 * Combines multiple filters with OR logic.
 * Buffer is processed if any filter returns true.
 * 
 * @code
 * // Accept errors from anywhere OR anything from debug module
 * auto filter = or_filter{}
 *     .add(level_filter{log_level::error})
 *     .add(module_filter{{"debug"}});
 * @endcode
 */
struct or_filter final
{
    std::vector<std::shared_ptr<void>> filters;
    std::vector<bool (*)(const void*, const log_buffer_base*)> checkers;
    
    /**
     * @brief Add a filter to the OR chain
     * @tparam Filter The filter type
     * @param filter The filter to add
     * @return Reference to this for chaining
     */
    template<typename Filter>
    or_filter& add(Filter filter)
    {
        filters.reserve(filters.size() + 1);
        checkers.reserve(checkers.size() + 1);
        
        auto ptr = std::make_shared<Filter>(std::move(filter));
        filters.push_back(ptr);
        
        // Store a type-erased checker function
        checkers.push_back([](const void* f, const log_buffer_base* buf) -> bool {
            return static_cast<const Filter*>(f)->should_process(buf);
        });
        
        return *this;
    }
    
    /**
     * @brief Check if buffer passes ANY filter
     * @param buffer The buffer to check
     * @return true if any filter returns true
     */
    bool should_process(const log_buffer_base* buffer) const noexcept
    {
        if (!buffer) [[unlikely]] return false;
        
        // Empty OR filter rejects all (no conditions to meet)
        if (filters.empty()) return false;
        
        for (size_t i = 0; i < filters.size(); ++i)
        {
            if (checkers[i](filters[i].get(), buffer))
                return true;
        }
        return false;
    }
};

/**
 * @brief Inverted filter - processes what the wrapped filter rejects
 * 
 * Useful for creating "everything except" filters.
 * 
 * @code
 * // Everything except debug messages
 * auto filter = not_filter{level_filter{log_level::debug}};
 * @endcode
 */
template<typename Filter>
struct not_filter final
{
    Filter wrapped;
    
    /**
     * @brief Check if buffer should NOT be processed by wrapped filter
     * @param buffer The buffer to check
     * @return Opposite of what wrapped filter returns
     */
    bool should_process(const log_buffer_base* buffer) const noexcept
    {
        return !wrapped.should_process(buffer);
    }
};

} // namespace slwoggy