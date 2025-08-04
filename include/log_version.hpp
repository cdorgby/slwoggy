/**
 * @file log_version.hpp
 * @brief Version information for slwoggy logging library
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

namespace slwoggy
{

#ifndef SLWOGGY_VERSION_STRING
    #define SLWOGGY_VERSION_STRING "dev"
#endif

inline constexpr const char *VERSION = SLWOGGY_VERSION_STRING;

} // namespace slwoggy