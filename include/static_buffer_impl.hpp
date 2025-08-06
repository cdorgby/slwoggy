/**
 * @file static_buffer_impl.hpp
 * @brief Implementation details for static_buffer that require generic_buffer_pool
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include "static_buffer.hpp"
#include "generic_buffer_pool.hpp"

namespace slwoggy
{

// Implementation of release method that requires generic_buffer_pool definition
template<size_t... REGION_SIZES>
    requires (sizeof...(REGION_SIZES) > 0)
void static_buffer<REGION_SIZES...>::release()
{
    // Thread-safe reference counting with proper memory ordering:
    // - acq_rel ensures the decrement can't be reordered with prior writes (release)
    //   and that other threads see the result correctly (acquire)
    // - The extra acquire fence ensures the cleanup thread sees all writes from
    //   any thread that previously held a reference
    if (header_->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        // Last reference - safe to return to pool
        // Acquire fence to synchronize with all previous releases
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // Reset buffer state
        reset();
        
        // Return storage to pool (go back to beginning of storage including header)
        char* storage_start = reinterpret_cast<char*>(header_);
        pool_->return_storage(storage_start);
    }
}

} // namespace slwoggy