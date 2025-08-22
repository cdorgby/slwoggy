/**
 * @file log_formatters.hpp
 * @brief Log message formatting implementations
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <cstring>
#include <streambuf>
#include <ostream>
#include <tao/json/events/to_stream.hpp>
#include <tao/json/events/to_pretty_stream.hpp>

#include "log_types.hpp"
#include "log_buffer.hpp"
#include "log_structured.hpp"
#include "log_structured_impl.hpp" // IWYU pragma: keep

namespace slwoggy
{

/**
 * @brief No-operation formatter that optionally copies raw buffer content
 *
 * This formatter performs minimal processing, either copying the raw buffer
 * content as-is or just calculating sizes without copying. Useful for:
 * - Benchmarking and testing
 * - Pass-through scenarios
 * - Custom sink implementations that need raw access
 */
class nop_formatter
{
  public:
    bool do_copy = false;

    size_t calculate_size(const log_buffer_base *buffer) const { return buffer ? buffer->len() : 0; }

    size_t format(const log_buffer_base *buffer, char *output, size_t max_size) const
    {
        if (!buffer || max_size == 0) return 0;

        if (do_copy)
        {
            size_t len = buffer->len();
            if (len > max_size) len = max_size;
            std::memcpy(output, buffer->get_text().data(), len);
            return len;
        }

        // Just return the size without copying
        return buffer->len();
    }
};

class raw_formatter
{
  public:
    bool use_color   = true;
    bool add_newline = false;

    size_t calculate_size(const log_buffer_base *buffer) const
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
        auto iter     = metadata.get_iterator();
        while (iter.has_next())
        {
            auto kv       = iter.next();
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

    size_t format(const log_buffer_base *buffer, char *output, size_t max_size) const
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
        auto iter     = metadata.get_iterator();

        while (iter.has_next())
        {
            auto kv       = iter.next();
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
        if (add_newline && ptr < end) { *ptr++ = '\n'; }

        return ptr - output;
    }
};

/**
 * @brief JSON formatter using taocpp/json library
 *
 * This formatter uses the taocpp/json library for robust JSON serialization
 * with proper escaping and Unicode handling. It maintains slwoggy's zero-allocation
 * principle by using a custom streambuf that writes directly to the output buffer.
 *
 * Features:
 * - Leverages taocpp/json's optimized JSON serialization
 * - Supports both compact and pretty-print output
 * - Zero intermediate allocations
 * - Proper JSON escaping and Unicode handling
 *
 * Usage:
 * @code
 * taocpp_json_formatter formatter;
 * formatter.pretty_print = true;  // Enable pretty printing
 * formatter.add_newline = true;   // Add newline after JSON
 * @endcode
 */
class taocpp_json_formatter
{
  private:
    /**
     * @brief Custom streambuf that writes directly to a char buffer
     *
     * This streambuf implementation allows us to use taocpp/json's stream-based
     * API while maintaining zero-allocation by writing directly to the pre-allocated
     * log buffer.
     */
    class direct_buffer_streambuf : public std::streambuf
    {
      private:
        char *begin_;
        char *end_;

      public:
        direct_buffer_streambuf(char *buffer, size_t size) : begin_(buffer), end_(buffer + size)
        {
            // Set up the put area for the entire buffer
            setp(begin_, end_);
        }

        size_t written() const { return pptr() - begin_; }

      protected:
        // Called when the buffer is full
        int_type overflow(int_type ch) override
        {
            if (ch != traits_type::eof())
            {
                // Buffer is full, can't write more
                return traits_type::eof();
            }
            return ch;
        }
    };

  public:
    bool pretty_print = false;
    bool add_newline  = false;

    /**
     * @brief Template method that produces JSON events for a log buffer
     *
     * This method follows taocpp/json's producer pattern, making it reusable
     * with different consumers (size calculation, formatting, etc.)
     */
    template <typename Consumer> void produce_log_json(Consumer &c, const log_buffer_base *buffer) const
    {
        c.begin_object();

        // Timestamp (as milliseconds.microseconds)
        c.key("timestamp");
        auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(buffer->timestamp_.time_since_epoch()).count();
        auto ms = timestamp_us / 1000;
        auto us = timestamp_us % 1000;

        // Format as "123456.789" (milliseconds with 3 decimal places)
        char timestamp_buf[32];
        int len = std::snprintf(timestamp_buf, sizeof(timestamp_buf), "%lld.%03lld", static_cast<long long>(ms), static_cast<long long>(us));
        c.string(std::string_view(timestamp_buf, len));
        c.member();

        // Level
        c.key("level");
        c.string(log_level_names[static_cast<int>(buffer->level_)]);
        c.member();

        // File
        c.key("file");
        c.string(buffer->file_);
        c.member();

        // Line
        c.key("line");
        c.number(static_cast<std::uint64_t>(buffer->line_));
        c.member();

        // Message
        c.key("message");
        auto msg = buffer->get_message();
        // Strip trailing quote from logfmt-formatted messages
        if (!buffer->is_padding_enabled() && !msg.empty() && msg.back() == '"') { msg.remove_suffix(1); }
        c.string(msg);
        c.member();

        // Structured data
        auto metadata = buffer->get_metadata_adapter();
        auto iter     = metadata.get_iterator();
        while (iter.has_next())
        {
            auto kv       = iter.next();
            auto key_name = structured_log_key_registry::instance().get_key(kv.key_id);
            c.key(key_name);
            c.string(kv.value);
            c.member();
        }

        c.end_object();
    }

    size_t calculate_size(const log_buffer_base *buffer) const
    {
        if (!buffer) return 0;

        // We need to actually format to get exact size with taocpp/json
        // since it doesn't provide a size_consumer. We'll use a counting streambuf.
        class counting_streambuf : public std::streambuf
        {
            size_t count_ = 0;

          protected:
            int_type overflow(int_type ch) override
            {
                if (ch != traits_type::eof()) { ++count_; }
                return ch;
            }
            std::streamsize xsputn(const char *, std::streamsize n) override
            {
                count_ += n;
                return n;
            }

          public:
            size_t count() const { return count_; }
        };

        counting_streambuf counter;
        std::ostream stream(&counter);

        if (pretty_print)
        {
            tao::json::events::to_pretty_stream consumer(stream, 2);
            produce_log_json(consumer, buffer);
        }
        else
        {
            tao::json::events::to_stream consumer(stream);
            produce_log_json(consumer, buffer);
        }

        stream.flush();
        size_t size = counter.count();

        if (add_newline) { size += 1; }
        return size;
    }

    size_t format(const log_buffer_base *buffer, char *output, size_t max_size) const
    {
        if (!buffer || max_size == 0) return 0;

        // Create our custom streambuf that writes to the provided buffer
        direct_buffer_streambuf streambuf(output, max_size);
        std::ostream stream(&streambuf);

        // Use taocpp/json's to_stream consumer
        if (pretty_print)
        {
            // Pretty print with 2-space indent
            tao::json::events::to_pretty_stream consumer(stream, 2);
            produce_log_json(consumer, buffer);
        }
        else
        {
            // Compact output
            tao::json::events::to_stream consumer(stream);
            produce_log_json(consumer, buffer);
        }

        // Force flush to ensure all data is written to our buffer
        stream.flush();

        size_t written_size = streambuf.written();

        // Add newline if requested and there's space
        if (add_newline && written_size < max_size) { output[written_size++] = '\n'; }

        return written_size;
    }
};

} // namespace slwoggy