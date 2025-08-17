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
        level_        = other.level_;
        file_         = other.file_;
        line_         = other.line_;
        timestamp_    = other.timestamp_;
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

inline size_t log_line_structured::write_header()
{
    
    if (!buffer_) return 0;

    // add msg=" as prefix
    size_t text_len_before = buffer_->len();
    buffer_->write_raw("msg=\"");
    buffer_->header_width_ = buffer_->len() - text_len_before;
    return buffer_->header_width_;
}

inline size_t log_line_base::format_hex_line_inline(char* buffer, size_t buffer_size, 
                                                    const uint8_t* bytes, size_t byte_count,
                                                    const hex_inline_config& config)
{
    char* ptr = buffer;
    char* end = buffer + buffer_size;
    
    // Format with configurable prefix, suffix, separator and brackets
    for (size_t i = 0; i < byte_count; i++) {
        // Add separator before byte (except first)
        if (i > 0 && config.separator[0] != '\0') {
            size_t sep_len = strlen(config.separator);
            if (ptr + sep_len <= end) {
                memcpy(ptr, config.separator, sep_len);
                ptr += sep_len;
            }
        }
        
        // Add left bracket
        if (config.left_bracket[0] != '\0') {
            size_t len = strlen(config.left_bracket);
            if (ptr + len <= end) {
                memcpy(ptr, config.left_bracket, len);
                ptr += len;
            }
        }
        
        // Add prefix
        if (config.prefix[0] != '\0') {
            size_t len = strlen(config.prefix);
            if (ptr + len <= end) {
                memcpy(ptr, config.prefix, len);
                ptr += len;
            }
        }
        
        // Add hex byte
        if (ptr + 2 <= end) {
            ptr += snprintf(ptr, end - ptr, "%02x", bytes[i]);
        }
        
        // Add suffix
        if (config.suffix[0] != '\0') {
            size_t len = strlen(config.suffix);
            if (ptr + len <= end) {
                memcpy(ptr, config.suffix, len);
                ptr += len;
            }
        }
        
        // Add right bracket
        if (config.right_bracket[0] != '\0') {
            size_t len = strlen(config.right_bracket);
            if (ptr + len <= end) {
                memcpy(ptr, config.right_bracket, len);
                ptr += len;
            }
        }
    }
    
    // No newline for inline format!
    return ptr - buffer;
}

inline size_t log_line_base::format_hex_line_formatted(char* buffer, size_t buffer_size,
                                                       const uint8_t* bytes, size_t byte_count,
                                                       size_t offset, bool include_ascii)
{
    char* ptr = buffer;
    char* end = buffer + buffer_size;
    
    // Offset
    ptr += snprintf(ptr, end - ptr, "%04zx: ", offset);
    
    // Hex bytes with grouping
    for (size_t i = 0; i < 16; i++) {
        if (i < byte_count) {
            ptr += snprintf(ptr, end - ptr, "%02x ", bytes[i]);
        } else {
            ptr += snprintf(ptr, end - ptr, "   ");
        }
        
        // Add extra space for grouping
        if (i == 3 || i == 7 || i == 11) {
            if (ptr < end) *ptr++ = ' ';
        }
    }
    
    // ASCII representation if requested
    if (include_ascii) {
        ptr += snprintf(ptr, end - ptr, " |");
        for (size_t i = 0; i < byte_count; i++) {
            uint8_t c = bytes[i];
            if (c >= 32 && c < 127) {
                if (ptr < end) *ptr++ = c;
            } else {
                if (ptr < end) *ptr++ = '.';
            }
        }
        if (ptr < end) *ptr++ = '|';
    }
    
    // Don't add newline here - caller will handle it
    return ptr - buffer;
}

inline size_t log_line_base::hex_dump_inline_impl(const uint8_t* bytes, size_t len, size_t max_lines,
                                                  const hex_inline_config& config)
{
    if (!buffer_ || !bytes || len == 0) return 0;
    
    ensure_header_written();
    
    // Buffer for formatting - need bigger for prefixes/suffixes
    char line_buf[512];
    
    // For inline format, dump all bytes in one continuous line (no newlines)
    size_t total_dumped = 0;
    
    while (total_dumped < len) {
        // Format as many bytes as fit in buffer
        size_t chunk_size = std::min(len - total_dumped, size_t(64)); // Reasonable chunk
        size_t formatted_len = format_hex_line_inline(line_buf, sizeof(line_buf),
                                                      bytes + total_dumped, chunk_size,
                                                      config);
        
        // Write without adding newline
        buffer_->write_raw(std::string_view(line_buf, formatted_len));
        
        total_dumped += chunk_size;
        
        // If buffer getting full, might need to continue in next buffer
        if (buffer_->available() < 100 && total_dumped < len) {
            break; // Let caller handle continuation
        }
    }
    
    return total_dumped;
}

inline size_t log_line_base::hex_dump_formatted_impl(const uint8_t* bytes, size_t len, 
                                                     bool include_ascii, size_t max_lines)
{
    // Simple version for hex_dump_best_effort - always starts at offset 0
    return hex_dump_formatted_full_impl(bytes, len, 0, len, include_ascii, max_lines);
}

inline size_t log_line_base::hex_dump_formatted_full_impl(const uint8_t* bytes, size_t len,
                                                          size_t start_offset, size_t total_len,
                                                          bool include_ascii, size_t max_lines)
{
    if (!buffer_ || !bytes || len == 0) return 0;

    ensure_header_written();

    // Buffer for formatting a single line
    char line_buf[128];

    // Track previous line for duplicate detection
    uint8_t prev_line[16]    = {0};
    bool prev_line_valid     = false;
    bool in_star_mode        = false;
    size_t last_shown_offset = 0;  // Track the last offset we actually showed

    // First, write the header with progress
    int header_len = snprintf(line_buf, sizeof(line_buf), "binary data len: %zu/%zu", start_offset, total_len);
    buffer_->write_raw(std::string_view(line_buf, header_len));

    size_t bytes_dumped  = 0;
    size_t lines_written = 0;

    while (bytes_dumped < len && lines_written < max_lines)
    {
        size_t line_offset = start_offset + bytes_dumped;  // Use absolute offset
        size_t line_bytes  = std::min(size_t(16), len - bytes_dumped);

        // Check for duplicate line
        bool is_duplicate = false;
        if (prev_line_valid && line_bytes == 16) { is_duplicate = (memcmp(bytes + bytes_dumped, prev_line, 16) == 0); }

        if (is_duplicate)
        {
            if (!in_star_mode)
            {
                // Calculate exact space needed: "*" (1) + "\n" (1) + padding (buffer_->header_width_)
                size_t space_needed = 2;  // "*\n"
                if (buffer_->is_padding_enabled()) {
                    space_needed += buffer_->header_width_;
                }
                
                if (buffer_->available() < space_needed) {
                    break;  // Not enough space, stop here
                }
                
                // Write star with padding
                buffer_->write_with_padding("*", true);
                in_star_mode = true;
                lines_written++;
            }
            // Keep updating last_shown_offset as we skip duplicates
            last_shown_offset = start_offset + bytes_dumped;  // Absolute offset
            bytes_dumped += line_bytes;
            continue;
        }

        // If we were in star mode, write the last duplicate line
        if (in_star_mode && last_shown_offset < start_offset + bytes_dumped)
        {
            // Format the line first to see its exact size
            size_t line_len = format_hex_line_formatted(line_buf, sizeof(line_buf), 
                                                       bytes + (last_shown_offset - start_offset), 16,
                                                       last_shown_offset, include_ascii);
            
            // Calculate exact space needed: line + "\n" (1) + padding (buffer_->header_width_ if enabled)
            size_t space_needed = line_len + 1;
            if (buffer_->is_padding_enabled()) {
                space_needed += buffer_->header_width_;
            }
            
            if (buffer_->available() < space_needed) {
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
        size_t line_len = format_hex_line_formatted(line_buf, sizeof(line_buf), 
                                                   bytes + bytes_dumped, line_bytes, 
                                                   line_offset, include_ascii);
        
        // Calculate exact space needed: line + "\n" (1) + padding (buffer_->header_width_ if enabled)
        size_t space_needed = line_len + 1;
        if (buffer_->is_padding_enabled()) {
            space_needed += buffer_->header_width_;
        }
        
        if (buffer_->available() < space_needed) {
            break;  // Not enough space for complete line, stop here
        }

        // Copy current line for next comparison
        if (line_bytes == 16)
        {
            memcpy(prev_line, bytes + bytes_dumped, 16);
            prev_line_valid = true;
        }

        // Write the current line with absolute offset
        buffer_->write_with_padding(std::string_view(line_buf, line_len), true);

        last_shown_offset = start_offset + bytes_dumped;  // Absolute offset
        bytes_dumped += line_bytes;
        lines_written++;
    }

    // If we ended in star mode, show the last duplicate line
    if (in_star_mode && last_shown_offset < start_offset + len)
    {
        size_t final_offset = last_shown_offset - start_offset;
        size_t final_bytes = std::min(size_t(16), len - final_offset);
        size_t line_len = format_hex_line_formatted(line_buf, sizeof(line_buf), 
                                                   bytes + final_offset, final_bytes,
                                                   last_shown_offset, include_ascii);
        
        // Calculate exact space needed: line + "\n" (1) + padding (buffer_->header_width_ if enabled)
        size_t space_needed = line_len + 1;
        if (buffer_->is_padding_enabled()) {
            space_needed += buffer_->header_width_;
        }
        
        // Only write if we have enough space
        if (buffer_->available() >= space_needed) {
            buffer_->write_with_padding(std::string_view(line_buf, line_len), true);
        } else {
            // Not enough space, revert to where we were
            bytes_dumped = last_shown_offset - start_offset;
        }
    }

    return bytes_dumped;
}

inline size_t log_line_base::hex_dump_best_effort(const void* data, size_t len,
                                                  hex_dump_format format,
                                                  size_t max_lines,
                                                  const hex_inline_config& inline_config)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    
    if (format == hex_dump_format::inline_hex) {
        return hex_dump_inline_impl(bytes, len, max_lines, inline_config);
    } else {
        bool include_ascii = (format == hex_dump_format::full);
        return hex_dump_formatted_impl(bytes, len, include_ascii, max_lines);
    }
}

inline void log_line_base::hex_dump_full(const void* data, size_t len,
                                         hex_dump_format format,
                                         const hex_inline_config& inline_config)
{
    if (!data || len == 0) return;
    
    size_t offset = 0;
    while (offset < len) {
        // Pass offset and total length for progress reporting
        size_t dumped;
        if (format == hex_dump_format::inline_hex) {
            dumped = hex_dump_inline_impl(
                static_cast<const uint8_t*>(data) + offset,
                len - offset,
                64,
                inline_config
            );
        } else {
            // For formatted dumps, we need to pass the starting offset
            dumped = hex_dump_formatted_full_impl(
                static_cast<const uint8_t*>(data) + offset,
                len - offset,
                offset,  // Starting offset for this chunk
                len,     // Total length for progress display
                format == hex_dump_format::full,
                64
            );
        }
        
        if (dumped == 0) break;  // Buffer full or error
        offset += dumped;
        
        // If we haven't dumped everything, swap buffers and continue
        if (offset < len) {
            flush();
        }
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