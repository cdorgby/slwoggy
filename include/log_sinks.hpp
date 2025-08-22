/**
 * @file log_sinks.hpp
 * @brief Factory functions for creating common log sinks with optional filtering
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include "log_sink.hpp"
#include "log_formatters.hpp"
#include "log_writers.hpp"
#include "log_sink_filters.hpp"
#include <string_view>
#include <type_traits>

namespace slwoggy
{

// Factory functions with optional filtering support via default parameter

template <typename Filter = no_filter> inline std::shared_ptr<log_sink> make_stdout_sink(Filter &&filter = {})
{
    if constexpr (std::is_same_v<std::decay_t<Filter>, no_filter>)
    {
        return std::make_shared<log_sink>(
            raw_formatter{
                .use_color   = true,
                .add_newline = true,
            },
            file_writer{STDOUT_FILENO});
    }
    else
    {
        return std::make_shared<log_sink>(
            raw_formatter{
                .use_color   = true,
                .add_newline = true,
            },
            file_writer{STDOUT_FILENO},
            std::forward<Filter>(filter));
    }
}

template <typename Filter = no_filter>
inline std::shared_ptr<log_sink> make_raw_file_sink(const std::string_view &filename, rotate_policy policy = {}, Filter &&filter = {})
{
    if constexpr (std::is_same_v<std::decay_t<Filter>, no_filter>)
    {
        return std::make_shared<log_sink>(
            raw_formatter{
                .use_color   = false,
                .add_newline = true,
            },
            file_writer{std::string(filename), policy});
    }
    else
    {
        return std::make_shared<log_sink>(
            raw_formatter{
                .use_color   = false,
                .add_newline = true,
            },
            file_writer{std::string(filename), policy},
            std::forward<Filter>(filter));
    }
}

template <typename Filter = no_filter>
inline std::shared_ptr<log_sink> make_writev_file_sink(const std::string_view &filename, rotate_policy policy = {}, Filter &&filter = {})
{
    if constexpr (std::is_same_v<std::decay_t<Filter>, no_filter>)
    {
        return std::make_shared<log_sink>(
            raw_formatter{
                .use_color   = false,
                .add_newline = true,
            },
            writev_file_writer{std::string(filename), policy});
    }
    else
    {
        return std::make_shared<log_sink>(
            raw_formatter{
                .use_color   = false,
                .add_newline = true,
            },
            writev_file_writer{std::string(filename), policy},
            std::forward<Filter>(filter));
    }
}

template <typename Filter = no_filter>
inline std::shared_ptr<log_sink> make_json_sink(const std::string_view &filename, rotate_policy policy = {}, Filter &&filter = {})
{
    if constexpr (std::is_same_v<std::decay_t<Filter>, no_filter>)
    {
        return std::make_shared<log_sink>(
            taocpp_json_formatter{
                .pretty_print = false,
                .add_newline  = true,
            },
            file_writer{std::string(filename), policy});
    }
    else
    {
        return std::make_shared<log_sink>(
            taocpp_json_formatter{
                .pretty_print = false,
                .add_newline  = true,
            },
            file_writer{std::string(filename), policy},
            std::forward<Filter>(filter));
    }
}

} // namespace slwoggy