#pragma once

/**
 * @file slwoggy.hpp
 * @brief Single-header amalgamation of the slwoggy logging library
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 *
 * This file is an amalgamation of all slwoggy headers including dependencies.
 * To use: simply #include "slwoggy.hpp" in your project.
 *
 * Generated on: %Y-%m-%d %H:%M:%S
 */

// Default to __FILE__ if SOURCE_FILE_NAME is not defined
// This allows the amalgamated header to work without CMake
#ifndef SOURCE_FILE_NAME
#define SOURCE_FILE_NAME __FILE__
#endif
