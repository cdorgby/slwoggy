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
#include <utility>
#include <memory>
#include <span>
#include <thread>

#include "fmt_config.hpp" // IWYU pragma: keep

#include "log_types.hpp"
#include "log_buffer.hpp"
#include "log_structured.hpp"
#include "log_structured_impl.hpp"
#include "log_module.hpp"

namespace slwoggy
{

// Forward declaration for concept
struct log_line_base;

/**
 * @brief Concept for types that can provide structured logging data
 *
 * Types satisfying this concept can be passed directly to log.add()
 * and will contribute their own key-value pairs to the log entry.
 *
 * Example:
 * @code
 * struct Connection {
 *     void add_log_context(log_line_base& line) const {
 *         line.add("conn_id", id_).add("remote_ip", ip_);
 *     }
 * };
 * @endcode
 */
template <typename T>
concept LogStructuredDataProvider = requires(T t, log_line_base &line) {
    { t.add_log_context(line) } -> std::same_as<void>;
};

/**
 * @brief Configuration for inline hex dump formatting
 *
 * Allows customization of how individual hex bytes are formatted
 * in inline_hex mode. Each byte can have a prefix, suffix, and
 * be surrounded by brackets, with configurable separators between bytes.
 *
 * Examples of different configurations:
 * - {"0x", "", " ", "", ""}    produces: 0x00 0x01 0x02
 * - {"", "h", " ", "", ""}      produces: 00h 01h 02h
 * - {"", "", "-", "", ""}       produces: 00-01-02
 * - {"", "", " ", "[", "]"}     produces: [00] [01] [02]
 * - {"0x", "", ", ", "<", ">"}  produces: <0x00>, <0x01>, <0x02>
 */
struct hex_inline_config
{
    const char *prefix        = ""; ///< String to prepend to each hex byte (e.g., "0x")
    const char *suffix        = ""; ///< String to append to each hex byte (e.g., "h")
    const char *separator     = ""; ///< String between consecutive bytes (e.g., "-" or " ")
    const char *left_bracket  = ""; ///< Left bracket before each byte (e.g., "[" or "<")
    const char *right_bracket = ""; ///< Right bracket after each byte (e.g., "]" or ">")
};

/**
 * @brief Represents a single log message with metadata
 *
 * This class handles the formatting and buffering of a single log message
 * along with its associated metadata (timestamp, level, location).
 */
struct log_line_base
{
    log_buffer_base *buffer_;
    bool needs_header_{true};    // should the write_header() be called before first text write
    bool human_readable_{false}; // true for human-readable format with padding, false for structured/logfmt

    log_line_base() = delete;

    log_line_base(log_level level, log_module_info &mod, std::string_view file, uint32_t line, bool needs_header, bool human_readable)
    : buffer_(level != log_level::nolog ? buffer_pool::instance().acquire(human_readable) : nullptr),
      needs_header_(needs_header), // Start with header needed
      human_readable_(human_readable)
    {
        if (buffer_)
        {
            buffer_->level_     = level;
            buffer_->file_      = file;
            buffer_->module_    = mod.detail; // Store module info pointer
            buffer_->line_      = line;
            buffer_->timestamp_ = log_fast_timestamp();
        }
    }

    // Move constructor - swap buffers
    log_line_base(log_line_base &&other) noexcept
    : buffer_(std::exchange(other.buffer_, nullptr)),
      needs_header_(std::exchange(other.needs_header_, false))
    {
    }

    // Move assignment - swap buffers
    log_line_base &operator=(log_line_base &&other) noexcept;

    // Keep copy deleted
    log_line_base(const log_line_base &)            = delete;
    log_line_base &operator=(const log_line_base &) = delete;

    virtual ~log_line_base();
    log_line_base &flush();

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
    template <typename T>
        requires(!LogStructuredDataProvider<T>)
    log_line_base &add(std::string_view key, T &&value) &
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

    template <typename T>
        requires LogStructuredDataProvider<std::decay_t<T>>
    log_line_base &add(T &&provider)
    {
        if (buffer_) { std::forward<T>(provider).add_log_context(*this); }
        return *this;
    }

    template <typename T> log_line_base &&add(std::string_view key, T &&value) &&
    {
        add(key, value);
        return std::move(*this);
    }

    /**
     * @brief Hex dump output format options
     *
     * Controls the format of hex dump output for binary data logging.
     */
    enum class hex_dump_format
    {
        full,      ///< Full hex dump with offset, hex bytes, and ASCII sidebar (like hexdump -C)
        no_ascii,  ///< Hex dump with offset and hex bytes only, no ASCII representation
        inline_hex ///< Compact inline hex string without offsets or formatting
    };

    /// Number of bytes per line in hex dump output (standard hexdump width)
    static constexpr size_t HEX_BYTES_PER_LINE = 16;

    /**
     * @brief Dump binary data in hex format (best effort within current buffer)
     *
     * Dumps binary data in the specified hex format, writing as much as possible
     * to the current buffer. Automatically handles duplicate line compression
     * using '*' notation (like hexdump -C) for full and no_ascii formats.
     *
     * @param data Pointer to binary data to dump
     * @param len Length of data in bytes
     * @param format Output format (full, no_ascii, or inline_hex)
     * @param max_lines Maximum number of lines to write (default 8)
     * @param inline_config Configuration for inline_hex format (prefix, suffix, separator, brackets)
     * @return Number of bytes actually dumped (may be less than len if buffer fills)
     *
     * @note This method writes as much as fits in the current buffer. For large
     *       data that may exceed buffer capacity, use hex_dump_full() instead.
     *
     * Example:
     * @code
     * uint8_t data[64];
     * LOG(info).hex_dump_best_effort(data, sizeof(data), log_line_base::hex_dump_format::full);
     * // Output: 0000: 00 01 02 03  04 05 06 07  08 09 0a 0b  0c 0d 0e 0f  |................|
     * @endcode
     */
    size_t hex_dump_best_effort(const void *data,
                                size_t len,
                                hex_dump_format format                 = hex_dump_format::full,
                                size_t max_lines                       = 8,
                                const hex_inline_config &inline_config = {});

    /**
     * @brief Dump entire binary data across multiple buffers if needed
     *
     * Dumps all binary data, automatically continuing across multiple log buffers
     * if the data exceeds the capacity of a single buffer. Shows progress
     * indicators (e.g., "binary data len: 1024/4096") when continuing.
     *
     * @param data Pointer to binary data to dump
     * @param len Length of data in bytes
     * @param format Output format (full, no_ascii, or inline_hex)
     * @param inline_config Configuration for inline_hex format
     *
     * @note This method will flush buffers as needed to ensure all data is dumped.
     *       Offsets are maintained across buffer boundaries for consistent output.
     *
     * Example:
     * @code
     * uint8_t large_data[4096];
     * LOG(info).hex_dump_full(large_data, sizeof(large_data), log_line_base::hex_dump_format::no_ascii);
     * // Output spans multiple log entries with progress tracking
     * @endcode
     */
    void hex_dump_full(const void *data,
                       size_t len,
                       hex_dump_format format                 = hex_dump_format::full,
                       const hex_inline_config &inline_config = {});

    // Convenience method for span
    template <typename T>
    size_t hex_dump_best_effort(std::span<const T> data,
                                hex_dump_format format                 = hex_dump_format::full,
                                size_t max_lines                       = 8,
                                const hex_inline_config &inline_config = {})
    {
        return hex_dump_best_effort(data.data(), data.size() * sizeof(T), format, max_lines, inline_config);
    }

    template <typename T>
    void hex_dump_full(std::span<const T> data, hex_dump_format format = hex_dump_format::full, const hex_inline_config &inline_config = {})
    {
        hex_dump_full(data.data(), data.size() * sizeof(T), format, inline_config);
    }

    // Swap buffer with a new one from pool, return old buffer
    log_buffer_base *swap_buffer()
    {
        auto *old_buffer = buffer_;

        buffer_ = buffer_pool::instance().acquire(human_readable_);

        // Reset positions
        needs_header_ = true;

        // Set buffer metadata if we got a new buffer
        if (buffer_)
        {
            buffer_->level_           = old_buffer->level_;
            buffer_->file_            = old_buffer->file_;
            buffer_->module_          = old_buffer->module_;
            buffer_->line_            = old_buffer->line_;
            buffer_->timestamp_       = old_buffer->timestamp_;
            buffer_->owner_thread_id_ = old_buffer->owner_thread_id_;
        }

        // Finalize the old buffer before swapping
        if (old_buffer) { old_buffer->finalize(); }

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

    // Helper function to format a single line of hex dump
    size_t format_hex_line_inline(char *buffer, size_t buffer_size, const uint8_t *bytes, size_t byte_count, const hex_inline_config &config);

    // Helper function to format a single line with offset and optional ASCII
    size_t format_hex_line_formatted(char *buffer, size_t buffer_size, const uint8_t *bytes, size_t byte_count, size_t offset, bool include_ascii);

    // Helper for inline hex dump - no duplicate detection, just raw hex
    size_t hex_dump_inline_impl(const uint8_t *bytes, size_t len, const hex_inline_config &config);

    // Helper for formatted hex dump with duplicate detection
    size_t hex_dump_formatted_impl(const uint8_t *bytes, size_t len, bool include_ascii, size_t max_lines);

    // Helper for full hex dump with offset continuation
    size_t hex_dump_formatted_full_impl(const uint8_t *bytes, size_t len, size_t start_offset, size_t total_len, bool include_ascii, size_t max_lines);
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
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_TS,
                                      buffer_->timestamp_.time_since_epoch().count());
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_LEVEL, string_from_log_level(level));
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_MODULE, buffer_->module_->name);
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_FILE, buffer_->file_);
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_LINE, buffer_->line_);
            // Use lower 32 bits of standard hash for consistency with header format
            size_t thread_hash = std::hash<std::thread::id>{}(buffer_->owner_thread_id_);
            metadata.add_kv_formatted(structured_log_key_registry::INTERNAL_KEY_THREAD_ID, static_cast<uint32_t>(thread_hash & 0xFFFFFFFF));
        }
    }

    ~log_line_structured() override
    {
        if (buffer_) { buffer_->append_or_replace_last('"'); }
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
class log_line_headered : public log_line_base
{
  public:
    log_line_headered(log_level level, log_module_info &mod, std::string_view file, uint32_t line)
    : log_line_base(level, mod, file, line, true, true)
    {
    }

    size_t write_header() override final;
};

} // namespace slwoggy