/**
 * @file log_gzip.hpp
 * @brief Gzip compression support using miniz library
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

// Configure miniz for header-only and small footprint
#define MINIZ_HEADER_FILE_ONLY         // No .cpp needed; functions become static inline
#define MINIZ_NO_ARCHIVE_APIS          // We only need deflate/gzip, not ZIP/tar
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES // Don't define macros like 'compress' that conflict with our code

// Include the amalgamated miniz.c for header-only mode
// The amalgamated version has everything needed
#include "miniz.c"

#include <array>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace slwoggy
{
namespace gzip
{

/**
 * @brief RAII wrapper for file descriptors
 */
class file_descriptor
{
  public:
    explicit file_descriptor(int fd = -1) noexcept : fd_(fd) {}

    file_descriptor(file_descriptor &&other) noexcept : fd_(other.release()) {}

    file_descriptor &operator=(file_descriptor &&other) noexcept
    {
        if (this != &other) { reset(other.release()); }
        return *this;
    }

    ~file_descriptor() { close(); }

    // Disable copy
    file_descriptor(const file_descriptor &)            = delete;
    file_descriptor &operator=(const file_descriptor &) = delete;

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    int release() noexcept
    {
        int old = fd_;
        fd_     = -1;
        return old;
    }

    void reset(int new_fd = -1) noexcept
    {
        if (fd_ != new_fd)
        {
            close();
            fd_ = new_fd;
        }
    }

    void close() noexcept
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

  private:
    int fd_;
};

/**
 * @brief RAII wrapper for miniz compression stream
 */
class compression_stream
{
  public:
    compression_stream() : stream_{} {}

    ~compression_stream()
    {
        if (initialized_) { mz_deflateEnd(&stream_); }
    }

    // Disable copy and move for simplicity
    compression_stream(const compression_stream &)            = delete;
    compression_stream &operator=(const compression_stream &) = delete;
    compression_stream(compression_stream &&)                 = delete;
    compression_stream &operator=(compression_stream &&)      = delete;

    bool init(int level)
    {
        int result   = mz_deflateInit2(&stream_, level, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 8, MZ_DEFAULT_STRATEGY);
        initialized_ = (result == MZ_OK);
        return initialized_;
    }

    mz_stream *get() noexcept { return &stream_; }
    const mz_stream *get() const noexcept { return &stream_; }

    int deflate(int flush) { return mz_deflate(&stream_, flush); }

  private:
    mz_stream stream_;
    bool initialized_ = false;
};

/**
 * @brief RAII wrapper for temporary file that gets deleted on failure
 */
class temp_file
{
  public:
    explicit temp_file(const std::string &path) : path_(path) {}

    ~temp_file()
    {
        if (!committed_ && !path_.empty()) { ::unlink(path_.c_str()); }
    }

    // Disable copy
    temp_file(const temp_file &)            = delete;
    temp_file &operator=(const temp_file &) = delete;

    // Enable move
    temp_file(temp_file &&other) noexcept : path_(std::move(other.path_)), committed_(other.committed_)
    {
        other.committed_ = true; // Prevent deletion in moved-from object
    }

    temp_file &operator=(temp_file &&other) noexcept
    {
        if (this != &other)
        {
            if (!committed_ && !path_.empty()) { ::unlink(path_.c_str()); }
            path_            = std::move(other.path_);
            committed_       = other.committed_;
            other.committed_ = true;
        }
        return *this;
    }

    void commit() noexcept { committed_ = true; }
    const std::string &path() const noexcept { return path_; }

  private:
    std::string path_;
    bool committed_ = false;
};

/**
 * @brief Write all data to file descriptor, handling partial writes and EINTR
 */
inline bool write_all(int fd, const void *buf, size_t len)
{
    const auto *p = static_cast<const unsigned char *>(buf);
    while (len > 0)
    {
        ssize_t w = ::write(fd, p, len);
        if (w < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        p += static_cast<size_t>(w);
        len -= static_cast<size_t>(w);
    }
    return true;
}

/**
 * @brief Create gzip header with modification time
 */
inline std::array<unsigned char, 10> create_gzip_header(uint32_t mtime)
{
    return {
        {
         0x1f, 0x8b,                                     // Magic number
            0x08,                                     // Compression method (deflate)
            0x00,                                     // Flags (no name, no comment, no extra fields)
            static_cast<unsigned char>(mtime & 0xff), // Modification time (little-endian)
            static_cast<unsigned char>((mtime >> 8) & 0xff),
         static_cast<unsigned char>((mtime >> 16) & 0xff),
         static_cast<unsigned char>((mtime >> 24) & 0xff),
         0x00, // Extra flags (0 for normal compression)
            0x03  // OS (3 = Unix)
        }
    };
}

/**
 * @brief Create gzip trailer with CRC32 and uncompressed size
 */
inline std::array<unsigned char, 8> create_gzip_trailer(uint32_t crc32, uint32_t uncompressed_size)
{
    return {
        {
         static_cast<unsigned char>(crc32 & 0xff),
         static_cast<unsigned char>((crc32 >> 8) & 0xff),
         static_cast<unsigned char>((crc32 >> 16) & 0xff),
         static_cast<unsigned char>((crc32 >> 24) & 0xff),
         static_cast<unsigned char>(uncompressed_size & 0xff),
         static_cast<unsigned char>((uncompressed_size >> 8) & 0xff),
         static_cast<unsigned char>((uncompressed_size >> 16) & 0xff),
         static_cast<unsigned char>((uncompressed_size >> 24) & 0xff),
         }
    };
}

/**
 * @brief Compress a file to gzip format using miniz's deflate API
 *
 * Uses RAII for all resources, ensuring proper cleanup even on exceptions.
 *
 * @param src Path to source file to compress
 * @param dst Path to destination .gz file (should not exist)
 * @param level Compression level (MZ_DEFAULT_COMPRESSION, MZ_BEST_SPEED, MZ_BEST_COMPRESSION, or 0-9)
 * @return true if compression successful, false on error
 */
inline bool file_to_gzip(const std::string &src, const std::string &dst, int level = MZ_DEFAULT_COMPRESSION)
{
    // Open source file for reading
    file_descriptor in_fd(::open(src.c_str(), O_RDONLY | O_CLOEXEC));
    if (!in_fd) return false;

    // Get source file modification time
    struct stat st;
    uint32_t mtime = 0;
    if (fstat(in_fd.get(), &st) == 0) { mtime = static_cast<uint32_t>(st.st_mtime); }

    // Create temporary output file (will auto-delete on failure)
    temp_file temp_out(dst);

    // Open destination file for writing (exclusive creation)
    file_descriptor out_fd(::open(dst.c_str(),
                                  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC
#ifdef O_NOFOLLOW
                                      | O_NOFOLLOW
#endif
                                  ,
                                  0644));
    if (!out_fd) return false;

    // Write gzip header
    auto gz_header = create_gzip_header(mtime);
    if (!write_all(out_fd.get(), gz_header.data(), gz_header.size())) { return false; }

    // Initialize compression stream (auto-cleanup on destruction)
    compression_stream compressor;
    if (!compressor.init(level)) { return false; }

    // Compression buffers
    constexpr size_t BUFFER_SIZE = 64 * 1024;
    std::vector<unsigned char> in_buf(BUFFER_SIZE);
    std::vector<unsigned char> out_buf(BUFFER_SIZE);

    // CRC32 calculation for gzip trailer
    mz_ulong crc32    = MZ_CRC32_INIT;
    mz_ulong total_in = 0;

    auto *stream = compressor.get();

    // Compression loop
    for (;;)
    {
        // Read chunk from input file
        ssize_t r = ::read(in_fd.get(), in_buf.data(), in_buf.size());
        if (r < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }

        // Update CRC32 for gzip trailer
        if (r > 0)
        {
            crc32 = mz_crc32(crc32, in_buf.data(), static_cast<unsigned>(r));
            total_in += static_cast<unsigned>(r);
        }

        // Set up input for compression
        stream->next_in  = in_buf.data();
        stream->avail_in = static_cast<unsigned>(r);
        int flush        = (r == 0) ? MZ_FINISH : MZ_NO_FLUSH;

        // Compress and write output
        do {
            stream->next_out  = out_buf.data();
            stream->avail_out = static_cast<unsigned>(out_buf.size());

            int ret = compressor.deflate(flush);
            if (ret == MZ_STREAM_ERROR) { return false; }

            size_t have = out_buf.size() - stream->avail_out;
            if (have && !write_all(out_fd.get(), out_buf.data(), have)) { return false; }
        } while (stream->avail_out == 0);

        // If we've finished all input, exit loop
        if (flush == MZ_FINISH) break;
    }

    // Write gzip trailer
    auto gz_trailer = create_gzip_trailer(crc32, total_in);
    if (!write_all(out_fd.get(), gz_trailer.data(), gz_trailer.size())) { return false; }

    // Success - commit the temp file so it won't be deleted
    temp_out.commit();
    return true;
}

} // namespace gzip
} // namespace slwoggy