/**
 * @file log_line.hpp
 * @brief Represents a single log message with metadata
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <chrono>
#include <fmt/format.h>
#include <utility>
#include <memory>

#include "log_types.hpp"
#include "log_buffer.hpp"
#include "log_structured.hpp"
#include "log_structured_impl.hpp"
#include "log_module.hpp"

namespace slwoggy
{

/**
 * @brief Represents a single log message with metadata
 *
 * This class handles the formatting and buffering of a single log message
 * along with its associated metadata (timestamp, level, location).
 */
struct log_line
{
    log_buffer *buffer_;
    bool needs_header_{false}; // True after swap, header written on first write

    // Store these for endl support and header writing
    log_level level_;
    std::string_view file_;
    uint32_t line_;
    std::chrono::steady_clock::time_point timestamp_;
    const log_module_info &module_;

    log_line() = delete;

    log_line(log_level level, log_module_info &mod, std::string_view file, uint32_t line)
    : buffer_(level != log_level::nolog ? buffer_pool::instance().acquire() : nullptr),
      level_(level),
      file_(file),
      line_(line),
      timestamp_(log_fast_timestamp()),
      needs_header_(true), // Start with header needed
      module_(mod)
    {
        if (buffer_)
        {
            buffer_->level_     = level_;
            buffer_->file_      = file_;
            buffer_->line_      = line_;
            buffer_->timestamp_ = timestamp_;
        }
    }

    // Move constructor - swap buffers
    log_line(log_line &&other) noexcept
    : buffer_(std::exchange(other.buffer_, nullptr)),
      needs_header_(std::exchange(other.needs_header_, false)),
      level_(other.level_),
      file_(other.file_),
      line_(other.line_),
      timestamp_(other.timestamp_),
      module_(other.module_)
    {
    }

    // Move assignment - swap buffers
    log_line &operator=(log_line &&other) noexcept;

    // Keep copy deleted
    log_line(const log_line &)            = delete;
    log_line &operator=(const log_line &) = delete;

    ~log_line();

    log_line &print(std::string_view str)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        buffer_->write_with_padding(str);
        return *this;
    }

    template <typename... Args> log_line &printf(const char *format, Args &&...args) &
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        // Forward to buffer's printf implementation
        buffer_->printf_with_padding(format, std::forward<Args>(args)...);

        return *this;
    }

    template <typename... Args> log_line &&printf(const char *format, Args &&...args) &&
    {
        printf(format, std::forward<Args>(args)...);
        return std::move(*this);
    }

    // Helper method that returns *this for chaining
    template <typename... Args> log_line &printfmt(const char *format, Args &&...args)
    {
        printf(format, std::forward<Args>(args)...);
        return *this;
    }

    template <typename... Args> log_line &format(fmt::format_string<Args...> fmt, Args &&...args)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        // Format directly into buffer with padding support
        buffer_->format_to_buffer_with_padding(fmt, std::forward<Args>(args)...);
        return *this;
    }

    // Helper method that returns *this for chaining
    template <typename... Args> log_line &fmtprint(fmt::format_string<Args...> fmt, Args &&...args)
    {
        format(fmt, std::forward<Args>(args)...);
        return *this;
    }

    // Generic version for any formattable type
    template <typename T>
        requires Loggable<T>
    log_line &operator<<(const T &value)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        // Format directly into buffer
        buffer_->format_to_buffer_with_padding("{}", value);
        return *this;
    }

    template <typename T> log_line &operator<<(T *ptr)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        if (ptr == nullptr) 
        { 
            buffer_->write_with_padding("nullptr"); 
        }
        else 
        { 
            buffer_->format_to_buffer_with_padding("{}", static_cast<const void *>(ptr)); 
        }
        return *this;
    }

    // Keep specialized versions for common types
    log_line &operator<<(std::string_view str)
    {
        print(str);
        return *this;
    }

    log_line &operator<<(const char *str)
    {
        print(std::string_view(str));
        return *this;
    }

    log_line &operator<<(const std::string &str)
    {
        print(std::string_view(str));
        return *this;
    }

    log_line &operator<<(int value)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        buffer_->format_to_buffer_with_padding("{}", value);
        return *this;
    }

    log_line &operator<<(unsigned int value)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        buffer_->format_to_buffer_with_padding("{}", value);
        return *this;
    }

    log_line &operator<<(void *ptr)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        buffer_->format_to_buffer_with_padding("{}", ptr);
        return *this;
    }

    // Special handling for shared_ptr
    template <typename T> log_line &operator<<(const std::shared_ptr<T> &ptr)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        if (ptr) 
        { 
            buffer_->format_to_buffer_with_padding("{}", static_cast<const void *>(ptr.get())); 
        }
        else 
        { 
            buffer_->write_with_padding("nullptr"); 
        }
        return *this;
    }

    // Special handling for weak_ptr
    template <typename T> log_line &operator<<(const std::weak_ptr<T> &ptr)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }

        if (auto sp = ptr.lock()) 
        { 
            buffer_->format_to_buffer_with_padding("{}", static_cast<const void *>(sp.get())); 
        }
        else 
        { 
            buffer_->write_with_padding("(expired)"); 
        }
        return *this;
    }

    log_line &operator<<(log_line &(*func)(log_line &)) { return func(*this); }

    /**
     * @brief Add structured key-value metadata to the log entry
     *
     * Adds a key-value pair to the structured metadata section of the log.
     * The key is registered in the global key registry and stored as a
     * numeric ID for efficiency. The value is formatted to a string using
     * fmt::format.
     *
     * @tparam T Any type formattable by fmt::format
     * @param key The metadata key (e.g., "user_id", "request_id")
     * @param value The value to associate with the key
     * @return *this for method chaining
     *
     * @note If the metadata section is full or an error occurs, the operation
     *       is silently ignored to ensure logging continues to work.
     *
     * Example:
     * @code
     * LOG(info).add("user_id", 123)
     *          .add("action", "login")
     *          .add("duration_ms", 45.7)
     *       << "User login completed";
     * @endcode
     */
    template <typename T> log_line &add(std::string_view key, T &&value) &
    {
        if (!buffer_) return *this;

        try
        {
            uint16_t key_id = structured_log_key_registry::instance().get_or_register_key(key);

            auto metadata = buffer_->get_metadata_adapter();
            metadata.add_kv_formatted(key_id, std::forward<T>(value));
        }
        catch (...)
        {
            // Silently ignore metadata errors to not break logging
        }

        return *this;
    }

    template <typename T> log_line &&add(std::string_view key, T &&value) &&
    {
        add(key, value);
        return std::move(*this);
    }

    // Swap buffer with a new one from pool, return old buffer
    log_buffer *swap_buffer()
    {
        auto *old_buffer = buffer_;
        
        // Finalize the old buffer before swapping
        if (old_buffer) {
            old_buffer->finalize();
        }
        
        buffer_          = buffer_pool::instance().acquire();

        // Reset positions
        needs_header_ = true;

        // Set buffer metadata if we got a new buffer
        if (buffer_)
        {
            buffer_->level_     = level_;
            buffer_->file_      = file_;
            buffer_->line_      = line_;
            buffer_->timestamp_ = timestamp_;
        }

        return old_buffer;
    }

  private:
    // Write header to buffer and return width
    size_t write_header();
};

} // namespace slwoggy