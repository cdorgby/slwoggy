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
#include <variant>
#include <type_traits>
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
 * @brief Concrete sink model that holds formatter, writer, and optional filter
 *
 * This template class implements the sink_concept interface for a specific
 * combination of Formatter, Writer, and Filter types. It handles the actual log
 * processing logic with optional per-sink filtering.
 */
template <typename Formatter, typename Writer, typename Filter = void>
struct sink_model final : sink_concept
{
    Formatter formatter_;
    Writer writer_;
    
    // Only include filter member if Filter is not void
    [[no_unique_address]] std::conditional_t<std::is_void_v<Filter>, 
                                              std::monostate, Filter> filter_;
    
    // Constructor for formatter and writer only (no filter or void filter)
    template<typename F = Filter>
    sink_model(Formatter f, Writer w) 
        requires std::is_void_v<F>
        : formatter_(std::move(f)), writer_(std::move(w)) {}
    
    // Constructor with filter
    template<typename F = Filter>
    sink_model(Formatter f, Writer w, F flt) 
        requires (!std::is_void_v<F> && std::is_same_v<F, Filter>)
        : formatter_(std::move(f)), writer_(std::move(w)), filter_(std::move(flt)) {}
    
    // Helper to detect if writer has bulk_write method
    template <typename T>
    static constexpr bool has_bulk_write_v = requires(T& t, log_buffer_base** bufs, size_t count, const Formatter& fmt) {
        { t.bulk_write(bufs, count, fmt) } -> std::same_as<size_t>;
    };
    
    size_t process_batch(log_buffer_base** buffers, size_t count, char* write_buffer, size_t write_buffer_size) const override
    {
        // Process all buffers given to us, counting non-filtered ones
        // The dispatcher expects us to process exactly 'count' buffers and return
        // the number of non-filtered ones we processed
        
        // Use bulk write if available, otherwise fall back to format/write
        if constexpr (has_bulk_write_v<Writer>) 
        { 
            return process_batch_bulk(buffers, count); 
        }
        else 
        { 
            return process_batch_individual(buffers, count, write_buffer, write_buffer_size); 
        }
    }
    
    // Type erasure support
    sink_concept* clone_in_place(void* buffer) const override
    {
        if constexpr (std::is_void_v<Filter>)
        {
            return new (buffer) sink_model(formatter_, writer_);
        }
        else
        {
            return new (buffer) sink_model(formatter_, writer_, filter_);
        }
    }
    
    sink_concept* move_in_place(void* buffer) noexcept override
    {
        if constexpr (std::is_void_v<Filter>)
        {
            return new (buffer) sink_model(std::move(formatter_), std::move(writer_));
        }
        else
        {
            return new (buffer) sink_model(std::move(formatter_), std::move(writer_), std::move(filter_));
        }
    }
    
    sink_concept* heap_clone() const override
    {
        if constexpr (std::is_void_v<Filter>)
        {
            return new sink_model(formatter_, writer_);
        }
        else
        {
            return new sink_model(formatter_, writer_, filter_);
        }
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
        // Format all non-filtered buffers into the provided write buffer, flushing as needed
        // The dispatcher has already determined the count - we process exactly that many
        size_t offset = 0;
        size_t processed = 0;
        
        for (size_t i = 0; i < count; ++i)
        {
            // Skip filtered buffers (global filters)
            if (buffers[i]->filtered_)
            {
                continue;
            }
            
            // Apply sink-specific filter if present
            if constexpr (!std::is_void_v<Filter>)
            {
                if (!filter_.should_process(buffers[i]))
                {
                    continue;
                }
            }
            
            // This is a non-filtered buffer we need to format
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
            processed++;
        }
        
        // Flush any remaining data
        if (offset > 0)
        {
            writer_.write(write_buffer, offset);
        }
        
        // Return count of non-filtered buffers we processed
        return processed;
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
    // Constructor from formatter and writer (no filter)
    template <typename Formatter, typename Writer> 
    log_sink_impl(Formatter f, Writer w)
        : impl_(sink_model<Formatter, Writer>{std::move(f), std::move(w)})
    {
    }
    
    // Constructor from formatter, writer, and filter
    template <typename Formatter, typename Writer, typename Filter> 
    log_sink_impl(Formatter f, Writer w, Filter flt)
        : impl_(sink_model<Formatter, Writer, Filter>{std::move(f), std::move(w), std::move(flt)})
    {
    }

    // Default constructor
    log_sink_impl() = default;

    // Emplace construction without filter
    template <typename Formatter, typename Writer>
    void emplace(Formatter f, Writer w)
    {
        impl_.template emplace<sink_model<Formatter, Writer>>(std::move(f), std::move(w));
    }
    
    // Emplace construction with filter
    template <typename Formatter, typename Writer, typename Filter>
    void emplace(Formatter f, Writer w, Filter flt)
    {
        impl_.template emplace<sink_model<Formatter, Writer, Filter>>(std::move(f), std::move(w), std::move(flt));
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
