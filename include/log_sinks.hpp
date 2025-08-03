/**
 * @file log_sinks.hpp
 * @brief Factory functions for creating common log sinks
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#pragma once

#include "log_sink.hpp"
#include "log_formatters.hpp"
#include "log_writers.hpp"
#include <string_view>

namespace slwoggy {

inline std::shared_ptr<log_sink> make_stdout_sink()
{
    return std::make_shared<log_sink>(
        raw_formatter{true, true},
        file_writer{STDOUT_FILENO}
    );
}

inline std::shared_ptr<log_sink> make_raw_file_sink(const std::string_view &filename)
{
    return std::make_shared<log_sink>(
        raw_formatter{false, true},
        file_writer{std::string(filename)}
    );
}

inline std::shared_ptr<log_sink> make_writev_file_sink(const std::string_view &filename)
{
    return std::make_shared<log_sink>(
        raw_formatter{true, true},
        writev_file_writer{std::string(filename)}
    );
}

inline std::shared_ptr<log_sink> make_json_sink(const std::string_view &filename)
{
    return std::make_shared<log_sink>(
        taocpp_json_formatter{false, true},
        file_writer{std::string(filename)}
    );
}

} // namespace slwoggy