#pragma once

/**
 * @file log_module.hpp
 * @brief Module-based log level control and registration
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */

#include <atomic>
#include <memory>
#include <shared_mutex>
#include <string_view>
#include <vector>
#include <cstring>
#include <cctype>
#include <algorithm>

#include "robin_hood.h"
#include "log_types.hpp"
#include "log_utils.hpp"

namespace slwoggy {

/**
 * @brief Internal module configuration data
 *
 * Holds the runtime state for a logging module, including its name
 * and current log level. The level is atomic to allow thread-safe
 * runtime adjustments.
 */
struct log_module_info_detail
{
    const char *name = "generic";                       ///< Module name (owned by registry for non-generic modules)
    std::atomic<log_level> level{GLOBAL_MIN_LOG_LEVEL}; ///< Current minimum log level for this module
};

/**
 * @brief Module information handle used by LOG() macro
 *
 * This lightweight structure holds a pointer to the actual module
 * configuration. Each compilation unit has its own static instance
 * that can be reassigned to different modules via LOG_MODULE_NAME().
 */
struct log_module_info
{
    log_module_info_detail *detail; ///< Pointer to shared module configuration
};

/**
 * @brief Registry for managing shared module configurations
 *
 * The module registry provides a centralized system for managing log modules across
 * the application. Each module can have its own runtime-adjustable log level,
 * allowing fine-grained control over logging verbosity by subsystem.
 *
 * Features:
 * - Thread-safe module registration and lookup
 * - "generic" module as the default for all compilation units
 * - Lazy module creation on first use
 * - Shared module instances across compilation units with the same name
 *
 * Usage:
 * - Modules are automatically created when referenced via LOG_MODULE_NAME()
 * - Module names are case-sensitive and should follow a consistent naming scheme
 * - The "generic" module is special and always exists with default settings
 *
 * Example module names:
 * - "network", "database", "ui", "physics", "audio"
 * - "subsystem::component" for hierarchical organization
 */
class log_module_registry
{
  private:
    // Default "generic" module - lives forever
    log_module_info_detail generic_module_{"generic", GLOBAL_MIN_LOG_LEVEL};

    // Registry of all modules by name
    robin_hood::unordered_map<std::string_view, std::unique_ptr<log_module_info_detail>> modules_;
    mutable std::shared_mutex mutex_; // Allow concurrent reads

    log_module_registry()
    {
        // Pre-register the generic module (with nullptr since we use the member directly)
        modules_.emplace("generic", nullptr);
    }

  public:
    static log_module_registry &instance()
    {
        static log_module_registry inst;
        return inst;
    }

    log_module_info_detail *get_module(const char *name)
    {
        // Fast path for "generic"
        if (std::strcmp(name, "generic") == 0) { return &generic_module_; }

        std::string_view name_view(name);

        // Try to find existing module (read lock)
        {
            std::shared_lock lock(mutex_);
            auto it = modules_.find(name_view);
            if (it != modules_.end()) { return it->second ? it->second.get() : &generic_module_; }
        }

        // Create new module (write lock)
        {
            std::unique_lock lock(mutex_);
            // Double-check in case another thread created it
            auto it = modules_.find(name_view);
            if (it != modules_.end()) { return it->second ? it->second.get() : &generic_module_; }

            // Create new module with same level as generic
            auto new_module   = std::make_unique<log_module_info_detail>();
            new_module->name  = strdup(name); // Need to own the string
            new_module->level = generic_module_.level.load();

            auto *ptr = new_module.get();
            // Use the owned string as the key
            modules_.emplace(std::string_view(new_module->name), std::move(new_module));
            return ptr;
        }
    }

    // Get generic module for initialization
    log_module_info_detail *get_generic() { return &generic_module_; }

    std::vector<const log_module_info_detail *> get_all_modules() const
    {
        std::shared_lock lock(mutex_);
        std::vector<const log_module_info_detail *> result;
        result.reserve(modules_.size());

        for (auto &[name, module] : modules_)
        {
            // No const_cast needed!
            result.push_back(module ? module.get() : &generic_module_);
        }
        return result;
    }

    /**
     * @brief Set the log level for a specific module
     * @param name Module name
     * @param level New log level
     *
     * If the module doesn't exist, it will be created with the specified level.
     */
    void set_module_level(const char *name, log_level level)
    {
        auto *module = get_module(name);
        if (module) { module->level.store(level, std::memory_order_relaxed); }
    }

    /**
     * @brief Get the current log level for a module
     * @param name Module name
     * @return Current log level, or GLOBAL_MIN_LOG_LEVEL if module doesn't exist
     */
    log_level get_module_level(const char *name) const
    {
        std::string_view name_view(name);

        // Fast path for generic
        if (name_view == "generic") { return generic_module_.level.load(std::memory_order_relaxed); }

        std::shared_lock lock(mutex_);
        auto it = modules_.find(name_view);
        if (it != modules_.end() && it->second) { return it->second->level.load(std::memory_order_relaxed); }

        return GLOBAL_MIN_LOG_LEVEL;
    }

    /**
     * @brief Set all modules to the same log level
     * @param level New log level for all modules
     */
    void set_all_modules_level(log_level level)
    {
        // Set generic module
        generic_module_.level.store(level, std::memory_order_relaxed);

        // Set all other modules
        std::shared_lock lock(mutex_);
        for (auto &[name, module] : modules_)
        {
            if (module) { module->level.store(level, std::memory_order_relaxed); }
        }
    }

    /**
     * @brief Reset all modules to GLOBAL_MIN_LOG_LEVEL
     */
    void reset_all_modules_level() { set_all_modules_level(GLOBAL_MIN_LOG_LEVEL); }

    /**
     * @brief Configure logging levels from a string specification
     * @param config Configuration string
     * @return true if configuration was valid, false otherwise
     *
     * Format examples:
     * - "info" - Set all modules to info level
     * - "network=debug,database=warn" - Set specific modules
     * - "info,network=debug" - Set default to info, network to debug
     * - "*=warn,network=debug" - Set all to warn, then network to debug
     *
     * Module names can include wildcards:
     * - "net*=debug" - All modules starting with "net"
     * - "*worker=info" - All modules ending with "worker"
     */
    bool configure_from_string(const char *config)
    {
        if (!config) return false;

        std::string config_str(config);
        if (config_str.empty()) return false;

        // Remove whitespace
        config_str.erase(std::remove_if(config_str.begin(), config_str.end(), ::isspace), config_str.end());

        // Split by comma
        size_t pos = 0;
        while (pos < config_str.length())
        {
            size_t comma_pos = config_str.find(',', pos);
            if (comma_pos == std::string::npos) comma_pos = config_str.length();

            std::string part = config_str.substr(pos, comma_pos - pos);
            pos              = comma_pos + 1;

            if (part.empty()) continue;

            // Check if it's a module=level pair
            size_t eq_pos = part.find('=');
            if (eq_pos == std::string::npos)
            {
                // No equals sign - set global level
                log_level level = log_level_from_string(part.c_str());
                if (level == log_level::nolog && part != "nolog" && part != "off")
                {
                    return false; // Invalid level
                }
                set_all_modules_level(level);
            }
            else
            {
                // Module=level pair
                std::string module_pattern = part.substr(0, eq_pos);
                std::string level_str      = part.substr(eq_pos + 1);

                if (module_pattern.empty() || level_str.empty()) return false;

                log_level level = log_level_from_string(level_str.c_str());
                if (level == log_level::nolog && level_str != "nolog" && level_str != "off")
                {
                    return false; // Invalid level
                }

                // Handle wildcards
                if (module_pattern == "*")
                {
                    // Set all modules
                    set_all_modules_level(level);
                }
                else if (module_pattern.find('*') != std::string::npos)
                {
                    // Wildcard matching - get all modules and match pattern
                    auto modules = get_all_modules();

                    // Use common wildcard matching utility
                    for (const auto *module : modules)
                    {
                        if (!module) continue;
                        
                        if (detail::wildcard_match(module->name, module_pattern)) {
                            set_module_level(module->name, level);
                        }
                    }
                }
                else
                {
                    // Exact module name
                    set_module_level(module_pattern.c_str(), level);
                }
            }
        }

        return true;
    }
};

/*
 * Start of log module support
 */

namespace
{
// Initialize with the generic module
static log_module_info g_log_module_info{log_module_registry::instance().get_generic()};
} // namespace

/**
 * @brief Helper struct to configure module settings during static initialization
 *
 * This struct is used by LOG_MODULE_NAME and LOG_MODULE_LEVEL macros to
 * configure the logging module for a compilation unit. The constructors
 * run during static initialization to set up module association.
 */
struct log_module_configurator
{
    /**
     * @brief Set the module name for the current compilation unit
     * @param name Module name to use (must have static storage duration)
     */
    log_module_configurator(const char *name)
    {
        // Only change if different
        if (std::strcmp(g_log_module_info.detail->name, name) != 0)
        {
            g_log_module_info.detail = log_module_registry::instance().get_module(name);
        }
    }

    /**
     * @brief Set the initial log level for the current module
     * @param level Initial minimum log level
     */
    log_module_configurator(log_level level) { g_log_module_info.detail->level = level; }
};

/**
 * @brief Helper macro to generate unique variable names
 * Used internally by LOG_MODULE_NAME and LOG_MODULE_LEVEL
 */
#define _LOG_UNIQ_VAR_NAME(base) base##__COUNTER__

/**
 * @brief Set the module name for all LOG() calls in the current compilation unit
 *
 * This macro should be used at file scope (outside any function) to assign
 * all log messages in the file to a specific module. Modules allow grouped
 * control over log levels.
 *
 * @param name String literal module name (e.g., "network", "database")
 *
 * @code
 * // At file scope
 * LOG_MODULE_NAME("network");
 *
 * void process_request() {
 *     LOG(debug) << "Processing request";  // Uses "network" module
 * }
 * @endcode
 */
#define LOG_MODULE_NAME(name)                                                                    \
    namespace                                                                                    \
    {                                                                                            \
    static ::slwoggy::log_module_configurator _LOG_UNIQ_VAR_NAME(_log_module_name_setter){name}; \
    }

/**
 * @brief Set the initial log level for the current module
 *
 * This macro sets the initial minimum log level for the module associated
 * with the current compilation unit. Should be used after LOG_MODULE_NAME.
 * The level can be changed at runtime via the module registry.
 *
 * @param level The minimum log_level (e.g., log_level::debug)
 *
 * @code
 * LOG_MODULE_NAME("network");
 * LOG_MODULE_LEVEL(log_level::info);  // Start with info and above
 * @endcode
 */
#define LOG_MODULE_LEVEL(level)                                                                    \
    namespace                                                                                      \
    {                                                                                              \
    static ::slwoggy::log_module_configurator _LOG_UNIQ_VAR_NAME(_log_module_level_setter){level}; \
    }

} // namespace slwoggy