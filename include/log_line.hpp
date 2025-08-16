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
struct log_line_base
{
    log_buffer_base *buffer_;
    bool needs_header_{true};      // should the write_header() be called before first text write
    bool human_readable_{false};   // true for human-readable format with padding, false for structured/logfmt

    // Store these for endl support and header writing
    log_level level_;
    std::string_view file_;
    uint32_t line_;
    std::chrono::steady_clock::time_point timestamp_;
    const log_module_info &module_;

    log_line_base() = delete;

    log_line_base(log_level level, log_module_info &mod, std::string_view file, uint32_t line, bool needs_header, bool human_readable)
    : buffer_(level != log_level::nolog ? buffer_pool::instance().acquire(human_readable) : nullptr),
      needs_header_(needs_header), // Start with header needed
      human_readable_(human_readable),
      level_(level),
      file_(file),
      line_(line),
      timestamp_(log_fast_timestamp()),
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
    log_line_base(log_line_base &&other) noexcept
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
    log_line_base &operator=(log_line_base &&other) noexcept;

    // Keep copy deleted
    log_line_base(const log_line_base &)            = delete;
    log_line_base &operator=(const log_line_base &) = delete;

    virtual ~log_line_base();

    log_line_base &print(std::string_view str)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        buffer_->write_with_padding(str);
        return *this;
    }

    template <typename... Args> log_line_base &printf(const char *format, Args &&...args) &
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        // Forward to buffer's printf implementation
        buffer_->printf_with_padding(format, std::forward<Args>(args)...);

        return *this;
    }

    template <typename... Args> log_line_base &&printf(const char *format, Args &&...args) &&
    {
        printf(format, std::forward<Args>(args)...);
        return std::move(*this);
    }

    // Helper method that returns *this for chaining
    template <typename... Args> log_line_base &printfmt(const char *format, Args &&...args)
    {
        printf(format, std::forward<Args>(args)...);
        return *this;
    }

    template <typename... Args> log_line_base &format(fmt::format_string<Args...> fmt, Args &&...args)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        // Format directly into buffer with padding support
        buffer_->format_to_buffer_with_padding(fmt, std::forward<Args>(args)...);
        return *this;
    }

    // Helper method that returns *this for chaining
    template <typename... Args> log_line_base &fmtprint(fmt::format_string<Args...> fmt, Args &&...args)
    {
        format(fmt, std::forward<Args>(args)...);
        return *this;
    }

    // Generic version for any formattable type
    template <typename T>
        requires Loggable<T>
    log_line_base &operator<<(const T &value)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        // Format directly into buffer
        buffer_->format_to_buffer_with_padding("{}", value);
        return *this;
    }

    template <typename T> log_line_base &operator<<(T *ptr)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        if (ptr == nullptr) { buffer_->write_with_padding("nullptr"); }
        else { buffer_->format_to_buffer_with_padding("{}", static_cast<const void *>(ptr)); }
        return *this;
    }

    // Keep specialized versions for common types
    log_line_base &operator<<(std::string_view str)
    {
        print(str);
        return *this;
    }

    log_line_base &operator<<(const char *str)
    {
        print(std::string_view(str));
        return *this;
    }

    log_line_base &operator<<(const std::string &str)
    {
        print(std::string_view(str));
        return *this;
    }

    log_line_base &operator<<(int value)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        buffer_->format_to_buffer_with_padding("{}", value);
        return *this;
    }

    log_line_base &operator<<(unsigned int value)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        buffer_->format_to_buffer_with_padding("{}", value);
        return *this;
    }

    log_line_base &operator<<(void *ptr)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        buffer_->format_to_buffer_with_padding("{}", ptr);
        return *this;
    }

    // Special handling for shared_ptr
    template <typename T> log_line_base &operator<<(const std::shared_ptr<T> &ptr)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        if (ptr) { buffer_->format_to_buffer_with_padding("{}", static_cast<const void *>(ptr.get())); }
        else { buffer_->write_with_padding("nullptr"); }
        return *this;
    }

    // Special handling for weak_ptr
    template <typename T> log_line_base &operator<<(const std::weak_ptr<T> &ptr)
    {
        if (!buffer_) { return *this; }

        // Write header if this is first write after swap
        ensure_header_written();

        if (auto sp = ptr.lock()) { buffer_->format_to_buffer_with_padding("{}", static_cast<const void *>(sp.get())); }
        else { buffer_->write_with_padding("(expired)"); }
        return *this;
    }

    log_line_base &operator<<(log_line_base &(*func)(log_line_base &)) { return func(*this); }

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
    template <typename T> log_line_base &add(std::string_view key, T &&value) &
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

    template <typename T> log_line_base &&add(std::string_view key, T &&value) &&
    {
        add(key, value);
        return std::move(*this);
    }

    // Swap buffer with a new one from pool, return old buffer
    log_buffer_base *swap_buffer()
    {
        auto *old_buffer = buffer_;

        // Finalize the old buffer before swapping
        if (old_buffer) { old_buffer->finalize(); }

        buffer_ = buffer_pool::instance().acquire(human_readable_);

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
    /**
     * @brief Ensure header is written if needed (first write after buffer swap)
     *
     * This helper method eliminates code duplication by centralizing the header
     * writing logic. It checks if a header needs to be written (after a buffer
     * swap) and writes it exactly once before the first content write.
     */
    void ensure_header_written()
    {
        if (needs_header_)
        {
            write_header();
            needs_header_ = false;
        }
    }

    /**
     * @brief Write header to buffer and return width
     * @return The number of characters written for the header
     */
    virtual size_t write_header() = 0;
};

/**
 * @brief Log line implementation for structured logging format (logfmt)
 * 
 * This class outputs logs in logfmt format: msg="text" key=value key2=value2
 * It automatically adds internal metadata fields and wraps the message in quotes.
 * 
 * The output format starts with msg="..." which is a format prefix, not a
 * structured field. This prefix won't interfere with user-defined "msg" fields
 * added via .add("msg", value).
 * 
 * Automatically added metadata fields:
 * - ts: Timestamp in nanoseconds since epoch
 * - level: Log level as string
 * - module: Module name
 * - file: Source file name
 * - line: Source line number
 */
class log_line_structured : public log_line_base
{
  public:
    log_line_structured(log_level level, log_module_info &mod, std::string_view file, uint32_t line)
    : log_line_base(level, mod, file, line, true, false)
    {
        if (buffer_)
        {
            auto metadata = buffer_->get_metadata_adapter();

            // Add standard metadata fields
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_TS, timestamp_.time_since_epoch().count());
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_LEVEL, string_from_log_level(level));
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_MODULE, module_.detail->name);
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_FILE, file_);
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_LINE, line_);
        }
    }

    ~log_line_structured() override
    {
        if (buffer_)
        {
            buffer_->append_or_replace_last('"');
        }
    }

    size_t write_header() override final;
};

/**
 * @brief Log line implementation for traditional text format with header
 * 
 * This class outputs logs in human-readable format with aligned columns:
 * TTTTTTTT.mmm [LEVEL] module     file:line message
 * 
 * The header provides context information in a fixed-width format for easy
 * visual scanning and alignment across multiple log lines.
 */
class log_line_headered: public log_line_base
{
  public:
    log_line_headered(log_level level, log_module_info &mod, std::string_view file, uint32_t line)
    : log_line_base(level, mod, file, line, true, true)
    {
    }

    size_t write_header() override final;
};

} // namespace slwoggy