/**
 * @file log_writers.hpp
 * @brief Log output writer implementations
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <cstdio>
#include <string>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <unistd.h> // For write() and STDOUT_FILENO
#include <fcntl.h>
#include <sys/uio.h> // For writev
#include <errno.h>

#include "log_buffer.hpp"
#include "log_file_rotator.hpp"

namespace slwoggy
{

class file_writer
{
  public:
    file_writer(const std::string &filename, rotate_policy policy = rotate_policy{}) 
        : filename_(filename), policy_(policy), fd_(-1), close_fd_(true)
    {
        if (policy_.mode != rotate_policy::kind::none) {
            auto& rotator = file_rotation_service::instance();
            rotation_handle_ = rotator.open(filename, policy);
            close_fd_ = false;
        } else {
            fd_ = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
            if (fd_ < 0) { throw std::runtime_error("Failed to open log file: " + filename); }
        }
    }
    
    
    file_writer(int fd, bool close_fd = false) : filename_(""), fd_(fd), close_fd_(close_fd) {}

    file_writer(const file_writer &other) 
        : filename_(other.filename_), fd_(-1), close_fd_(other.close_fd_),
          policy_(other.policy_), rotation_handle_(other.rotation_handle_)
    {
        if (rotation_handle_) {
            close_fd_ = false;
        }
        else if (other.fd_ >= 0 && other.close_fd_)
        {
            fd_ = dup(other.fd_);
            if (fd_ < 0) { throw std::runtime_error("Failed to dup file descriptor"); }
        }
        else
        {
            fd_       = other.fd_;
            close_fd_ = false;
        }
    }

    file_writer &operator=(const file_writer &other)
    {
        if (this != &other)
        {
            if (rotation_handle_) {
                rotation_handle_->close();
            } else if (close_fd_ && fd_ >= 0) { 
                close(fd_); 
            }

            filename_ = other.filename_;
            close_fd_ = other.close_fd_;
            policy_ = other.policy_;
            rotation_handle_ = other.rotation_handle_;

            if (rotation_handle_) {
                close_fd_ = false;
            }
            else if (other.fd_ >= 0 && other.close_fd_)
            {
                fd_ = dup(other.fd_);
                if (fd_ < 0) { throw std::runtime_error("Failed to dup file descriptor"); }
            }
            else
            {
                fd_       = other.fd_;
                close_fd_ = false;
            }
        }
        return *this;
    }

    ~file_writer()
    {
        if (close_fd_ && fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    ssize_t write(const char *data, size_t len) const
    {
        int write_fd = fd_;
        
        if (rotation_handle_) {
            write_fd = rotation_handle_->get_current_fd();
            
            if (rotation_handle_->should_rotate(len)) {
                int new_fd = rotation_handle_->get_next_fd();
                if (new_fd == -1) {
                    // Use public methods instead of direct member access
                    rotation_handle_->increment_dropped_records();
                    rotation_handle_->increment_dropped_bytes(len);
                    return len;
                }
                write_fd = new_fd;
            }
        }
        
        if (write_fd < 0) { return -1; }
        
        // Capture FD once to ensure atomic write to single file
        const int captured_fd = write_fd;
        
        size_t total_written = 0;
        while (total_written < len) {
            ssize_t written = ::write(captured_fd, data + total_written, len - total_written);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("Failed to write to log file");
                return -1;
            }
            total_written += written;
        }
        
        if (rotation_handle_ && total_written > 0) {
            rotation_handle_->add_bytes_written(total_written);
        }
        return total_written;
    }

  protected:
    std::string filename_; ///< File name for logging
    mutable int fd_{-1};   ///< File descriptor for logging (mutable for rotation)
    bool close_fd_{false}; ///< Whether to close fd on destruction
    rotate_policy policy_; ///< Rotation policy
    std::shared_ptr<rotation_handle> rotation_handle_; ///< Rotation handle
};

// Constants for writer configuration
static constexpr size_t WRITER_MAX_IOV = 1024;  // Maximum iovec entries for writev

// High-performance writer using writev for zero-copy bulk writes
class writev_file_writer : public file_writer
{
  public:
    using file_writer::file_writer; // Inherit constructors

    // Bulk write implementation using writev
    template <typename Formatter>
    size_t bulk_write(log_buffer_base **buffers, size_t count, const Formatter &formatter) const
    {
        if (fd_ < 0 || count == 0) return 0;

        // Build iovec array
        struct iovec iov[WRITER_MAX_IOV];
        size_t iov_count = 0;
        size_t processed = 0;

        // Check if formatter wants newlines
        bool add_newline = false;
        if constexpr (requires { formatter.add_newline; }) { add_newline = formatter.add_newline; }

        for (size_t i = 0; i < count && iov_count < WRITER_MAX_IOV; ++i)
        {
            log_buffer_base *buf = buffers[i];

            // Skip filtered buffers
            if (buf->filtered_) continue;
            
            // Skip empty buffers
            if (buf->len() == 0) continue;

            // If we need to add newline, append it to the buffer
            if (add_newline) { buf->append_or_replace_last('\n'); }

            // Add buffer to iovec
            auto text               = buf->get_text();
            iov[iov_count].iov_base = const_cast<char *>(text.data());
            iov[iov_count].iov_len  = text.size();
            iov_count++;
            processed++;
        }

        // Single syscall to write everything
        if (iov_count > 0) {
            ssize_t written = writev(fd_, iov, iov_count);
            if (written < 0)
            {
                perror("writev failed");
                return 0;
            }
        }

        // Return count of non-filtered buffers we processed
        return processed;
    }
};

class discard_writer
{
  public:
    discard_writer() = default;

    // Copy constructor
    discard_writer(const discard_writer &) = default;

    // Copy assignment
    discard_writer &operator=(const discard_writer &) = default;

    // Write method that does nothing
    ssize_t write(const char *, size_t) const { return 0; }
};

} // namespace slwoggy