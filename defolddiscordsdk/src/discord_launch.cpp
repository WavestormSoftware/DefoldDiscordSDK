// Copyright (c) 2026 Wavestorm Software
// DefoldDiscordSDK Defold extension
//
// See discord_launch.h for the contract.

#include "discord_launch.h"

#include <string.h>

#include <dmsdk/sdk.h>

#include "discord_util.h"

#if defined(DM_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(DM_PLATFORM_LINUX)
#include <stdio.h>
#include <unistd.h>
#elif defined(DM_PLATFORM_OSX)
#include <crt_externs.h>
#include <string.h>
#endif

namespace discordsdk_launch {

namespace {

// Data-driven candidate tokens. Extend these lists as more launch parameter
// formats surface; unknown tokens always reach Lua through the raw table.
const char* kAppIdTokens[] = {
    "--discord-app-id",
    "--discord-appid",
    "--app-id",
};
const char* kJoinTokens[] = {
    "--discord-join",
    "--discord-join-secret",
};
const char* kSpectateTokens[] = {
    "--discord-spectate",
    "--discord-spectate-secret",
};

bool MatchToken(const char* const* list, size_t list_count, const std::string& token_lower)
{
    for (size_t i = 0; i < list_count; ++i) {
        const std::string candidate = discordsdk::ToLower(std::string(list[i]));
        if (token_lower.compare(0, candidate.size(), candidate) == 0) {
            return true;
        }
    }
    return false;
}

void ParseArguments(const std::vector<std::string>& arguments, LaunchArgs* out)
{
    out->m_RawArguments = arguments;

    for (size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        if (argument.size() < 3 || argument[0] != '-' || argument[1] != '-') {
            continue;
        }
        // Separate key=value and key value forms.
        std::string key = argument;
        std::string value;
        bool inline_value = false;

        size_t eq = argument.find('=');
        if (eq != std::string::npos) {
            key = argument.substr(0, eq);
            value = argument.substr(eq + 1);
            inline_value = true;
        }

        const std::string key_lower = discordsdk::ToLower(key);

        uint64_t app_id = 0;
        if (MatchToken(kAppIdTokens, sizeof(kAppIdTokens) / sizeof(kAppIdTokens[0]), key_lower)) {
            std::string candidate = value;
            bool have_value = inline_value;
            if (!have_value && i + 1 < arguments.size()) {
                candidate = arguments[i + 1];
                have_value = !candidate.empty() && candidate[0] != '-';
            }
            if (have_value && discordsdk::ParseUint64(candidate, &app_id)) {
                out->m_HasAppId = true;
                out->m_AppId = app_id;
                if (!inline_value) {
                    ++i; // consume the trailing value token
                }
            }
        } else if (MatchToken(kJoinTokens, sizeof(kJoinTokens) / sizeof(kJoinTokens[0]), key_lower)) {
            std::string candidate = value;
            bool have_value = inline_value;
            if (!have_value && i + 1 < arguments.size()) {
                candidate = arguments[i + 1];
                have_value = !candidate.empty() && candidate[0] != '-';
            }
            if (have_value) {
                out->m_HasJoinSecret = true;
                out->m_JoinSecret = candidate;
                if (!inline_value) {
                    ++i;
                }
            }
        } else if (MatchToken(kSpectateTokens, sizeof(kSpectateTokens) / sizeof(kSpectateTokens[0]), key_lower)) {
            std::string candidate = value;
            bool have_value = inline_value;
            if (!have_value && i + 1 < arguments.size()) {
                candidate = arguments[i + 1];
                have_value = !candidate.empty() && candidate[0] != '-';
            }
            if (have_value) {
                out->m_HasSpectateSecret = true;
                out->m_SpectateSecret = candidate;
                if (!inline_value) {
                    ++i;
                }
            }
        }
    }
}

#if defined(DM_PLATFORM_WINDOWS)

bool ReadSystemArguments(std::vector<std::string>* out)
{
    int count = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv || count <= 0) {
        if (argv) {
            LocalFree(argv);
        }
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const wchar_t* wide = argv[i];
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
        if (utf8_len > 0) {
            std::vector<char> utf8(utf8_len);
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, &utf8[0], utf8_len, NULL, NULL);
            out->push_back(std::string(&utf8[0]));
        }
    }
    LocalFree(argv);
    return true;
}

#elif defined(DM_PLATFORM_LINUX)

bool ReadSystemArguments(std::vector<std::string>* out)
{
    FILE* file = fopen("/proc/self/cmdline", "rb");
    if (!file) {
        return false;
    }
    std::vector<char> buffer(4096);
    size_t read = 0;
    std::string current;
    while ((read = fread(&buffer[0], 1, buffer.size(), file)) > 0) {
        for (size_t i = 0; i < read; ++i) {
            if (buffer[i] == '\0') {
                out->push_back(current);
                current.clear();
            } else {
                current += buffer[i];
            }
        }
    }
    if (!current.empty()) {
        out->push_back(current);
    }
    fclose(file);
    return out->size() > 0;
}

#elif defined(DM_PLATFORM_OSX)

bool ReadSystemArguments(std::vector<std::string>* out)
{
    int* argc = _NSGetArgc();
    char*** argv = _NSGetArgv();
    if (!argc || !argv || !*argv || *argc <= 0) {
        return false;
    }
    for (int i = 0; i < *argc; ++i) {
        if ((*argv)[i]) {
            out->push_back(std::string((*argv)[i]));
        }
    }
    return true;
}

#else

bool ReadSystemArguments(std::vector<std::string>*) { return false; }

#endif

LaunchArgs g_LaunchArgs;
bool g_Parsed = false;
bool g_ParseLogged = false;

} // unnamed namespace

const LaunchArgs* GetLaunchArgs()
{
    if (!g_Parsed) {
        std::vector<std::string> arguments;
        if (!ReadSystemArguments(&arguments) && !g_ParseLogged) {
            g_ParseLogged = true;
            dmLogWarning("DefoldDiscordSDK: could not read the process command line; launch"
                         " arguments are unavailable on this platform.");
        }
        ParseArguments(arguments, &g_LaunchArgs);
        g_Parsed = true;
    }
    return &g_LaunchArgs;
}

std::string Describe(const LaunchArgs& args)
{
    std::string summary;
    if (args.m_HasAppId) {
        summary += "app_id:";
        summary += discordsdk::Uint64ToString(args.m_AppId);
    }
    if (args.m_HasJoinSecret) {
        if (!summary.empty()) {
            summary += " ";
        }
        summary += "join:yes";
    }
    if (args.m_HasSpectateSecret) {
        if (!summary.empty()) {
            summary += " ";
        }
        summary += "spectate:yes";
    }
    return summary;
}

} // namespace discordsdk_launch
