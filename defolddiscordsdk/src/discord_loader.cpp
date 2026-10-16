// Copyright 2023-2026
// DefoldDiscordSDK Defold extension
//
// See discord_loader.h for the search-order contract and krisp policy.

#include "discord_loader.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "discordsdk_capi.h"
#include "discord_util.h"

// ---------------------------------------------------------------------------
// Supported desktop platforms only. Everything else compiles into the
// graceful no-op variant at the bottom of this file (HTML5, Android, iOS).
// ---------------------------------------------------------------------------
#if defined(DM_PLATFORM_WINDOWS) || defined(DM_PLATFORM_OSX) || defined(DM_PLATFORM_LINUX)
#define DISCORD_SOCIAL_LOADER_SUPPORTED 1

#if defined(DM_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <unistd.h>
#include <dlfcn.h>
#if defined(DM_PLATFORM_OSX)
#include <mach-o/dyld.h>
#endif
#endif

#if defined(DM_PLATFORM_WINDOWS)
#define DISCORD_SOCIAL_PARTNER_LIB "discord_partner_sdk.dll"
#define DISCORD_SOCIAL_KRISP_LIB "discord_krisp.dll"
#elif defined(DM_PLATFORM_OSX)
#define DISCORD_SOCIAL_PARTNER_LIB "libdiscord_partner_sdk.dylib"
#define DISCORD_SOCIAL_KRISP_LIB "libdiscord_krisp.dylib"
#else
#define DISCORD_SOCIAL_PARTNER_LIB "libdiscord_partner_sdk.so"
#define DISCORD_SOCIAL_KRISP_LIB "libdiscord_krisp.so"
#endif

namespace discordsdk_loader {

struct DiscordSocialLoaderState
{
    void* m_Module;
    DiscordSocialCapi m_Capi;
    bool m_KrispPresent;
    std::string m_LibraryPath;
    std::string m_KrispPath;

    DiscordSocialLoaderState() : m_Module(NULL), m_Capi(), m_KrispPresent(false), m_LibraryPath(), m_KrispPath() {}
};

static DiscordSocialLoaderState g_State;

static const char* GetPlatformResFolder()
{
#if defined(DM_PLATFORM_WINDOWS)
    return "x86_64-win32";
#elif defined(DM_PLATFORM_OSX)
    return "x86_64-osx";
#else
    return "x86_64-linux";
#endif
}

static bool FileExists(const std::string& path)
{
#if defined(DM_PLATFORM_WINDOWS)
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES;
#else
    return access(path.c_str(), R_OK) == 0;
#endif
}

static std::string GetExecutableDirectory()
{
#if defined(DM_PLATFORM_WINDOWS)
    char buffer[4096];
    DWORD length = GetModuleFileNameA(NULL, buffer, (DWORD)(sizeof(buffer) - 1));
    if (length == 0 || length >= sizeof(buffer) - 1) {
        return std::string();
    }
    buffer[length] = '\0';
    return discordsdk::DirName(std::string(buffer));
#elif defined(DM_PLATFORM_OSX)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    std::vector<char> buffer(size + 1);
    if (_NSGetExecutablePath(&buffer[0], &size) != 0) {
        return std::string();
    }
    return discordsdk::DirName(std::string(&buffer[0]));
#else
    char buffer[4096];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return std::string();
    }
    buffer[length] = '\0';
    return discordsdk::DirName(std::string(buffer));
#endif
}

static std::string GetCurrentWorkingDirectory()
{
#if defined(DM_PLATFORM_WINDOWS)
    char buffer[4096];
    DWORD length = GetCurrentDirectoryA((DWORD)(sizeof(buffer) - 1), buffer);
    if (length == 0 || length >= sizeof(buffer) - 1) {
        return std::string();
    }
    buffer[length] = '\0';
    return std::string(buffer);
#else
    char buffer[4096];
    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        return std::string();
    }
    return std::string(buffer);
#endif
}

static void* OpenNative(const std::string& path, std::string* error_out)
{
#if defined(DM_PLATFORM_WINDOWS)
    // LOAD_WITH_ALTERED_SEARCH_PATH makes dependent libraries (krisp) resolve
    // from the loaded library's own directory, not only the exe directory.
    HMODULE handle = LoadLibraryExA(path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!handle && error_out) {
        char message[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                       GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), message, sizeof(message), NULL);
        *error_out = message;
    }
    return handle;
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle && error_out) {
        const char* err = dlerror();
        *error_out = err ? err : "unknown dlopen error";
    }
    return handle;
#endif
}

static void* GetSymbolDispatch(void* module, const char* name)
{
#if defined(DM_PLATFORM_WINDOWS)
    return (void*)GetProcAddress((HMODULE)module, name);
#else
    return dlsym(module, name);
#endif
}

static void FreeNative(void* module)
{
    if (!module) {
        return;
    }
#if defined(DM_PLATFORM_WINDOWS)
    FreeLibrary((HMODULE)module);
#else
    dlclose(module);
#endif
}

static void ResolveLogMissing(const char* name, const char* reason)
{
    dmLogWarning("DefoldDiscordSDK: symbol lookup failed for %s (%s)", name, reason);
}

static void CloseModuleIfNeeded()
{
    if (g_State.m_Module) {
        FreeNative(g_State.m_Module);
        g_State.m_Module = NULL;
    }
    g_State.m_Capi = DiscordSocialCapi();
    g_State.m_Capi.m_Loaded = false;
}

// Collects every directory the loader probes, most promising first.
static void BuildSearchDirs(const char* config_lib_path, std::vector<std::string>* dirs)
{
    if (config_lib_path && config_lib_path[0]) {
        dirs->push_back(std::string(config_lib_path));
    }

    std::string exe_dir = GetExecutableDirectory();
    if (!exe_dir.empty()) {
        // The executable directory itself (bundle Contents/MacOS on macOS).
        dirs->push_back(exe_dir);
#if defined(DM_PLATFORM_OSX)
        // .app bundle style: dylibs commonly sit in Contents/Frameworks.
        dirs->push_back(discordsdk::JoinPath(exe_dir, "../Frameworks"));
#endif
        // Classic resource subfolder next to the binary.
        dirs->push_back(discordsdk::JoinPath(exe_dir, GetPlatformResFolder()));
    }

    std::string cwd = GetCurrentWorkingDirectory();
    if (!cwd.empty()) {
        dirs->push_back(cwd);
        dirs->push_back(discordsdk::JoinPath(cwd, GetPlatformResFolder()));
        dirs->push_back(discordsdk::JoinPath(discordsdk::JoinPath(cwd, "defolddiscordsdk"), GetPlatformResFolder()));
    }
}

// Turns the search directory list into concrete candidate file paths. When the
// configured lib_path points directly at the library file, both the file and
// its directory are used.
static void BuildCandidateLists(const char* config_lib_path,
                                std::vector<std::string>* partner_candidates,
                                std::vector<std::string>* krisp_candidates)
{
    std::vector<std::string> dirs;
    BuildSearchDirs(config_lib_path, &dirs);

    std::vector<std::string> unique_dirs;
    for (size_t i = 0; i < dirs.size(); ++i) {
        bool duplicate = false;
        for (size_t j = 0; j < unique_dirs.size(); ++j) {
            if (unique_dirs[j] == dirs[i]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            unique_dirs.push_back(dirs[i]);
        }
    }

    for (size_t i = 0; i < unique_dirs.size(); ++i) {
        const std::string& dir = unique_dirs[i];
        const std::string base = discordsdk::BaseName(dir);
        if (base == DISCORD_SOCIAL_PARTNER_LIB) {
            // Configuration pointed at the library file itself.
            const std::string file_dir = discordsdk::DirName(dir);
            if (!file_dir.empty()) {
                krisp_candidates->push_back(discordsdk::JoinPath(file_dir, DISCORD_SOCIAL_KRISP_LIB));
            }
            continue;
        }
        partner_candidates->push_back(discordsdk::JoinPath(dir, DISCORD_SOCIAL_PARTNER_LIB));
        krisp_candidates->push_back(discordsdk::JoinPath(dir, DISCORD_SOCIAL_KRISP_LIB));
    }
}

// Preloads krisp from a directory that also contains the partner sdk so the
// partner sdk can bind to it implicitly by short name later.
static void TryPreloadKrisp(const std::string& partner_path, const std::vector<std::string>& krisp_candidates)
{
    const std::string partner_dir = discordsdk::DirName(partner_path);
    for (size_t i = 0; i < krisp_candidates.size(); ++i) {
        const std::string& krisp_path = krisp_candidates[i];
        if (discordsdk::DirName(krisp_path) != partner_dir || !FileExists(krisp_path)) {
            continue;
        }
        g_State.m_KrispPresent = true;
        g_State.m_KrispPath = krisp_path;
        std::string error;
        void* krisp_module = OpenNative(krisp_path, &error);
        if (!krisp_module) {
            dmLogWarning("DefoldDiscordSDK: krisp library found but failed to load (%s): %s",
                         krisp_path.c_str(), error.c_str());
            g_State.m_KrispPresent = false;
            g_State.m_KrispPath.clear();
        } else {
            // Left open intentionally for the lifetime of the process.
            return;
        }
    }
}

bool Load(dmConfigFile::HConfig config)
{
    if (g_State.m_Module) {
        return true;
    }

    const char* lib_path = dmConfigFile::GetString(config, "defolddiscordsdk.lib_path", "");
    std::vector<std::string> partner_candidates;
    std::vector<std::string> krisp_candidates;
    BuildCandidateLists(lib_path, &partner_candidates, &krisp_candidates);

    for (size_t i = 0; i < partner_candidates.size(); ++i) {
        const std::string& candidate = partner_candidates[i];
        if (!FileExists(candidate)) {
            continue;
        }
        if (!g_State.m_KrispPresent) {
            TryPreloadKrisp(candidate, krisp_candidates);
        }

        std::string error;
        void* module = OpenNative(candidate, &error);
        if (!module) {
            dmLogWarning("DefoldDiscordSDK: failed to load %s: %s", candidate.c_str(), error.c_str());
            continue;
        }

        DiscordSocialCapi capi;
        memset(&capi, 0, sizeof(capi));
        if (!DiscordSocialCapi_Resolve(&capi, module, GetSymbolDispatch, ResolveLogMissing)) {
            dmLogWarning("DefoldDiscordSDK: %s loaded but does not export the expected discord partner sdk"
                         " symbols (incompatible library build); trying remaining candidates",
                         candidate.c_str());
            FreeNative(module);
            continue;
        }

        g_State.m_Module = module;
        g_State.m_Capi = capi;
        g_State.m_Capi.m_Loaded = true;
        g_State.m_LibraryPath = candidate;
        dmLogInfo("DefoldDiscordSDK: loaded %s from %s", DISCORD_SOCIAL_PARTNER_LIB, candidate.c_str());
        return true;
    }

    dmLogWarning("DefoldDiscordSDK: " DISCORD_SOCIAL_PARTNER_LIB
                 " not found. Copy it next to the game binary, set [defolddiscordsdk] lib_path in"
                 " game.project, or place it under defolddiscordsdk/res/<arch>/. The extension runs"
                 " in degraded no-op mode until the library is available.");
    CloseModuleIfNeeded();
    return false;
}

void Unload()
{
    CloseModuleIfNeeded();
    g_State.m_KrispPresent = false;
    g_State.m_KrispPath.clear();
}

bool IsLoaded()
{
    return g_State.m_Module != NULL;
}

bool IsKrispPresent()
{
    return g_State.m_KrispPresent;
}

const char* LibraryPath()
{
    return g_State.m_LibraryPath.c_str();
}

const DiscordSocialCapi* GetCapi()
{
    return IsLoaded() ? &g_State.m_Capi : NULL;
}

} // namespace discordsdk_loader

#else // !DISCORD_SOCIAL_LOADER_SUPPORTED

namespace discordsdk_loader {

bool Load(dmConfigFile::HConfig)
{
    static bool warned = false;
    if (!warned) {
        warned = true;
        dmLogWarning("DefoldDiscordSDK: this platform is not supported by the Discord Social SDK native"
                     " library; extension functions behave as stubs on this platform.");
    }
    return false;
}

void Unload() {}

bool IsLoaded() { return false; }

bool IsKrispPresent() { return false; }

const char* LibraryPath() { return ""; }

const DiscordSocialCapi* GetCapi() { return NULL; }

} // namespace discordsdk_loader

#endif // DISCORD_SOCIAL_LOADER_SUPPORTED
