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
#include <cstring>
#include <cassert>
#include <thread>

namespace slwoggy
{

inline log_line_base &log_line_base::operator=(log_line_base &&other) noexcept
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
    }
    return *this;
}

inline log_line_base::~log_line_base()
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

inline size_t log_line_headered::write_header()
{
    if (!buffer_) return 0;

    // Use stored timestamp
    auto &dispatcher = log_line_dispatcher::instance();
    int64_t diff_us =
        std::chrono::duration_cast<std::chrono::microseconds>(buffer_->timestamp_ - dispatcher.start_time()).count();
    int64_t ms = diff_us / 1000;
    int64_t us = std::abs(diff_us % 1000);

    // Format header: "TTTTTTTT.mmm [LEVEL] [thread_id] module    file:line "
    // Note: header doesn't need padding since it's the first line
    size_t text_len_before = buffer_->len();

    // Use fmt::format_to_buffer_with_padding for better performance
    int file_width      = log_site_registry::longest_file();
    int actual_file_len = std::min(file_width, static_cast<int>(buffer_->file_.size()));

    // Hash the thread ID to get a shorter representation
    size_t thread_hash = std::hash<std::thread::id>{}(buffer_->owner_thread_id_);
    
    buffer_->format_to_buffer_with_padding("{:08}.{:03} [{:<5}] [{:08x}] {:<10} {:>{}.{}}:{} ",
                                           ms,
                                           us,
                                           log_level_names[static_cast<int>(buffer_->level_)],
                                           static_cast<uint32_t>(thread_hash & 0xFFFFFFFF), // Use lower 32 bits for shorter display
                                           buffer_->module_->name,
                                           buffer_->file_.substr(0, actual_file_len),
                                           file_width,
                                           actual_file_len,
                                           buffer_->line_);

    buffer_->header_width_ = buffer_->len() - text_len_before;
    return buffer_->header_width_;
}

inline size_t log_line_structured::write_header()
{

    if (!buffer_) return 0;

    // add msg=" as prefix
    size_t text_len_before = buffer_->len();
    buffer_->write_raw("msg=\"");
    buffer_->header_width_ = buffer_->len() - text_len_before;
    return buffer_->header_width_;
}

inline size_t log_line_base::format_hex_line_inline(char *buffer, size_t buffer_size, const uint8_t *bytes, size_t byte_count, const hex_inline_config &config)
{
    // Use fmt to format into the provided buffer
    auto out         = buffer;
    size_t remaining = buffer_size;

    for (size_t i = 0; i < byte_count && remaining > 0; i++)
    {
        // Add separator before byte (except first)
        if (i > 0 && config.separator[0] != '\0')
        {
            auto sep_result = fmt::format_to_n(out, remaining, "{}", config.separator);
            size_t written  = sep_result.out - out;
            out             = sep_result.out;
            remaining       = (written < remaining) ? remaining - written : 0;
        }

        // Format the byte with brackets and prefix/suffix
        if (remaining > 0)
        {
            auto byte_result = fmt::format_to_n(out,
                                                remaining,
                                                "{}{}{:02x}{}{}",
                                                config.left_bracket,
                                                config.prefix,
                                                bytes[i],
                                                config.suffix,
                                                config.right_bracket);
            size_t written   = byte_result.out - out;
            out              = byte_result.out;
            remaining        = (written < remaining) ? remaining - written : 0;
        }
    }

    // Return number of bytes written
    return out - buffer;
}

inline size_t
log_line_base::format_hex_line_formatted(char *buffer, size_t buffer_size, const uint8_t *bytes, size_t byte_count, size_t offset, bool include_ascii)
{
    // Build the hex line using fmt
    fmt::memory_buffer temp_buf;

    // Format offset
    fmt::format_to(std::back_inserter(temp_buf), "{:04x}: ", offset);

    // Hex bytes with grouping
    for (size_t i = 0; i < HEX_BYTES_PER_LINE; i++)
    {
        if (i < byte_count) { fmt::format_to(std::back_inserter(temp_buf), "{:02x} ", bytes[i]); }
        else { fmt::format_to(std::back_inserter(temp_buf), "   "); }

        // Add extra space for grouping
        if (i == 3 || i == 7 || i == 11) { fmt::format_to(std::back_inserter(temp_buf), " "); }
    }

    // ASCII representation if requested
    if (include_ascii)
    {
        fmt::format_to(std::back_inserter(temp_buf), " |");
        for (size_t i = 0; i < byte_count; i++)
        {
            uint8_t c = bytes[i];
            if (c >= 32 && c < 127) { fmt::format_to(std::back_inserter(temp_buf), "{:c}", static_cast<char>(c)); }
            else { fmt::format_to(std::back_inserter(temp_buf), "."); }
        }
        fmt::format_to(std::back_inserter(temp_buf), "|");
    }

    // Copy to output buffer
    size_t bytes_to_copy = std::min(temp_buf.size(), buffer_size);
    std::memcpy(buffer, temp_buf.data(), bytes_to_copy);

    return bytes_to_copy;
}

inline size_t log_line_base::hex_dump_inline_impl(const uint8_t *bytes, size_t len, const hex_inline_config &config)
{
    if (!buffer_ || !bytes || len == 0) return 0;

    ensure_header_written();

    // Buffer for formatting - need bigger for prefixes/suffixes
    char line_buf[512];

    // For inline format, dump all bytes in one continuous line (no newlines)
    size_t total_dumped = 0;

    while (total_dumped < len)
    {
        // Format as many bytes as fit in buffer
        size_t chunk_size = std::min(len - total_dumped, size_t(64)); // Reasonable chunk
        size_t formatted_len = format_hex_line_inline(line_buf, sizeof(line_buf), bytes + total_dumped, chunk_size, config);

        // Write without adding newline
        buffer_->write_raw(std::string_view(line_buf, formatted_len));

        total_dumped += chunk_size;

        // If buffer getting full, might need to continue in next buffer
        if (buffer_->available() < 100 && total_dumped < len)
        {
            break; // Let caller handle continuation
        }
    }

    return total_dumped;
}

inline size_t log_line_base::hex_dump_formatted_impl(const uint8_t *bytes, size_t len, bool include_ascii, size_t max_lines)
{
    // Simple version for hex_dump_best_effort - always starts at offset 0
    return hex_dump_formatted_full_impl(bytes, len, 0, len, include_ascii, max_lines);
}

inline size_t
log_line_base::hex_dump_formatted_full_impl(const uint8_t *bytes, size_t len, size_t start_offset, size_t total_len, bool include_ascii, size_t max_lines)
{
    if (!buffer_ || !bytes || len == 0) return 0;

    ensure_header_written();

    // Buffer for formatting a single line
    char line_buf[128];

    // Track previous line for duplicate detection
    uint8_t prev_line[HEX_BYTES_PER_LINE] = {0};
    bool prev_line_valid                  = false;
    bool in_star_mode                     = false;
    size_t last_shown_offset = start_offset; // Track the last offset we actually showed (initialize to start_offset to
                                             // prevent underflow)

    // First, write the header with progress
    fmt::memory_buffer header_buf;
    fmt::format_to(std::back_inserter(header_buf), "binary data len: {}/{}", start_offset, total_len);
    buffer_->write_raw(std::string_view(header_buf.data(), header_buf.size()));

    size_t bytes_dumped  = 0;
    size_t lines_written = 0;

    while (bytes_dumped < len && lines_written < max_lines)
    {
        size_t line_offset = start_offset + bytes_dumped; // Use absolute offset
        size_t line_bytes  = std::min(HEX_BYTES_PER_LINE, len - bytes_dumped);

        // Check for duplicate line
        bool is_duplicate = false;
        if (prev_line_valid && line_bytes == HEX_BYTES_PER_LINE)
        {
            is_duplicate = (memcmp(bytes + bytes_dumped, prev_line, HEX_BYTES_PER_LINE) == 0);
        }

        if (is_duplicate)
        {
            if (!in_star_mode)
            {
                // Calculate exact space needed: "*" (1) + "\n" (1) + padding (buffer_->header_width_)
                size_t space_needed = 2; // "*\n"
                if (buffer_->is_padding_enabled()) { space_needed += buffer_->header_width_; }

                if (buffer_->available() < space_needed)
                {
                    break; // Not enough space, stop here
                }

                // Write star with padding
                buffer_->write_with_padding("*", true);
                in_star_mode = true;
                lines_written++;
            }
            // Keep updating last_shown_offset as we skip duplicates
            last_shown_offset = start_offset + bytes_dumped; // Absolute offset
            bytes_dumped += line_bytes;
            continue;
        }

        // If we were in star mode, write the last duplicate line
        if (in_star_mode && last_shown_offset < start_offset + bytes_dumped)
        {
            // Assert invariant: last_shown_offset should always be >= start_offset
            assert(last_shown_offset >= start_offset);
            // Format the line first to see its exact size
            size_t line_len = format_hex_line_formatted(line_buf,
                                                        sizeof(line_buf),
                                                        bytes + (last_shown_offset - start_offset),
                                                        HEX_BYTES_PER_LINE,
                                                        last_shown_offset,
                                                        include_ascii);

            // Calculate exact space needed: line + "\n" (1) + padding (buffer_->header_width_ if enabled)
            size_t space_needed = line_len + 1;
            if (buffer_->is_padding_enabled()) { space_needed += buffer_->header_width_; }

            if (buffer_->available() < space_needed)
            {
                // Restore star mode state and break
                in_star_mode = true;
                bytes_dumped = last_shown_offset - start_offset;
                break;
            }

            buffer_->write_with_padding(std::string_view(line_buf, line_len), true);
            lines_written++;
        }

        in_star_mode = false;

        // Format the line first to check its exact size
        size_t line_len = format_hex_line_formatted(line_buf, sizeof(line_buf), bytes + bytes_dumped, line_bytes, line_offset, include_ascii);

        // Calculate exact space needed: line + "\n" (1) + padding (buffer_->header_width_ if enabled)
        size_t space_needed = line_len + 1;
        if (buffer_->is_padding_enabled()) { space_needed += buffer_->header_width_; }

        if (buffer_->available() < space_needed)
        {
            break; // Not enough space for complete line, stop here
        }

        // Copy current line for next comparison
        if (line_bytes == HEX_BYTES_PER_LINE)
        {
            memcpy(prev_line, bytes + bytes_dumped, HEX_BYTES_PER_LINE);
            prev_line_valid = true;
        }

        // Write the current line with absolute offset
        buffer_->write_with_padding(std::string_view(line_buf, line_len), true);

        last_shown_offset = start_offset + bytes_dumped; // Absolute offset
        bytes_dumped += line_bytes;
        lines_written++;
    }

    // If we ended in star mode, show the last duplicate line
    if (in_star_mode && last_shown_offset < start_offset + len)
    {
        assert(last_shown_offset >= start_offset); // Invariant: no underflow
        size_t final_offset = last_shown_offset - start_offset;
        size_t final_bytes  = std::min(HEX_BYTES_PER_LINE, len - final_offset);
        size_t line_len = format_hex_line_formatted(line_buf, sizeof(line_buf), bytes + final_offset, final_bytes, last_shown_offset, include_ascii);

        // Calculate exact space needed: line + "\n" (1) + padding (buffer_->header_width_ if enabled)
        size_t space_needed = line_len + 1;
        if (buffer_->is_padding_enabled()) { space_needed += buffer_->header_width_; }

        // Only write if we have enough space
        if (buffer_->available() >= space_needed)
        {
            buffer_->write_with_padding(std::string_view(line_buf, line_len), true);
        }
        else
        {
            // Not enough space, revert to where we were
            bytes_dumped = last_shown_offset - start_offset;
        }
    }

    return bytes_dumped;
}

inline size_t log_line_base::hex_dump_best_effort(const void *data, size_t len, hex_dump_format format, size_t max_lines, const hex_inline_config &inline_config)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);

    if (format == hex_dump_format::inline_hex) { return hex_dump_inline_impl(bytes, len, inline_config); }
    else
    {
        bool include_ascii = (format == hex_dump_format::full);
        return hex_dump_formatted_impl(bytes, len, include_ascii, max_lines);
    }
}

inline void log_line_base::hex_dump_full(const void *data, size_t len, hex_dump_format format, const hex_inline_config &inline_config)
{
    if (!data || len == 0) return;

    size_t offset = 0;
    while (offset < len)
    {
        // Pass offset and total length for progress reporting
        size_t dumped;
        if (format == hex_dump_format::inline_hex)
        {
            dumped = hex_dump_inline_impl(static_cast<const uint8_t *>(data) + offset, len - offset, inline_config);
        }
        else
        {
            // For formatted dumps, we need to pass the starting offset
            dumped = hex_dump_formatted_full_impl(static_cast<const uint8_t *>(data) + offset,
                                                  len - offset,
                                                  offset, // Starting offset for this chunk
                                                  len,    // Total length for progress display
                                                  format == hex_dump_format::full,
                                                  64);
        }

        if (dumped == 0) break; // Buffer full or error
        offset += dumped;

        // If we haven't dumped everything, swap buffers and continue
        if (offset < len) { flush(); }
    }
}

// our own endl for log_line
inline log_line_base &log_line_base::flush()
{
    if (buffer_) { buffer_->finalize(); }
    slwoggy::log_line_dispatcher::instance().dispatch(*this);
    return *this;
}

} // namespace slwoggy

namespace
{

// our own endl for log_line
inline slwoggy::log_line_base &endl(slwoggy::log_line_base &line)
{
    if (line.buffer_) { line.buffer_->finalize(); }
    slwoggy::log_line_dispatcher::instance().dispatch(line);
    return line;
}

} // namespace