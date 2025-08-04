/**
 * @file log_utils.hpp
 * @brief Common utilities for the slwoggy logging library
 * @author dorgby.net
 * @copyright Copyright (c) 2025 dorgby.net. Licensed under MIT License, see LICENSE for details.
 */
#pragma once

#include <string>

namespace slwoggy
{

namespace detail
{

/**
 * @brief Simple wildcard pattern matching utility
 *
 * Supports patterns with * at the beginning and/or end:
 * - "prefix*" matches strings starting with "prefix"
 * - "*suffix" matches strings ending with "suffix"
 * - "*infix*" matches strings containing "infix"
 * - "exact" matches only "exact"
 *
 * @param text The text to match against
 * @param pattern The pattern with optional wildcards
 * @return true if the text matches the pattern
 */
inline bool wildcard_match(const std::string &text, const std::string &pattern)
{
    if (pattern.empty()) return text.empty();

    bool starts_with = (pattern.back() == '*');
    bool ends_with   = (pattern.front() == '*');

    // Extract the literal part of the pattern
    std::string literal = pattern;
    if (starts_with) literal.pop_back();
    if (ends_with) literal.erase(0, 1);

    if (literal.empty()) return true; // "*" matches everything

    if (starts_with && ends_with)
    {
        // *literal* - contains
        return text.find(literal) != std::string::npos;
    }
    else if (starts_with)
    {
        // literal* - prefix match
        return text.substr(0, literal.length()) == literal;
    }
    else if (ends_with)
    {
        // *literal - suffix match
        return text.length() >= literal.length() && text.substr(text.length() - literal.length()) == literal;
    }
    else
    {
        // literal - exact match
        return text == literal;
    }
}

} // namespace detail

} // namespace slwoggy