#pragma once

#include <string>
#include <cstring>
#include <stdexcept>
#include <unistd.h> // For write() and STDOUT_FILENO
#include <fcntl.h>

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
        return written;
    }

  private:
    std::string filename_; ///< File name for logging
    int fd_{-1};           ///< File descriptor for logging
    bool close_fd_{false}; ///< Whether to close fd on destruction
};

} // namespace slwoggy