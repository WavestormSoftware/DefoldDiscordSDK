// Copyright 2023-2026
// DefoldDiscordSDK Defold extension
//
// Runtime loader for the Discord Social SDK native library
// (discord_partner_sdk .dll/.so/.dylib).
//
// Search order (first existing + loadable wins):
//   1. `[defolddiscordsdk] lib_path` from game.project.
//      Relative values are resolved against the current working directory.
//   2. Bundle relative: the executable directory itself, the classic
//      x86_64-win32 / x86_64-osx / x86_64-linux resource subfolders next to it,
//      and for macOS also ../Frameworks relative to Contents/MacOS.
//   3. The current working directory.
//   4. ./defolddiscordsdk/res/<arch>/ relative to the working directory
//      (x86_64-win32, x86_64-osx, x86_64-linux; see the placeholder files).
//
// The library is loaded without Alpine-style global symbol exposure
// (RTLD_LOCAL-ish semantics): the game must never accidentally import SDK
// symbols. Krisp noise cancellation support libraries are optional: when the
// krisp binary sits next to the partner sdk it is preloaded by full path so
// the partner sdk can load it implicitly by short name; when it is missing
// (for example on Linux where krisp may not ship) loading continues and
// noise cancellation degrades gracefully at the API layer.

#ifndef DISCORD_SOCIAL_LOADER_H
#define DISCORD_SOCIAL_LOADER_H

#include <dmsdk/sdk.h>

struct DiscordSocialCapi;

namespace discordsdk_loader {

// Loads the partner sdk library once per process. Thread-unsafe by design:
// call from extension Initialize on the main thread.
bool Load(dmConfigFile::HConfig config);

// Releases the library handle. Never call SDK entry points afterwards.
void Unload();

bool IsLoaded();

// True when a krisp support library was found next to the partner sdk.
bool IsKrispPresent();

// The resolved absolute path of the partner sdk binary, for diagnostics.
const char* LibraryPath();

// Typed symbol table for every bound entry point. Only valid while IsLoaded.
const DiscordSocialCapi* GetCapi();

} // namespace discordsdk_loader

#endif // DISCORD_SOCIAL_LOADER_H
