#pragma once

/**
 * @file log_sink.hpp
 * @brief Type-erased sink implementation for log output
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <memory>
#include <utility>
#include <cstring>
#include "log_buffer.hpp"

namespace slwoggy {

template <size_t BufferSize> class log_sink_impl
{
    static constexpr size_t buffer_size = BufferSize;

    struct concept_t
    {
        virtual ~concept_t()                                                   = default;
        virtual size_t process_batch(log_buffer **buffers, size_t count) const = 0;

        virtual concept_t *clone_in_place(void *buffer) const = 0;
        virtual concept_t *move_in_place(void *buffer)        = 0; // <— added
        virtual concept_t *heap_clone() const                 = 0;
        virtual size_t object_size() const                    = 0;
    };

    template <typename Formatter, typename Writer> struct model_t final : concept_t
    {
        Formatter formatter_;
        Writer writer_;

        model_t(Formatter f, Writer w) : formatter_(std::move(f)), writer_(std::move(w)) {}

        size_t process_batch(log_buffer **buffers, size_t count) const override
        {
            constexpr size_t STACK_LIMIT = 64 * 1024;

            // Process buffers until we hit a marker
            size_t processed = 0;
            for (size_t i = 0; i < count; ++i)
            {
                if (!buffers[i] || buffers[i]->is_flush_marker())
                {
                    break;  // Stop at markers
                }
                processed++;
            }
            
            if (processed == 0) return 0;
            
            // Calculate total size for processed buffers
            size_t total_size = 0;
            for (size_t i = 0; i < processed; ++i)
            {
                total_size += formatter_.calculate_size(buffers[i]);
            }

            if (total_size < STACK_LIMIT)
            {
                char *buf     = static_cast<char *>(alloca(total_size));
                size_t offset = 0;
                for (size_t i = 0; i < processed; ++i)
                    offset += formatter_.format(buffers[i], buf + offset, total_size - offset);
                writer_.write(buf, offset);
            }
            else
            {
                auto buf      = std::make_unique<char[]>(total_size);
                size_t offset = 0;
                for (size_t i = 0; i < processed; ++i)
                    offset += formatter_.format(buffers[i], buf.get() + offset, total_size - offset);
                writer_.write(buf.get(), offset);
            }
            
            return processed;
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
        if (ptr_) return ptr_->process_batch(buffers, count);
        return 0;
    }
};

using log_sink = log_sink_impl<64>; // Default buffer size of 64 bytes

} // namespace slwoggy
