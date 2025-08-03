#pragma once

/**
 * @file log_site.hpp
 * @brief Log site registration and runtime control
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <deque>
#include <mutex>
#include <cstring>
#include "log_types.hpp"
#include "log_utils.hpp"

namespace slwoggy {

/**
 * @brief Descriptor for a single LOG() macro invocation site
 *
 * This structure captures compile-time information about each LOG() call site
 * in the codebase. Instances are created via static initialization within the
 * LOG() macro, ensuring one registration per unique call site.
 */
struct log_site_descriptor
{
    const char *file;     ///< Source file path (from SOURCE_FILE_NAME)
    int line;             ///< Line number where LOG() was invoked
    const char *function; ///< Function name (currently unused, always "unknown")
    log_level min_level;  ///< The log level specified in LOG(level)
};

/**
 * @brief Global registry of all LOG() call sites in the application
 *
 * This registry automatically collects information about every LOG() macro
 * invocation that survives compile-time filtering (level >= GLOBAL_MIN_LOG_LEVEL).
 * The registry serves multiple purposes:
 * - Tracks all active logging locations for analysis/debugging
 * - Calculates the maximum filename length for aligned log output
 * - Enables potential future features like per-site dynamic control
 *
 * Registration happens automatically via static initialization, ensuring
 * thread-safe, one-time registration per call site with zero runtime overhead
 * for disabled log levels.
 */
struct log_site_registry
{
    /**
     * @brief Get the vector of all registered log sites
     * @return Reference to the static deque containing all log site descriptors
     * @note Thread-safe access requires external synchronization via registry_mutex()
     * @note The deque is used to ensure pointer stability across the application lifetime
     */
    static std::deque<log_site_descriptor> &sites()
    {
        static std::deque<log_site_descriptor> s;
        return s;
    }

    /**
     * @brief Register a new log site
     * @param file Source file path (typically SOURCE_FILE_NAME)
     * @param line Line number of the LOG() invocation
     * @param min_level The log level specified in LOG(level)
     * @param function Optional function name (currently unused)
     *
     * This method is called automatically by the LOG() macro during static
     * initialization. It also updates the longest filename tracking for
     * proper log alignment.
     */
    static auto &register_site(const char *file, int line, log_level min_level, const char *function)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        sites().emplace_back(log_site_descriptor{file, line, function ? function : "unknown", min_level});

        if (file && std::strlen(file) > longest_file()) { longest_file() = static_cast<int32_t>(std::strlen(file)); }
        return sites().back();
    }

    /**
     * @brief Find a specific log site by file and line
     * @param file Source file path to search for
     * @param line Line number to search for
     * @return Pointer to the log site descriptor if found, nullptr otherwise
     */
    static const log_site_descriptor *find_site(const char *file, int line)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        for (const auto &site : sites())
        {
            if (std::strcmp(site.file, file) == 0 && site.line == line) { return &site; }
        }
        return nullptr;
    }

    /**
     * @brief Clear all registered log sites
     * @note Primarily for testing; production code rarely needs this
     */
    static void clear()
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        sites().clear();
    }

    /**
     * @brief Get the length of the longest registered filename
     * @return Maximum filename length among all registered log sites
     *
     * This value is used by the logging system to properly align log output,
     * ensuring that all log messages have consistent formatting regardless
     * of source file path length.
     */
    static int32_t &longest_file()
    {
        static int32_t longest_file = 0;
        return longest_file;
    }

    /**
     * @brief Set the log level for a specific site
     * @param file Source file path
     * @param line Line number
     * @param level New log level for the site
     * @return true if site was found and updated, false otherwise
     */
    static bool set_site_level(const char *file, int line, log_level level)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        for (auto &site : sites())
        {
            if (std::strcmp(site.file, file) == 0 && site.line == line)
            {
                site.min_level = level;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Get the log level for a specific site
     * @param file Source file path
     * @param line Line number
     * @return The site's log level if found, log_level::nolog if not found
     */
    static log_level get_site_level(const char *file, int line)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        for (const auto &site : sites())
        {
            if (std::strcmp(site.file, file) == 0 && site.line == line) { return site.min_level; }
        }
        return log_level::nolog;
    }

    /**
     * @brief Set log level for all sites in a specific file
     * @param file Source file path (can include wildcards)
     * @param level New log level
     * @return Number of sites updated
     */
    static size_t set_file_level(const char *file, log_level level)
    {
        if (!file) return 0;

        std::lock_guard<std::mutex> lock(registry_mutex());
        size_t count = 0;

        std::string file_pattern(file);
        bool has_wildcard = (file_pattern.find('*') != std::string::npos);

        if (has_wildcard)
        {
            // Use common wildcard matching utility
            for (auto &site : sites())
            {
                if (detail::wildcard_match(site.file, file_pattern))
                {
                    site.min_level = level;
                    count++;
                }
            }
        }
        else
        {
            // Exact match
            for (auto &site : sites())
            {
                if (std::strcmp(site.file, file) == 0)
                {
                    site.min_level = level;
                    count++;
                }
            }
        }

        return count;
    }

    /**
     * @brief Set log level for all registered sites
     * @param level New log level
     */
    static void set_all_sites_level(log_level level)
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        for (auto &site : sites()) { site.min_level = level; }
    }

  private:
    // Add a mutex to protect all shared static data
    static std::mutex &registry_mutex()
    {
        static std::mutex m;
        return m;
    }
};

} // namespace slwoggy