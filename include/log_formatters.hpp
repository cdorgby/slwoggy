#pragma once

#include <cstring>

#include "log_types.hpp"
#include "log_buffer.hpp"

namespace slwoggy {

class raw_formatter
{
  public:
    bool use_color = true;
    bool add_newline = false;

    size_t calculate_size(const log_buffer *buffer) const
    {
        if (!buffer) return 0;

        size_t size = 0;

        // Color prefix
        if (use_color && buffer->level_ >= log_level::trace && buffer->level_ <= log_level::fatal)
        {
            size += std::strlen(log_level_colors[static_cast<int>(buffer->level_)]);
        }

        // Message text
        size += buffer->len();

        // Structured data
        auto metadata = buffer->get_metadata_adapter();
        auto iter = metadata.get_iterator();
        while (iter.has_next())
        {
            auto kv = iter.next();
            auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);
            
            size += 1; // space
            size += key_name.size();
            size += 1; // equals
            
            // Check if needs quotes
            bool needs_quotes = kv.value.find_first_of(" \t\n\r\"'") != std::string_view::npos;
            if (needs_quotes) size += 2; // quotes
            size += kv.value.size();
        }

        // Color reset
        if (use_color)
        {
            size += 4; // "\033[0m"
        }
        
        if (add_newline)
        {
            size += 1; // newline
        }
        
        return size;
    }

    size_t format(const log_buffer *buffer, char *output, size_t max_size) const
    {
        if (!buffer) return 0;

        char *ptr = output;
        char *end = output + max_size;

        // Helper to append string_view
        auto append = [&ptr, end](std::string_view sv) -> bool
        {
            if (ptr + sv.size() > end) return false;
            std::memcpy(ptr, sv.data(), sv.size());
            ptr += sv.size();
            return true;
        };

        // Add color
        if (use_color && buffer->level_ >= log_level::trace && buffer->level_ <= log_level::fatal)
        {
            const char *color = log_level_colors[static_cast<int>(buffer->level_)];
            if (!append(color)) return ptr - output;
        }

        // Add message text
        if (!append(buffer->get_text())) return ptr - output;

        // Add structured data
        auto metadata = buffer->get_metadata_adapter();
        auto iter = metadata.get_iterator();

        while (iter.has_next())
        {
            auto kv = iter.next();
            auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);
            
            if (ptr >= end) return ptr - output;
            *ptr++ = ' ';
            
            if (!append(key_name)) return ptr - output;
            
            if (ptr >= end) return ptr - output;
            *ptr++ = '=';
            
            // Check if needs quotes
            bool needs_quotes = kv.value.find_first_of(" \t\n\r\"'") != std::string_view::npos;
            
            if (needs_quotes)
            {
                if (ptr >= end) return ptr - output;
                *ptr++ = '"';
            }
            
            if (!append(kv.value)) return ptr - output;
            
            if (needs_quotes)
            {
                if (ptr >= end) return ptr - output;
                *ptr++ = '"';
            }
        }

        // Reset color
        if (use_color)
        {
            if (!append("\033[0m")) return ptr - output;
        }

        // Newline
        if (add_newline && ptr < end)
        {
            *ptr++ = '\n';
        }

        return ptr - output;
    }
};

class json_formatter
{
  public:
    bool pretty_print = false;
    bool add_newline = false;

    size_t calculate_size(const log_buffer *buffer) const
    {
        if (!buffer) return 0;

        // Helper to calculate escaped JSON string size
        auto calculate_escaped_size = [](std::string_view str) -> size_t
        {
            size_t size = 0;
            for (char c : str)
            {
                switch (c)
                {
                case '"':
                case '\\':
                case '\b':
                case '\f':
                case '\n':
                case '\r':
                case '\t': size += 2; break;
                default:
                    if (c >= 0x20 && c <= 0x7E) { size += 1; }
                    else { size += UNICODE_ESCAPE_CHARS; }
                }
            }
            return size;
        };

        // Calculate timestamp
        auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(buffer->timestamp_.time_since_epoch()).count();
        auto ms = timestamp_us / 1000;
        auto us = timestamp_us % 1000;

        // Calculate timestamp size: max int64 is 19 digits + dot + 3 decimal places = 23 chars
        char timestamp_buf[32];
        int timestamp_len = std::snprintf(timestamp_buf, sizeof(timestamp_buf), "%lld.%03lld", 
                                         static_cast<long long>(ms), static_cast<long long>(us));

        // Pre-calculate sizes for all fields
        auto text = buffer->get_message();
        size_t level_str_len = std::strlen(log_level_names[static_cast<int>(buffer->level_)]);
        size_t file_len = buffer->file_.size();
        
        // Calculate line number size
        char line_buf[16];
        int line_len = std::snprintf(line_buf, sizeof(line_buf), "%u", buffer->line_);

        // Calculate escaped sizes
        size_t text_escaped_size = calculate_escaped_size(text);
        size_t file_escaped_size = calculate_escaped_size(buffer->file_);

        // JSON structure size calculation:
        size_t json_size = 0;
        json_size += 2;  // {}
        json_size += 13; // "timestamp":
        json_size += timestamp_len;
        json_size += 10; // ,"level":"
        json_size += level_str_len;
        json_size += 10; // ","file":"
        json_size += file_escaped_size;
        json_size += 10; // ","line":
        json_size += line_len;
        json_size += 13; // ,"message":"
        json_size += text_escaped_size;
        json_size += 1; // closing quote for message

        // Add k/v overhead if any
        auto metadata = buffer->get_metadata_adapter();
        auto iter = metadata.get_iterator();
        while (iter.has_next())
        {
            auto kv = iter.next();
            auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);
            json_size += 3; // ,"
            json_size += calculate_escaped_size(key_name);
            json_size += 3; // ":"
            json_size += calculate_escaped_size(kv.value);
            json_size += 1; // "
        }

        // Add newline
        if (add_newline) json_size += 1;

        return json_size;
    }

    size_t format(const log_buffer *buffer, char *output, size_t max_size) const
    {
        if (!buffer) return 0;

        // Helper to escape JSON strings
        auto escape_json = [](std::string_view str, char *out) -> size_t
        {
            char *start = out;
            for (char c : str)
            {
                switch (c)
                {
                case '"':
                    *out++ = '\\';
                    *out++ = '"';
                    break;
                case '\\':
                    *out++ = '\\';
                    *out++ = '\\';
                    break;
                case '\b':
                    *out++ = '\\';
                    *out++ = 'b';
                    break;
                case '\f':
                    *out++ = '\\';
                    *out++ = 'f';
                    break;
                case '\n':
                    *out++ = '\\';
                    *out++ = 'n';
                    break;
                case '\r':
                    *out++ = '\\';
                    *out++ = 'r';
                    break;
                case '\t':
                    *out++ = '\\';
                    *out++ = 't';
                    break;
                default:
                    if (c >= 0x20 && c <= 0x7E) { *out++ = c; }
                    else
                    {
                        // Unicode escape for control chars
                        out += std::snprintf(out, UNICODE_ESCAPE_SIZE, "\\u%04x", static_cast<unsigned char>(c));
                    }
                }
            }
            return out - start;
        };

        char *ptr = output;
        char *end = output + max_size;

        // Calculate timestamp
        auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(buffer->timestamp_.time_since_epoch()).count();
        auto ms = timestamp_us / 1000;
        auto us = timestamp_us % 1000;

        // Start with timestamp and basic fields
        ptr += std::snprintf(ptr, end - ptr,
                            "{\"timestamp\":%lld.%03lld,\"level\":\"%s\",\"file\":\"",
                            static_cast<long long>(ms),
                            static_cast<long long>(us),
                            log_level_names[static_cast<int>(buffer->level_)]);

        if (ptr >= end) return ptr - output;

        ptr += escape_json(buffer->file_, ptr);
        if (ptr >= end) return ptr - output;

        ptr += std::snprintf(ptr, end - ptr, "\",\"line\":%u,\"message\":\"", buffer->line_);
        if (ptr >= end) return ptr - output;

        ptr += escape_json(buffer->get_message(), ptr);
        if (ptr >= end) return ptr - output;
        *ptr++ = '"';

        // Add structured data as JSON fields
        auto metadata = buffer->get_metadata_adapter();
        auto iter = metadata.get_iterator();
        while (iter.has_next())
        {
            auto kv = iter.next();
            auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);

            if (ptr + 7 >= end) return ptr - output; // Minimum space for ,"":{""
            *ptr++ = ',';
            *ptr++ = '"';
            ptr += escape_json(key_name, ptr);
            if (ptr >= end) return ptr - output;
            *ptr++ = '"';
            *ptr++ = ':';
            *ptr++ = '"';
            ptr += escape_json(kv.value, ptr);
            if (ptr >= end) return ptr - output;
            *ptr++ = '"';
        }

        if (ptr >= end) return ptr - output;
        *ptr++ = '}';

        if (add_newline && ptr < end)
        {
            *ptr++ = '\n';
        }

        return ptr - output;
    }
};


} // namespace slwoggy