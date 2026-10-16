// Copyright 2023-2026
// DefoldDiscordSDK Defold extension
//
// Command line parsing for Discord launch flow arguments.
//
// When a user joins an activity through Discord and the game is launched by
// Discord via the registered launch command, Discord appends launch arguments
// to the new process. The exact argument names are not documented in the SDK
// headers, so parsing here is data driven: the candidate token lists below are
// the recognised set, everything else (including unknown tokens) is surfaced
// untouched through the raw token table exposed to Lua.
//
// Recognised structured forms:
//   --discord-app-id=1234 | --discord-app-id 1234 (+# remaining candidates)
//   --discord-join=SECRET | --discord-join SECRET
//   --discord-spectate=SECRET | --discord-spectate SECRET
//   --discord-appid=1234, --discord-joinSecret=..., --discord-spectateSecret=...
//
// Windows reads the command line with GetCommandLineW; Linux reads
// /proc/self/cmdline; macOS reads _NSGetArgv/_NSGetArgc. The parser itself is
// platform neutral and runs once at Initialize.

#ifndef DISCORD_SOCIAL_LAUNCH_H
#define DISCORD_SOCIAL_LAUNCH_H

#include <stdint.h>

#include <string>
#include <vector>

namespace discordsdk_launch {

struct LaunchArgs
{
    bool m_HasAppId;
    bool m_HasJoinSecret;
    bool m_HasSpectateSecret;
    uint64_t m_AppId;
    std::string m_JoinSecret;
    std::string m_SpectateSecret;
    std::vector<std::string> m_RawArguments;

    LaunchArgs()
        : m_HasAppId(false)
        , m_HasJoinSecret(false)
        , m_HasSpectateSecret(false)
        , m_AppId(0)
    {}
};

// Parses the current process command line. Safe to call repeatedly; parsing
// happens once and the result is cached.
const LaunchArgs* GetLaunchArgs();

// Human readable summary for diagnostics (used in logs and version output).
std::string Describe(const LaunchArgs& args);

} // namespace discordsdk_launch

#endif // DISCORD_SOCIAL_LAUNCH_H
