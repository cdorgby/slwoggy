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

namespace slwoggy
{

/**
 * @brief Type-erased sink implementation using small buffer optimization
 *
 * This class implements a type-erasure pattern to store any combination of
 * Formatter and Writer types without exposing them in the interface. This allows
 * sinks to be stored in homogeneous containers while maintaining type safety
 * and avoiding virtual function overhead in the hot path.
 *
 * @tparam BufferSize Size of the inline storage buffer for small object optimization.
 *                    If the formatter+writer pair fits within this buffer, no heap
 *                    allocation occurs. Default is 64 bytes.
 *
 * ## Design Pattern: Type Erasure with Small Buffer Optimization
 *
 * The implementation uses three key components:
 *
 * 1. **concept_t** - Abstract interface that defines the operations all sinks must support
 *    - process_batch(): Process multiple log buffers in one call
 *    - clone/move operations for copying and moving sinks
 *    - object_size(): Reports actual size for debugging
 *
 * 2. **model_t<Formatter, Writer>** - Concrete implementation that stores the actual
 *    formatter and writer objects. This is what gets instantiated for each sink type.
 *
 * 3. **Small Buffer Optimization** - If sizeof(model_t) <= BufferSize, the object is
 *    stored inline in the `storage_` array. Otherwise, it's heap-allocated.
 *
 * ## Usage Example:
 * @code
 * // Create a sink with JSON formatter writing to stdout
 * log_sink sink{json_formatter{}, file_writer{STDOUT_FILENO}};
 *
 * // Process a batch of log buffers
 * log_buffer* buffers[10];
 * size_t processed = sink.process_batch(buffers, 10);
 * @endcode
 *
 * ## Performance Considerations:
 * - Small buffer optimization avoids heap allocation for most formatter/writer pairs
 * - Batch processing amortizes virtual call overhead across multiple buffers
 * - Move operations are optimized to avoid unnecessary copies
 * - Type erasure allows storing different sink types in the same container
 *
 * ## Implementation Details:
 * - Uses placement new for in-place construction in the small buffer
 * - Properly handles alignment requirements with alignas(std::max_align_t)
 * - Batch processing stops at flush markers to maintain log ordering
 * - Stack allocation (alloca) used for formatting buffers under 64KB
 * - Heap allocation fallback for larger formatting operations
 */
template <size_t BufferSize> class log_sink_impl
{
    static constexpr size_t buffer_size = BufferSize;

    // Abstract interface for type-erased sink operations
    struct concept_t
    {
        virtual ~concept_t() = default;
        virtual size_t process_batch(log_buffer **buffers, size_t count, char *write_buffer, size_t write_buffer_size) const = 0;

        // Placement new operations for copy/move into provided buffer
        virtual concept_t *clone_in_place(void *buffer) const = 0;
        virtual concept_t *move_in_place(void *buffer)        = 0;

        // Heap allocation for when object doesn't fit in small buffer
        virtual concept_t *heap_clone() const = 0;
        virtual size_t object_size() const    = 0;
    };

    // Concrete implementation that holds the actual formatter and writer
    template <typename Formatter, typename Writer> struct model_t final : concept_t
    {
        Formatter formatter_;
        Writer writer_;

        model_t(Formatter f, Writer w) : formatter_(std::move(f)), writer_(std::move(w)) {}

        // Helper to detect if writer has bulk_write method
        template <typename T>
        static constexpr bool has_bulk_write_v = requires(T &t, log_buffer **bufs, size_t count, const Formatter &fmt) {
            { t.bulk_write(bufs, count, fmt) } -> std::same_as<size_t>;
        };

        size_t process_batch(log_buffer **buffers, size_t count, char *write_buffer, size_t write_buffer_size) const override
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
            if constexpr (has_bulk_write_v<Writer>) { return process_batch_bulk(buffers, processed); }
            else { return process_batch_individual(buffers, processed, write_buffer, write_buffer_size); }
        }

      private:
        // Fast path for writers that support bulk operations
        size_t process_batch_bulk(log_buffer **buffers, size_t count) const
        {
            return writer_.bulk_write(buffers, count, formatter_);
        }

        // Standard path with formatting to intermediate buffer
        size_t process_batch_individual(log_buffer **buffers, size_t count, char *write_buffer, size_t write_buffer_size) const
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
            if (offset > 0) { writer_.write(write_buffer, offset); }

            return count;
        }

        concept_t *clone_in_place(void *buffer) const override { return new (buffer) model_t(formatter_, writer_); }

        // Correct move_in_place implementation:
        concept_t *move_in_place(void *buffer) override
        {
            return new (buffer) model_t(std::move(formatter_), std::move(writer_));
        }

        concept_t *heap_clone() const override { return new model_t(formatter_, writer_); }

        size_t object_size() const override { return sizeof(model_t); }
    };

    alignas(std::max_align_t) char storage_[buffer_size];
    concept_t *ptr_ = nullptr;
    bool is_small_  = false;

    void destroy()
    {
        if (ptr_)
        {
            if (is_small_)
                ptr_->~concept_t();
            else
                delete ptr_;
            ptr_ = nullptr;
        }
    }

  public:
    template <typename Formatter, typename Writer> log_sink_impl(Formatter f, Writer w)
    {
        using Model = model_t<Formatter, Writer>;
        if constexpr (sizeof(Model) <= buffer_size)
        {
            ptr_      = new (storage_) Model(std::move(f), std::move(w));
            is_small_ = true;
        }
        else
        {
            ptr_      = new Model(std::move(f), std::move(w));
            is_small_ = false;
        }
    }

    log_sink_impl(const log_sink_impl &other) : is_small_(other.is_small_)
    {
        if (other.ptr_) { ptr_ = is_small_ ? other.ptr_->clone_in_place(storage_) : other.ptr_->heap_clone(); }
    }

    log_sink_impl(log_sink_impl &&other) noexcept : is_small_(other.is_small_), ptr_(nullptr)
    {
        if (other.ptr_)
        {
            if (is_small_)
            {
                // Small object: must move-construct into our storage
                ptr_ = other.ptr_->move_in_place(storage_);
                other.destroy();
            }
            else
            {
                // Large object: just steal the pointer
                ptr_       = other.ptr_;
                other.ptr_ = nullptr;
            }
        }
    }

    log_sink_impl &operator=(const log_sink_impl &other)
    {
        if (this != &other)
        {
            destroy();
            is_small_ = other.is_small_;
            if (other.ptr_) { ptr_ = is_small_ ? other.ptr_->clone_in_place(storage_) : other.ptr_->heap_clone(); }
        }
        return *this;
    }

    log_sink_impl &operator=(log_sink_impl &&other) noexcept
    {
        if (this != &other)
        {
            destroy();
            is_small_ = other.is_small_;
            if (other.ptr_)
            {
                if (is_small_)
                {
                    ptr_ = other.ptr_->move_in_place(storage_);
                    other.destroy();
                }
                else
                {
                    ptr_       = other.ptr_;
                    other.ptr_ = nullptr;
                }
            }
        }
        return *this;
    }

    ~log_sink_impl() { destroy(); }

    size_t process_batch(log_buffer **buffers, size_t count) const
    {
        if (ptr_) return ptr_->process_batch(buffers, count, write_buffer_, sizeof(write_buffer_));
        return 0;
    }

  private:
    mutable char write_buffer_[LOG_SINK_BUFFER_SIZE]; // Temporary buffer for formatting output
};

using log_sink = log_sink_impl<64>; // Default buffer size of 64 bytes

} // namespace slwoggy
