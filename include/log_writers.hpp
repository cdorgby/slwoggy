#pragma once

/**
 * @file log_writers.hpp
 * @brief Log output writer implementations
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <cstdio>
#include <string>
#include <cstring>
#include <stdexcept>
#include <unistd.h> // For write() and STDOUT_FILENO
#include <fcntl.h>
#include <sys/uio.h> // For writev

#include "log_buffer.hpp"

namespace slwoggy {

class file_writer
{
  public:
    file_writer(const std::string &filename) : filename_(filename), fd_(-1), close_fd_(true)
    {
        fd_ = open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd_ < 0) { throw std::runtime_error("Failed to open log file: " + filename); }
    }
    file_writer(int fd, bool close_fd = false) : filename_(""), fd_(fd), close_fd_(close_fd) {}

    // Copy constructor - dup the file descriptor
    file_writer(const file_writer& other) 
        : filename_(other.filename_), fd_(-1), close_fd_(other.close_fd_)
    {
        if (other.fd_ >= 0 && other.close_fd_) {
            fd_ = dup(other.fd_);
            if (fd_ < 0) {
                throw std::runtime_error("Failed to dup file descriptor");
            }
        } else {
            // For stdout/stderr, don't dup, just share
            fd_ = other.fd_;
            close_fd_ = false;
        }
    }

    // Copy assignment
    file_writer& operator=(const file_writer& other)
    {
        if (this != &other)
        {
            // Close our current fd if needed
            if (close_fd_ && fd_ >= 0) {
                close(fd_);
            }
            
            filename_ = other.filename_;
            close_fd_ = other.close_fd_;
            
            if (other.fd_ >= 0 && other.close_fd_) {
                fd_ = dup(other.fd_);
                if (fd_ < 0) {
                    throw std::runtime_error("Failed to dup file descriptor");
                }
            } else {
                fd_ = other.fd_;
                close_fd_ = false;
            }
        }
        return *this;
    }

    ~file_writer()
    {
        if (close_fd_ && fd_ >= 0)
        {
            close(fd_);
            fd_ = -1;
        }
    }

    ssize_t write(const char *data, size_t len) const
    {
        if (fd_ < 0) { return -1; } // Not initialized

        ssize_t written = ::write(fd_, data, len);
        if (written < 0)
        {
            perror("Failed to write to log file");
        }
        return written;
    }

  protected:
    std::string filename_; ///< File name for logging
    int fd_{-1};           ///< File descriptor for logging
    bool close_fd_{false}; ///< Whether to close fd on destruction
};

// High-performance writer using writev for zero-copy bulk writes
class writev_file_writer : public file_writer
{
public:
    using file_writer::file_writer;  // Inherit constructors

    // Bulk write implementation using writev
    template<typename Formatter>
    size_t bulk_write(log_buffer **buffers, size_t count, const Formatter& formatter) const
    {
        if (fd_ < 0 || count == 0) return 0;

        // Build iovec array
        constexpr size_t MAX_IOV = 1024;
        struct iovec iov[MAX_IOV];
        size_t iov_count = 0;
        
        // Check if formatter wants newlines
        bool add_newline = false;
        if constexpr (requires { formatter.add_newline; })
        {
            add_newline = formatter.add_newline;
        }
        
        size_t i;
        for (i = 0; i < count && iov_count < MAX_IOV; ++i)
        {
            log_buffer* buf = buffers[i];
            
            // Skip empty buffers
            if (buf->len() == 0) continue;
            
            // If we need to add newline, append it to the buffer
            if (add_newline)
            {
                buf->append_or_replace_last('\n');
            }
            
            // Add buffer to iovec
            auto text = buf->get_text();
            iov[iov_count].iov_base = const_cast<char*>(text.data());
            iov[iov_count].iov_len = text.size();
            iov_count++;
        }
        
        // Single syscall to write everything
        ssize_t written = writev(fd_, iov, iov_count);
        if (written < 0)
        {
            perror("writev failed");
            return 0;
        }
        
        // Return number of buffers consumed from input array (not iov_count)
        return i;
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