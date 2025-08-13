/**
 * @file log_line_impl.hpp
 * @brief Implementation of log_line methods and operators
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include "log_line.hpp"
#include "log_site.hpp"
#include "log_dispatcher.hpp"

namespace slwoggy
{

inline log_line &log_line::operator=(log_line &&other) noexcept
{
    if (this != &other)
    {
        if (buffer_)
        {
            buffer_->finalize();
            log_line_dispatcher::instance().dispatch(*this);
            buffer_->release();
        }

        // Move from other
        buffer_       = std::exchange(other.buffer_, nullptr);
        needs_header_ = std::exchange(other.needs_header_, false);
        level_        = other.level_;
        file_         = other.file_;
        line_         = other.line_;
        timestamp_    = other.timestamp_;
    }
    return *this;
}

inline log_line::~log_line()
{
    if (!buffer_) return;

    // Finalize buffer before dispatching
    buffer_->finalize();

    log_line_dispatcher::instance().dispatch(*this);

    if (buffer_)
    {
        // Release our reference to the buffer
        buffer_->release();
    }
}

inline size_t log_line::write_header()
{
    if (!buffer_) return 0;

    // Use stored timestamp
    auto &dispatcher = log_line_dispatcher::instance();
    int64_t diff_us = std::chrono::duration_cast<std::chrono::microseconds>(timestamp_ - dispatcher.start_time()).count();
    int64_t ms = diff_us / 1000;
    int64_t us = std::abs(diff_us % 1000);

    // Format header: "TTTTTTTT.mmm [LEVEL]    file:line "
    // Note: header doesn't need padding since it's the first line
    size_t text_len_before = buffer_->len();

    // Use fmt::format_to_buffer_with_padding for better performance
    int file_width      = log_site_registry::longest_file();
    int actual_file_len = std::min(file_width, static_cast<int>(file_.size()));

    buffer_->format_to_buffer_with_padding("{:08}.{:03} [{:<5}] {:<10} {:>{}.{}}:{} ",
                                           ms,
                                           us,
                                           log_level_names[static_cast<int>(level_)],
                                           module_.detail->name,
                                           file_.substr(0, actual_file_len),
                                           file_width,
                                           actual_file_len,
                                           line_);

    buffer_->header_width_ = buffer_->len() - text_len_before;
    return buffer_->header_width_;
}

} // namespace slwoggy

namespace
{

// our own endl for log_line
inline slwoggy::log_line &endl(slwoggy::log_line &line)
{
    if (line.buffer_) { line.buffer_->finalize(); }
    slwoggy::log_line_dispatcher::instance().dispatch(line);
    return line;
}

} // namespace