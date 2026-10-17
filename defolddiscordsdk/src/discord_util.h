// Copyright (c) 2026 Wavestorm Software
// DefoldDiscordSDK Defold extension
//
// Small platform-neutral helpers shared by the DefoldDiscordSDK extension
// modules. Everything here is inline so the header carries no state.

#ifndef DISCORD_SOCIAL_UTIL_H
#define DISCORD_SOCIAL_UTIL_H

#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#if defined(_WIN32)
#define DISCORD_SOCIAL_PATH_SEP '\\'
#else
#define DISCORD_SOCIAL_PATH_SEP '/'
#endif

namespace discordsdk {

// Joins two path components with the platform separator. Empty components are
// tolerated; a trailing separator is not duplicated.
inline std::string JoinPath(const std::string& a, const std::string& b)
{
    std::string result;
    if (!a.empty()) {
        result = a;
    }
    if (!b.empty()) {
        if (!a.empty() && a[a.size() - 1] != DISCORD_SOCIAL_PATH_SEP) {
            result += DISCORD_SOCIAL_PATH_SEP;
        }
        result += b;
    }
    return result;
}

// Returns the directory part of a path (everything before the last separator).
// Returns an empty string when no separator is present.
inline std::string DirName(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return std::string();
    }
    return path.substr(0, pos);
}

// Returns the file name part of a path (everything after the last separator).
inline std::string BaseName(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return path;
    }
    if (pos + 1 < path.size()) {
        return path.substr(pos + 1);
    }
    return std::string();
}

// Case-insensitive ends-with check used for suffix probing.
inline bool EndsWithNoCase(const std::string& value, const char* suffix)
{
    size_t len = strlen(suffix);
    if (value.size() < len) {
        return false;
    }
    const char* tail = value.c_str() + (value.size() - len);
    for (size_t i = 0; i < len; ++i) {
        char c = tail[i];
        char s = suffix[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + 'a' - 'A');
        }
        if (s >= 'A' && s <= 'Z') {
            s = (char)(s + 'a' - 'A');
        }
        if (c != s) {
            return false;
        }
    }
    return true;
}

inline std::string ToLower(std::string value)
{
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (c >= 'A' && c <= 'Z') {
            value[i] = (char)(c + 'a' - 'A');
        }
    }
    return value;
}

// Strict unsigned integer parsing. All characters must be digits and empty
// input fails; overflow saturates to failure.
inline bool ParseUint64(const char* text, uint64_t* out)
{
    if (!text || !out) {
        return false;
    }
    const char* p = text;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '\0' || *p == '-') {
        return false;
    }
    uint64_t value = 0;
    while (*p >= '0' && *p <= '9') {
        uint64_t digit = (uint64_t)(*p - '0');
        if (value > ((~(uint64_t)0) - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
        ++p;
    }
    if (*p != '\0') {
        return false;
    }
    *out = value;
    return true;
}

inline bool ParseUint64(const std::string& text, uint64_t* out)
{
    return ParseUint64(text.c_str(), out);
}

inline bool ParseUint32(const char* text, uint32_t* out)
{
    uint64_t v = 0;
    if (!ParseUint64(text, &v) || v > 0xFFFFFFFFull) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

inline bool ParseUint32(const std::string& text, uint32_t* out)
{
    return ParseUint32(text.c_str(), out);
}

// Accepts "1", "true", "yes", "on" (case-insensitive) as true.
inline bool ParseBool(const char* text, bool default_value)
{
    if (!text || !text[0]) {
        return default_value;
    }
    std::string t = ToLower(text);
    if (t == "1" || t == "true" || t == "yes" || t == "on") {
        return true;
    }
    if (t == "0" || t == "false" || t == "no" || t == "off") {
        return false;
    }
    return default_value;
}

// Splits on any whitespace run; empty tokens are skipped. Produces exactly
// what scope lists like DefaultPresenceScopes ("identify rpc ...") need.
inline void SplitWhitespace(const std::string& text, std::vector<std::string>* out)
{
    out->clear();
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
            ++i;
        }
        if (i >= n) {
            break;
        }
        size_t start = i;
        while (i < n && !(text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
            ++i;
        }
        out->push_back(text.substr(start, i - start));
    }
}

inline std::string Uint64ToString(uint64_t value)
{
    // std::to_string raises no exceptions for uint64_t; it is a plain
    // formatting library routine and C++17 compliant.
    char buffer[32];
    int pos = sizeof(buffer) - 1;
    buffer[pos] = '\0';
    if (value == 0) {
        buffer[--pos] = '0';
    }
    while (value > 0) {
        buffer[--pos] = (char)('0' + (value % 10));
        value /= 10;
    }
    return std::string(&buffer[pos]);
}

} // namespace discordsdk

#endif // DISCORD_SOCIAL_UTIL_H
