/**
 * @file log_sink.hpp
 * @brief Type-erased sink implementation for log output
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <utility>
#include <cstring>
#include <cassert>
#include <concepts>
#include "log_buffer.hpp"
#include "log_types.hpp"
#include "type_erased.hpp"

namespace slwoggy
{

/**
 * @brief Abstract interface for log sink operations
 *
 * This interface defines the contract that all sink implementations must follow
 * to work with the type erasure system. It provides the core sink functionality
 * plus the necessary operations for cloning and moving within the type erasure
 * framework.
 */
struct sink_concept
{
    virtual ~sink_concept() = default;
    
    // Core sink operation
    virtual size_t process_batch(log_buffer_base **buffers, size_t count, char *write_buffer, size_t write_buffer_size) const = 0;

    // Type erasure support operations
    virtual sink_concept* clone_in_place(void* buffer) const = 0;
    virtual sink_concept* move_in_place(void* buffer) noexcept = 0;
    virtual sink_concept* heap_clone() const = 0;
    virtual size_t object_size() const = 0;
};

/**
 * @brief Concrete sink model that holds formatter and writer
 *
 * This template class implements the sink_concept interface for a specific
 * combination of Formatter and Writer types. It handles the actual log
 * processing logic and provides the type erasure support operations.
 */
template <typename Formatter, typename Writer>
struct sink_model final : sink_concept
{
    Formatter formatter_;
    Writer writer_;
    
    sink_model(Formatter f, Writer w) 
        : formatter_(std::move(f)), writer_(std::move(w)) {}
    
    // Helper to detect if writer has bulk_write method
    template <typename T>
    static constexpr bool has_bulk_write_v = requires(T& t, log_buffer_base** bufs, size_t count, const Formatter& fmt) {
        { t.bulk_write(bufs, count, fmt) } -> std::same_as<size_t>;
    };
    
    size_t process_batch(log_buffer_base** buffers, size_t count, char* write_buffer, size_t write_buffer_size) const override
    {
        // Process buffers until we hit a marker (null or flush marker)
        size_t processed = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (!buffers[i] || buffers[i]->is_flush_marker())
            {
                break; // Stop at markers to maintain ordering
            }
            processed++;
        }
        
        if (processed == 0) return 0;
        
        // Use bulk write if available, otherwise fall back to format/write
        if constexpr (has_bulk_write_v<Writer>) 
        { 
            return process_batch_bulk(buffers, processed); 
        }
        else 
        { 
            return process_batch_individual(buffers, processed, write_buffer, write_buffer_size); 
        }
    }
    
    // Type erasure support
    sink_concept* clone_in_place(void* buffer) const override
    {
        return new (buffer) sink_model(formatter_, writer_);
    }
    
    sink_concept* move_in_place(void* buffer) noexcept override
    {
        return new (buffer) sink_model(std::move(formatter_), std::move(writer_));
    }
    
    sink_concept* heap_clone() const override
    {
        return new sink_model(formatter_, writer_);
    }
    
    size_t object_size() const override
    {
        return sizeof(sink_model);
    }
    
private:
    // Fast path for writers that support bulk operations
    size_t process_batch_bulk(log_buffer_base** buffers, size_t count) const
    {
        return writer_.bulk_write(buffers, count, formatter_);
    }
    
    // Standard path with formatting to intermediate buffer
    size_t process_batch_individual(log_buffer_base** buffers, size_t count, char* write_buffer, size_t write_buffer_size) const
    {
        // Format all buffers into the provided write buffer, flushing as needed
        size_t offset = 0;
        for (size_t i = 0; i < count; ++i)
        {
            size_t buffer_size = formatter_.calculate_size(buffers[i]);
            
            // Check if this buffer would overflow our write buffer
            if (offset + buffer_size > write_buffer_size)
            {
                // Flush what we have so far
                if (offset > 0)
                {
                    writer_.write(write_buffer, offset);
                    offset = 0;
                }
                
                // If single buffer is still too large after flush, we have a problem
                assert(buffer_size <= write_buffer_size && "Single log entry exceeds write buffer size");
            }
            
            // Format into write buffer
            offset += formatter_.format(buffers[i], write_buffer + offset, write_buffer_size - offset);
        }
        
        // Flush any remaining data
        if (offset > 0)
        {
            writer_.write(write_buffer, offset);
        }
        
        return count;
    }
};

/**
 * @brief Log sink implementation using type-erased storage
 *
 * This class provides a concrete sink implementation that can store any combination
 * of Formatter and Writer types using the generic type_erased template. It manages
 * the write buffer and delegates the actual processing to the stored sink model.
 *
 * @tparam BufferSize Size of the inline storage buffer for small object optimization.
 *                    If the formatter+writer pair fits within this buffer, no heap
 *                    allocation occurs. Default is 64 bytes.
 *
 * ## Usage Example:
 * @code
 * // Create a sink with JSON formatter writing to stdout
 * log_sink sink{json_formatter{}, file_writer{STDOUT_FILENO}};
 *
 * // Process a batch of log buffers
 * log_buffer_base* buffers[10];
 * size_t processed = sink.process_batch(buffers, 10);
 * @endcode
 */
template <size_t BufferSize> 
class log_sink_impl
{
    // Use the generic type_erased template with sink_concept
    type_erased<sink_concept, BufferSize> impl_;
    mutable char write_buffer_[LOG_SINK_BUFFER_SIZE]; // Temporary buffer for formatting output

public:
    // Constructor from formatter and writer
    template <typename Formatter, typename Writer> 
    log_sink_impl(Formatter f, Writer w)
        : impl_(sink_model<Formatter, Writer>{std::move(f), std::move(w)})
    {
    }

    // Default constructor
    log_sink_impl() = default;

    // Emplace construction
    template <typename Formatter, typename Writer>
    void emplace(Formatter f, Writer w)
    {
        impl_.template emplace<sink_model<Formatter, Writer>>(std::move(f), std::move(w));
    }

    // Process a batch of log buffers
    size_t process_batch(log_buffer_base** buffers, size_t count) const
    {
        if (impl_)
        {
            return impl_->process_batch(buffers, count, write_buffer_, sizeof(write_buffer_));
        }
        return 0;
    }

    // Check if sink is empty
    bool empty() const { return impl_.empty(); }
    explicit operator bool() const { return static_cast<bool>(impl_); }
};

using log_sink = log_sink_impl<64>; // Default buffer size of 64 bytes

} // namespace slwoggy
