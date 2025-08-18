/**
 * @file fmt_config.hpp
 * @brief Configuration for fmt library to be used in header-only mode
 * 
 * This file ensures fmt is used in header-only mode, making slwoggy
 * truly header-only without requiring separate fmt compilation.
 */
#pragma once

// Enable fmt header-only mode
#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif

// Include fmt with header-only configuration
#include <fmt/format.h>