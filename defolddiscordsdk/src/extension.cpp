// Copyright (c) 2026 Wavestorm Software
// DefoldDiscordSDK Defold extension
//
// Lua glue for the `discordsdk` module (see wiki/API-Reference.md, which is
// the surface contract). Structure follows the established Defold extension
// pattern: the SDK library is loaded once in InitializeExtension, the module
// table is registered through luaL_register under its plain name, and every
// function degrades gracefully (false, nil or an empty table plus a one-time
// log line) while the Discord Social SDK library is unavailable.
//
// Ownership and units:
//   - ids cross into Lua as strings (uint64 snowflakes surface exactly);
//   - timestamps cross as UNIX seconds; the SDK speaks milliseconds and the
//     conversion happens once at the push boundary (discord_client.cpp);
//   - join requests surface through activity_join_request because this SDK
//     build has no dedicated callback and no decline/ignore call; only
//     REPLY_YES issues SDK traffic (see the note in discord_client.cpp);
//   - auth order stays explicit: initialize/authorize set the tokens up, the
//     `authorized` event confirms the link and connect() is a separate step.

#define EXTENSION_NAME DefoldDiscordSDK
#define LIB_NAME "DefoldDiscordSDK"
// luaL_register with a plain name creates the global `discordsdk` table and
// registers package.loaded["discordsdk"], so the module is used directly as
// the global (no require nesting and no dotted path to remember).
#define MODULE_NAME "discordsdk"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include <dmsdk/sdk.h>

#include "discord_client.h"
#include "discord_events.h"
#include "discord_launch.h"
#include "discord_loader.h"
#include "discord_presence.h"
#include "discord_util.h"
#include "discordsdk_capi.h"

namespace {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// The [defolddiscordsdk] config kept from AppInitialize so initialize()'s Lua
// options can fall back onto it.
dmConfigFile::HConfig g_Config = 0;

// One-time stub warning: fires the first time a network capability is used
// while the SDK library is missing or the extension was disabled.
bool g_StubWarned = false;
bool g_ExplicitlyDisabled = false;

// Wiki scope presets; used when the library cannot answer the getters itself.
const char* k_PresenceScopePreset = "openid sdk.social_layer_presence";
const char* k_CommunicationScopePreset = "openid sdk.social_layer";

const char* k_StubWarning =
    "DefoldDiscordSDK: the Discord Social SDK library is not available; discordsdk.* calls act as"
    " stubs (network calls return false, getters empty) until the library is staged next to the"
    " game binary or [defolddiscordsdk] lib_path points at it.";

void WarnStubOnce(const char* for_what)
{
    if (!g_StubWarned) {
        g_StubWarned = true;
        dmLogWarning("%s (first stub call: %s)", k_StubWarning, for_what ? for_what : "unknown");
    }
}

bool Available()
{
    return discordsdk_loader::IsLoaded() && discordsdk_client::IsRunning();
}

// Shared guard; stub mode returns false and warns exactly once.
bool NeedSdk(const char* for_what)
{
    if (!g_ExplicitlyDisabled && Available()) {
        return true;
    }
    WarnStubOnce(for_what);
    return false;
}

// ---------------------------------------------------------------------------
// Lua helpers
// ---------------------------------------------------------------------------

// Snowflakes arrive as strings (uint64 does not round trip through Lua
// numbers); numbers are tolerated.
uint64_t ParseSnowflake(lua_State* L, int index, const char* argument_name)
{
    if (lua_isnumber(L, index)) {
        return (uint64_t)lua_tonumber(L, index);
    }
    const char* text = luaL_checkstring(L, index);
    uint64_t value = 0;
    if (!discordsdk::ParseUint64(text, &value)) {
        luaL_error(L, "%s must be a Discord snowflake id string", argument_name);
    }
    return value;
}

// Optional snowflake field inside a table (initialize options).
bool ParseSnowflakeOption(lua_State* L, int table, const char* key, uint64_t* out)
{
    *out = 0;
    lua_getfield(L, table, key);
    bool parsed = false;
    if (lua_isnumber(L, -1)) {
        *out = (uint64_t)lua_tonumber(L, -1);
        parsed = *out != 0;
    } else if (lua_isstring(L, -1)) {
        if (!discordsdk::ParseUint64(lua_tostring(L, -1), out)) {
            luaL_error(L, "%s must be a Discord application id (snowflake string)", key);
        }
        parsed = true;
    }
    lua_pop(L, 1);
    return parsed;
}

bool ParseBoolOption(lua_State* L, int table, const char* key, bool default_value, bool* changed)
{
    lua_getfield(L, table, key);
    bool value = default_value;
    bool given = false;
    if (lua_isboolean(L, -1)) {
        value = lua_toboolean(L, -1) != 0;
        given = true;
    } else if (lua_isstring(L, -1)) {
        value = discordsdk::ParseBool(lua_tostring(L, -1), default_value);
        given = true;
    }
    lua_pop(L, 1);
    if (changed) {
        *changed = given;
    }
    return value;
}

// The output string receives its content immediately, copied out of Lua's
// stack; no std::string temporary lives across the Lua calls. Returns
// whether the option was provided as a string value.
bool ParseStringOption(lua_State* L, int table, const char* key, std::string* out)
{
    out->clear();
    lua_getfield(L, table, key);
    bool present = lua_isstring(L, -1) != 0;
    if (present) {
        size_t length = 0;
        const char* text = lua_tolstring(L, -1, &length);
        out->assign(text, length);
    }
    lua_pop(L, 1);
    return present;
}

// The scopes option accepts the preset names, a raw separation string, or an
// array of scope strings; the result is the space separated list the SDK
// AuthorizationArgs wants.
bool ParseScopesOption(lua_State* L, int table, const char* key, std::string* out)
{
    out->clear();
    lua_getfield(L, table, key);
    bool present = false;
    if (lua_istable(L, -1)) {
        present = true;
        int count = 0;
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_isstring(L, -1)) {
                if (count > 0) {
                    out->push_back(' ');
                }
                size_t length = 0;
                const char* scope = lua_tolstring(L, -1, &length);
                out->append(scope, length);
                ++count;
            }
            lua_pop(L, 1);
        }
    } else if (lua_isstring(L, -1)) {
        present = true;
        size_t length = 0;
        const char* text = lua_tolstring(L, -1, &length);
        out->assign(text, length);
    }
    lua_pop(L, 1);
    return present;
}

// Collects string keys/values from a Lua table into a std::map; numbers are
// stringified, everything else raises.
void ReadStringMap(lua_State* L, int index, std::map<std::string, std::string>* out)
{
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            luaL_error(L, "metadata keys must be strings");
        }
        const char* key = lua_tostring(L, -2);
        if (!lua_isstring(L, -1)) {
            luaL_error(L, "metadata values must be strings or numbers (key '%s')", key);
        }
        (*out)[key] = std::string(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

// ---------------------------------------------------------------------------
// Module implementations
// ---------------------------------------------------------------------------

int InitializeLua(lua_State* L)
{
    discordsdk_client::Options options;
    bool enabled = true;

    if (lua_istable(L, 1)) {
        ParseSnowflakeOption(L, 1, "application_id", &options.m_ApplicationId);
        ParseScopesOption(L, 1, "scopes", &options.m_Scopes);
        options.m_RegisterLaunchCommand =
            ParseBoolOption(L, 1, "auto_register_launch", options.m_RegisterLaunchCommand, 0);
        ParseStringOption(L, 1, "default_redirect", &options.m_DefaultRedirect);
        uint64_t steam_option = 0;
        ParseSnowflakeOption(L, 1, "steam_app_id", &steam_option);
        if (steam_option > 0xFFFFFFFFull) {
            luaL_error(L, "steam_app_id does not fit a Steam application id");
        }
        options.m_SteamAppId = (uint32_t)steam_option;
        enabled = ParseBoolOption(L, 1, "enabled", true, 0);
    }

    // game.project fallbacks (initialize() values win).
    if (options.m_ApplicationId == 0) {
        const char* config_id =
            dmConfigFile::GetString(g_Config, "defolddiscordsdk.application_id", "");
        discordsdk::ParseUint64(config_id, &options.m_ApplicationId);
    }
    if (options.m_SteamAppId == 0) {
        discordsdk::ParseUint32(dmConfigFile::GetString(g_Config,
                                                            "defolddiscordsdk.steam_app_id", ""),
                                    &options.m_SteamAppId);
    }
    if (options.m_DefaultRedirect.empty()) {
        options.m_DefaultRedirect.assign(dmConfigFile::GetString(g_Config,
                                                                 "defolddiscordsdk.default_redirect", ""));
    }
    if (options.m_Scopes.empty()) {
        const char* scopes = dmConfigFile::GetString(g_Config, "defolddiscordsdk.scopes", "");
        if (scopes[0] == '\0' || strcmp(scopes, "presence") == 0) {
            options.m_Scopes.assign(k_PresenceScopePreset);
        } else if (strcmp(scopes, "communication") == 0) {
            options.m_Scopes.assign(k_CommunicationScopePreset);
        } else {
            options.m_Scopes.assign(scopes);
        }
    }
    options.m_RegisterLaunchSteam = options.m_SteamAppId != 0;

    g_ExplicitlyDisabled = !enabled;
    if (!enabled) {
        WarnStubOnce("initialize (enabled = false)");
        lua_pushboolean(L, 0);
        return 1;
    }

    const DiscordSocialCapi* capi = discordsdk_loader::GetCapi();
    if (!capi) {
        WarnStubOnce("initialize");
        lua_pushboolean(L, 0);
        return 1;
    }

    bool created = discordsdk_client::Create(capi, options);
    if (created) {
        discordsdk_presence::SetTarget(capi, discordsdk_client::GetClient());
        // Nothing connects here: authorize()/set_token() complete into the
        // `authorized` event and connect() remains the explicit next step.
        dmLogInfo("DefoldDiscordSDK: initialized (application %s / steam %s)",
                  discordsdk::Uint64ToString(options.m_ApplicationId).c_str(),
                  options.m_SteamAppId != 0
                      ? discordsdk::Uint64ToString(options.m_SteamAppId).c_str()
                      : "none");
    }
    lua_pushboolean(L, created ? 1 : 0);
    return 1;
}

int ConnectLua(lua_State* L)
{
    if (!NeedSdk("connect")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    discordsdk_client::Connect();
    lua_pushboolean(L, 1);
    return 1;
}

int DisconnectLua(lua_State* L)
{
    if (!NeedSdk("disconnect")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    discordsdk_client::Disconnect();
    lua_pushboolean(L, 1);
    return 1;
}

int GetStatusLua(lua_State* L)
{
    if (!NeedSdk("get_status")) {
        lua_pushlstring(L, "", 0);
        return 1;
    }
    std::string status = discordsdk_client::StatusName();
    lua_pushlstring(L, status.c_str(), status.size());
    return 1;
}

int IsReadyLua(lua_State* L)
{
    lua_pushboolean(L, discordsdk_client::IsReady() ? 1 : 0);
    return 1;
}

int IsAvailableLua(lua_State* L)
{
    lua_pushboolean(L, (!g_ExplicitlyDisabled && Available()) ? 1 : 0);
    return 1;
}

int GetCurrentUserLua(lua_State* L)
{
    if (!NeedSdk("get_current_user")) {
        lua_pushnil(L);
        return 1;
    }
    discordsdk_client::UserSnap user;
    if (!discordsdk_client::GetCurrentUserSnap(&user)) {
        lua_pushnil(L);
        return 1;
    }
    discordsdk_client::PushUser(L, user);
    return 1;
}

std::string SdkVersionString()
{
    const DiscordSocialCapi* capi = discordsdk_loader::GetCapi();
    char buffer[48];
    buffer[0] = '\0';
    if (capi) {
        (void)snprintf(buffer, sizeof(buffer), "%d.%d.%d", capi->Discord_Client_GetVersionMajor(),
                       capi->Discord_Client_GetVersionMinor(), capi->Discord_Client_GetVersionPatch());
    }
    return std::string(buffer);
}

int VersionLua(lua_State* L)
{
    std::string sdk = SdkVersionString();
    std::string text = std::string("DefoldDiscordSDK 1.0.5 (Social SDK ") +
                       (sdk.empty() ? "library not loaded" : sdk.c_str()) + ")";
    lua_pushlstring(L, text.c_str(), text.size());
    return 1;
}

int SetCallbackLua(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    int kind = 0;
    if (!DiscordSocialEvents_KindFromName(name, &kind)) {
        dmLogWarning("DefoldDiscordSDK: unknown event name '%s'", name);
        lua_pushboolean(L, 0);
        return 1;
    }
    DiscordSocialEvents_SetHandler(L, kind, 2);
    lua_pushboolean(L, 1);
    return 1;
}

int UpdatePresenceLua(lua_State* L)
{
    if (!NeedSdk("update_presence")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_gettop(L) < 1 || !lua_istable(L, 1)) {
        luaL_error(L, "update_presence expects an activity table");
    }
    lua_pushboolean(L, discordsdk_presence::Update(L, 1) ? 1 : 0);
    return 1;
}

int ClearPresenceLua(lua_State* L)
{
    if (!NeedSdk("clear_presence")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    discordsdk_presence::Clear();
    lua_pushboolean(L, 1);
    return 1;
}

int SendInviteLua(lua_State* L)
{
    if (!NeedSdk("send_invite")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint64_t user_id = ParseSnowflake(L, 1, "user_id");
    // This SDK build's SendActivityInvite carries no type argument (join
    // channel invites only); the documented type is accepted for shape and
    // flagged when it is not a plain join.
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2) && (int)lua_tonumber(L, 2) != 1) {
        dmLogWarning("DefoldDiscordSDK: send_invite supports join invites in this SDK build;"
                     " the invite_type argument is ignored");
    }
    const char* message = 0;
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
        message = luaL_checkstring(L, 3);
    }
    lua_pushboolean(L, discordsdk_client::SendInvite(user_id, message) ? 1 : 0);
    return 1;
}

int AcceptInviteLua(lua_State* L)
{
    if (!NeedSdk("accept_invite")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint64_t user_id = ParseSnowflake(L, 1, "user_id");
    lua_pushboolean(L, discordsdk_client::AcceptInvite(user_id) ? 1 : 0);
    return 1;
}

int SendJoinRequestLua(lua_State* L)
{
    if (!NeedSdk("send_join_request")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint64_t user_id = ParseSnowflake(L, 1, "user_id");
    lua_pushboolean(L, discordsdk_client::SendJoinRequest(user_id) ? 1 : 0);
    return 1;
}

int SendJoinRequestReplyLua(lua_State* L)
{
    if (!NeedSdk("send_join_request_reply")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint64_t user_id = ParseSnowflake(L, 1, "user_id");
    int reply = (int)luaL_checknumber(L, 2);
    if (reply != 0 && reply != 1 && reply != 2) {
        luaL_error(L, "reply must be REPLY_NO (0), REPLY_YES (1) or REPLY_IGNORE (2)");
    }
    lua_pushboolean(L,
                    discordsdk_client::SendJoinRequestReplyForSender(user_id, reply) ? 1 : 0);
    return 1;
}

int GetLaunchArgsLua(lua_State* L)
{
    const discordsdk_launch::LaunchArgs* args = discordsdk_launch::GetLaunchArgs();
    lua_newtable(L);
    // Absent both fields when the game was not launched through Discord.
    if (args->m_HasJoinSecret) {
        lua_pushlstring(L, args->m_JoinSecret.c_str(), args->m_JoinSecret.size());
        lua_setfield(L, -2, "join");
    }
    if (args->m_HasSpectateSecret) {
        lua_pushlstring(L, args->m_SpectateSecret.c_str(), args->m_SpectateSecret.size());
        lua_setfield(L, -2, "spectate");
    }
    return 1;
}

int RegisterLaunchCommandLua(lua_State* L)
{
    if (!NeedSdk("register_launch_command")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const discordsdk_client::Options* options = discordsdk_client::GetOptions();
    if (options->m_ApplicationId == 0) {
        luaL_error(L, "register_launch_command requires a configured application id");
    }
    const char* command = luaL_checkstring(L, 1);
    std::string command_text(command);
    const DiscordSocialCapi* capi = discordsdk_loader::GetCapi();
    Discord_String command_arg;
    command_arg.ptr = (uint8_t*)command_text.data();
    command_arg.size = command_text.size();
    bool ok = capi->Discord_Client_RegisterLaunchCommand(discordsdk_client::GetClient(),
                                                         options->m_ApplicationId, command_arg);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int RegisterSteamLua(lua_State* L)
{
    if (!NeedSdk("register_steam")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint64_t steam_id = ParseSnowflake(L, 1, "steam_app_id");
    const discordsdk_client::Options* options = discordsdk_client::GetOptions();
    if (options->m_ApplicationId == 0 || steam_id == 0 || steam_id > 0xFFFFFFFFull) {
        luaL_error(L, "register_steam requires a configured application id and a steam id");
    }
    const DiscordSocialCapi* capi = discordsdk_loader::GetCapi();
    bool ok = capi->Discord_Client_RegisterLaunchSteamApplication(
        discordsdk_client::GetClient(), options->m_ApplicationId, (uint32_t)steam_id);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int LobbyJoinOrCreateLua(lua_State* L)
{
    if (!NeedSdk("lobby_join_or_create")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* secret = luaL_checkstring(L, 1);
    std::map<std::string, std::string> lobby_metadata;
    std::map<std::string, std::string> member_metadata;
    const bool has_metadata = !lua_isnil(L, 2);
    if (has_metadata) {
        if (!lua_istable(L, 2)) {
            luaL_error(L, "metadata must be a table with string keys and string values");
        }
        ReadStringMap(L, 2, &lobby_metadata);
        // Flat tables attach to the lobby and the member alike; the optional
        // "lobby"/"member" subtable keys override each side individually.
        lua_getfield(L, 2, "lobby");
        if (lua_istable(L, -1)) {
            lobby_metadata.clear();
            ReadStringMap(L, -1, &lobby_metadata);
        }
        lua_pop(L, 1);
        lua_getfield(L, 2, "member");
        if (lua_istable(L, -1)) {
            member_metadata.clear();
            ReadStringMap(L, -1, &member_metadata);
        } else {
            member_metadata = lobby_metadata;
        }
        lua_pop(L, 1);
    }
    bool accepted = discordsdk_client::CreateOrJoinLobby(
        secret, has_metadata ? &lobby_metadata : 0, has_metadata ? &member_metadata : 0);
    lua_pushboolean(L, accepted ? 1 : 0);
    return 1;
}

int LobbyLeaveLua(lua_State* L)
{
    if (!NeedSdk("lobby_leave")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint64_t lobby_id = ParseSnowflake(L, 1, "lobby_id");
    lua_pushboolean(L, discordsdk_client::LeaveLobby(lobby_id) ? 1 : 0);
    return 1;
}

int LobbySendMessageLua(lua_State* L)
{
    if (!NeedSdk("lobby_send_message")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint64_t lobby_id = ParseSnowflake(L, 1, "lobby_id");
    const char* content = luaL_checkstring(L, 2);
    lua_pushboolean(L, discordsdk_client::SendLobbyMessage(lobby_id, content) ? 1 : 0);
    return 1;
}

int LobbyGetMessagesLua(lua_State* L)
{
    if (!NeedSdk("lobby_get_messages")) {
        lua_newtable(L);
        return 1;
    }
    uint64_t lobby_id = ParseSnowflake(L, 1, "lobby_id");
    int limit = 75;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        limit = (int)luaL_checknumber(L, 2);
    }
    if (limit <= 0) {
        limit = 75;
    } else if (limit > 200) {
        // The wiki caps history fetches at two hundred messages.
        limit = 200;
    }
    // Served synchronously from the local cache; a fresh fetch is queued and
    // lands for the next call (the SDK fetch itself is asynchronous).
    std::vector<discordsdk_client::MessageSnap> cached;
    discordsdk_client::GetCachedMessages(lobby_id, &cached);
    if ((int)cached.size() > limit) {
        cached.erase(cached.begin(), cached.begin() + (cached.size() - limit));
    }
    discordsdk_client::GetLobbyMessages(lobby_id, limit);

    lua_newtable(L);
    for (size_t i = 0; i < cached.size(); ++i) {
        discordsdk_client::PushMessage(L, cached[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }
    return 1;
}

int GetLobbiesLua(lua_State* L)
{
    lua_newtable(L);
    if (!NeedSdk("get_lobbies")) {
        return 1;
    }
    std::vector<uint64_t> lobby_ids;
    if (!discordsdk_client::GetLobbyIds(&lobby_ids)) {
        return 1;
    }
    for (size_t i = 0; i < lobby_ids.size(); ++i) {
        discordsdk_client::LobbySnap lobby;
        if (discordsdk_client::GetLobbySnap(lobby_ids[i], &lobby)) {
            discordsdk_client::PushLobby(L, lobby);
            lua_rawseti(L, -2, (int)(i + 1));
        }
    }
    return 1;
}

int StartVoiceLua(lua_State* L)
{
    if (!NeedSdk("start_voice")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint64_t lobby_id = ParseSnowflake(L, 1, "lobby_id");
    lua_pushboolean(L, discordsdk_client::StartVoice(lobby_id) ? 1 : 0);
    return 1;
}

int StopVoiceLua(lua_State* L)
{
    if (!NeedSdk("stop_voice")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // The argument is optional: no id ends the most recent call started here.
    uint64_t lobby_id = 0;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        lobby_id = ParseSnowflake(L, 1, "lobby_id");
    }
    lua_pushboolean(L, discordsdk_client::StopVoice(lobby_id) ? 1 : 0);
    return 1;
}

int StopAllVoiceLua(lua_State* L)
{
    if (!NeedSdk("stop_all_voice")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, discordsdk_client::StopAllVoice() ? 1 : 0);
    return 1;
}

int SetMutedLua(lua_State* L)
{
    if (!NeedSdk("set_muted")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, discordsdk_client::SetSelfMute(lua_toboolean(L, 1) != 0) ? 1 : 0);
    return 1;
}

int SetDeafLua(lua_State* L)
{
    if (!NeedSdk("set_deaf")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, discordsdk_client::SetSelfDeaf(lua_toboolean(L, 1) != 0) ? 1 : 0);
    return 1;
}

int SetNoiseCancellationLua(lua_State* L)
{
    if (!NeedSdk("set_noise_cancellation")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!discordsdk_loader::IsKrispPresent()) {
        // No-op per contract when the optional krisp assets are not staged.
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, discordsdk_client::SetNoiseCancellation(lua_toboolean(L, 1) != 0) ? 1 : 0);
    return 1;
}

int SetNoiseSuppressionLua(lua_State* L)
{
    if (!NeedSdk("set_noise_suppression")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, discordsdk_client::SetNoiseSuppression(lua_toboolean(L, 1) != 0) ? 1 : 0);
    return 1;
}

int SetEchoCancellationLua(lua_State* L)
{
    if (!NeedSdk("set_echo_cancellation")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, discordsdk_client::SetEchoCancellation(lua_toboolean(L, 1) != 0) ? 1 : 0);
    return 1;
}

int SetAgcLua(lua_State* L)
{
    if (!NeedSdk("set_agc")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L,
                    discordsdk_client::SetAutomaticGainControl(lua_toboolean(L, 1) != 0) ? 1 : 0);
    return 1;
}

int SetInputVolumeLua(lua_State* L)
{
    if (!NeedSdk("set_input_volume")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    discordsdk_client::SetInputVolume((float)luaL_checknumber(L, 1));
    lua_pushboolean(L, 1);
    return 1;
}

int SetOutputVolumeLua(lua_State* L)
{
    if (!NeedSdk("set_output_volume")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    discordsdk_client::SetOutputVolume((float)luaL_checknumber(L, 1));
    lua_pushboolean(L, 1);
    return 1;
}

int GetInputDevicesLua(lua_State* L)
{
    if (!NeedSdk("get_input_devices")) {
        lua_newtable(L);
        return 1;
    }
    std::vector<discordsdk_client::DeviceSnap> devices;
    discordsdk_client::GetCachedInputDevices(&devices);
    // Devices are also refetched asynchronously so the next call sees live
    // data (empty until voice connects, per the wiki).
    discordsdk_client::FetchInputDevices();
    lua_newtable(L);
    for (size_t i = 0; i < devices.size(); ++i) {
        discordsdk_client::PushDevice(L, devices[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }
    return 1;
}

int GetOutputDevicesLua(lua_State* L)
{
    if (!NeedSdk("get_output_devices")) {
        lua_newtable(L);
        return 1;
    }
    std::vector<discordsdk_client::DeviceSnap> devices;
    discordsdk_client::GetCachedOutputDevices(&devices);
    discordsdk_client::FetchOutputDevices();
    lua_newtable(L);
    for (size_t i = 0; i < devices.size(); ++i) {
        discordsdk_client::PushDevice(L, devices[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }
    return 1;
}

int SetInputDeviceLua(lua_State* L)
{
    if (!NeedSdk("set_input_device")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* id = luaL_checkstring(L, 1);
    lua_pushboolean(L, discordsdk_client::SetInputDevice(id) ? 1 : 0);
    return 1;
}

int SetOutputDeviceLua(lua_State* L)
{
    if (!NeedSdk("set_output_device")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* id = luaL_checkstring(L, 1);
    lua_pushboolean(L, discordsdk_client::SetOutputDevice(id) ? 1 : 0);
    return 1;
}

// set_online_status(string): strings outside the STATUS_* set fail locally and
// return false without reaching the SDK.
int SetOnlineStatusLua(lua_State* L)
{
    if (!NeedSdk("set_online_status")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* text = luaL_checkstring(L, 1);
    // Numeric mapping from Discord_StatusType (include/cdiscourd.h).
    int status_type = -1;
    if (strcmp(text, "online") == 0) {
        status_type = 0; // Discord_StatusType_Online
    } else if (strcmp(text, "offline") == 0) {
        status_type = 1; // Discord_StatusType_Offline
    } else if (strcmp(text, "idle") == 0) {
        status_type = 3; // Discord_StatusType_Idle
    } else if (strcmp(text, "dnd") == 0) {
        status_type = 4; // Discord_StatusType_Dnd
    } else if (strcmp(text, "invisible") == 0) {
        status_type = 5; // Discord_StatusType_Invisible
    }
    if (status_type < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, discordsdk_client::SetOnlineStatus(status_type) ? 1 : 0);
    return 1;
}

int AuthorizeLua(lua_State* L)
{
    if (!NeedSdk("authorize")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // authorize({ browser = true }) skips the SDK's Discord-client handoff and
    // runs the loopback browser flow (see discord_client.cpp).
    bool browser = false;
    if (lua_istable(L, 1)) {
        browser = ParseBoolOption(L, 1, "browser", false, 0);
    }
    bool ok = browser ? discordsdk_client::AuthorizeInBrowser() : discordsdk_client::Authorize();
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int AbortAuthorizeLua(lua_State* L)
{
    discordsdk_client::AbortAuthorize();
    return 0;
}

int IsAuthorizingLua(lua_State* L)
{
    lua_pushboolean(L, discordsdk_client::IsAuthorizing() ? 1 : 0);
    return 1;
}

int SetTokenLua(lua_State* L)
{
    if (!NeedSdk("set_token")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* access = luaL_checkstring(L, 1);
    const char* refresh = 0;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        refresh = luaL_checkstring(L, 2);
    }
    lua_pushboolean(L, discordsdk_client::SetToken(access, refresh) ? 1 : 0);
    return 1;
}

int GetTokenLua(lua_State* L)
{
    // Deliberately no SDK guard: the mirror answers locally, same contract as
    // set_token and is_authenticated. Returns nil until something is stored.
    discordsdk_client::TokenSnapshot token;
    if (!discordsdk_client::GetTokenSnapshot(&token)) {
        lua_pushnil(L);
        return 1;
    }
    lua_newtable(L);
    lua_pushstring(L, token.m_Access.c_str());
    lua_setfield(L, -2, "access");
    lua_pushstring(L, token.m_Refresh.c_str());
    lua_setfield(L, -2, "refresh");
    lua_pushstring(L, token.m_Scopes.c_str());
    lua_setfield(L, -2, "scopes");
    lua_pushnumber(L, (lua_Number)token.m_ExpiresAt); // unix seconds, 0 = unknown
    lua_setfield(L, -2, "expires_at");
    return 1;
}

int ClearTokenLua(lua_State* L)
{
    lua_pushboolean(L, discordsdk_client::ClearToken() ? 1 : 0);
    return 1;
}

int RefreshTokenLua(lua_State* L)
{
    if (!NeedSdk("refresh_token")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, discordsdk_client::RefreshToken() ? 1 : 0);
    return 1;
}

int RevokeTokenLua(lua_State* L)
{
    if (!NeedSdk("revoke_token")) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, discordsdk_client::RevokeToken() ? 1 : 0);
    return 1;
}

int IsAuthenticatedLua(lua_State* L)
{
    lua_pushboolean(L, (discordsdk_client::IsAuthenticated() || discordsdk_client::TokenConfigured())
                           ? 1 : 0);
    return 1;
}

int GetFriendsLua(lua_State* L)
{
    lua_newtable(L);
    if (!NeedSdk("get_friends")) {
        return 1;
    }
    std::vector<discordsdk_client::RelationshipSnap> relationships;
    if (!discordsdk_client::GetRelationships(&relationships)) {
        return 1;
    }
    for (size_t i = 0; i < relationships.size(); ++i) {
        discordsdk_client::PushRelationship(L, relationships[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }
    return 1;
}

int GetFriendLua(lua_State* L)
{
    if (!NeedSdk("get_friend")) {
        lua_pushnil(L);
        return 1;
    }
    uint64_t user_id = ParseSnowflake(L, 1, "user_id");
    discordsdk_client::RelationshipSnap relationship;
    if (!discordsdk_client::GetRelationship(user_id, &relationship)) {
        lua_pushnil(L);
        return 1;
    }
    discordsdk_client::PushRelationship(L, relationship);
    return 1;
}

// ---------------------------------------------------------------------------
// Lua registration
// ---------------------------------------------------------------------------

const luaL_reg Module_methods[] = {
    { "initialize", InitializeLua },
    { "connect", ConnectLua },
    { "disconnect", DisconnectLua },
    { "get_status", GetStatusLua },
    { "is_ready", IsReadyLua },
    { "is_available", IsAvailableLua },
    { "get_current_user", GetCurrentUserLua },
    { "version", VersionLua },
    { "set_callback", SetCallbackLua },
    { "update_presence", UpdatePresenceLua },
    { "clear_presence", ClearPresenceLua },
    { "send_invite", SendInviteLua },
    { "accept_invite", AcceptInviteLua },
    { "send_join_request", SendJoinRequestLua },
    { "send_join_request_reply", SendJoinRequestReplyLua },
    { "get_launch_args", GetLaunchArgsLua },
    { "register_launch_command", RegisterLaunchCommandLua },
    { "register_steam", RegisterSteamLua },
    { "lobby_join_or_create", LobbyJoinOrCreateLua },
    { "lobby_leave", LobbyLeaveLua },
    { "lobby_send_message", LobbySendMessageLua },
    { "lobby_get_messages", LobbyGetMessagesLua },
    { "get_lobbies", GetLobbiesLua },
    { "start_voice", StartVoiceLua },
    { "stop_voice", StopVoiceLua },
    { "stop_all_voice", StopAllVoiceLua },
    { "set_muted", SetMutedLua },
    { "set_deaf", SetDeafLua },
    { "set_noise_cancellation", SetNoiseCancellationLua },
    { "set_noise_suppression", SetNoiseSuppressionLua },
    { "set_echo_cancellation", SetEchoCancellationLua },
    { "set_agc", SetAgcLua },
    { "set_input_volume", SetInputVolumeLua },
    { "set_output_volume", SetOutputVolumeLua },
    { "get_input_devices", GetInputDevicesLua },
    { "get_output_devices", GetOutputDevicesLua },
    { "set_input_device", SetInputDeviceLua },
    { "set_output_device", SetOutputDeviceLua },
    { "set_online_status", SetOnlineStatusLua },
    { "authorize", AuthorizeLua },
    { "abort_authorize", AbortAuthorizeLua },
    { "is_authorizing", IsAuthorizingLua },
    { "set_token", SetTokenLua },
    { "get_token", GetTokenLua },
    { "clear_token", ClearTokenLua },
    { "refresh_token", RefreshTokenLua },
    { "revoke_token", RevokeTokenLua },
    { "is_authenticated", IsAuthenticatedLua },
    { "get_friends", GetFriendsLua },
    { "get_friend", GetFriendLua },
    { 0, 0 },
};

// Scope presets. When the library is loaded the SDK's own default scope
// strings win; the wiki preset literals fill in for stub mode.
void PushScopeConstant(lua_State* L, const char* key, const char* preset)
{
    std::string scopes = preset;
    const DiscordSocialCapi* capi = discordsdk_loader::GetCapi();
    if (capi) {
        Discord_String value;
        memset(&value, 0, sizeof(value));
        if (strcmp(key, "scopes_presence") == 0) {
            capi->Discord_Client_GetDefaultPresenceScopes(&value);
        } else {
            capi->Discord_Client_GetDefaultCommunicationScopes(&value);
        }
        // ReadString copies the payload out and releases the SDK-owned buffer.
        std::string fetched = DiscordSocialCapi_ReadString(*capi, value);
        if (!fetched.empty()) {
            scopes = fetched;
        }
    }
    lua_newtable(L);
    std::vector<std::string> parts;
    discordsdk::SplitWhitespace(scopes, &parts);
    for (size_t i = 0; i < parts.size(); ++i) {
        lua_pushlstring(L, parts[i].c_str(), parts[i].size());
        lua_rawseti(L, -2, (int)(i + 1));
    }
    lua_setfield(L, -2, key);
}

void PushNumberConstant(lua_State* L, const char* key, double value)
{
    lua_pushnumber(L, (lua_Number)value);
    lua_setfield(L, -2, key);
}

void PushStringConstant(lua_State* L, const char* key, const char* value)
{
    lua_pushstring(L, value);
    lua_setfield(L, -2, key);
}

void PushSdkVersion(lua_State* L, const char* key)
{
    // SDK_VERSION is nil when the library is missing (see the wiki).
    std::string sdk = SdkVersionString();
    if (sdk.empty()) {
        lua_pushnil(L);
    } else {
        lua_pushlstring(L, sdk.c_str(), sdk.size());
    }
    lua_setfield(L, -2, key);
}

void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);

    luaL_register(L, MODULE_NAME, Module_methods);

    // ---- scope presets ----
    PushScopeConstant(L, "scopes_presence", k_PresenceScopePreset);
    PushScopeConstant(L, "scopes_communication", k_CommunicationScopePreset);

    // ---- activity types (Discord_ActivityTypes, include/cdiscourd.h) ----
    PushNumberConstant(L, "TYPE_PLAYING", 0);
    PushNumberConstant(L, "TYPE_STREAMING", 1);
    PushNumberConstant(L, "TYPE_LISTENING", 2);
    PushNumberConstant(L, "TYPE_WATCHING", 3);
    PushNumberConstant(L, "TYPE_CUSTOM", 4);
    PushNumberConstant(L, "TYPE_COMPETING", 5);

    // ---- status display types ----
    PushNumberConstant(L, "DISPLAY_NAME", 0);
    PushNumberConstant(L, "DISPLAY_STATE", 1);
    PushNumberConstant(L, "DISPLAY_DETAILS", 2);

    // ---- platform mask (wiki surface values; the presence module maps them
    //      onto the SDK's ActivityGamePlatforms bit set when sending) ----
    PushNumberConstant(L, "PLATFORM_DESKTOP", 1);
    PushNumberConstant(L, "PLATFORM_WINDOWS", 2);
    PushNumberConstant(L, "PLATFORM_MACOS", 4);
    PushNumberConstant(L, "PLATFORM_LINUX", 8);
    PushNumberConstant(L, "PLATFORM_ANDROID", 16);
    PushNumberConstant(L, "PLATFORM_IOS", 32);

    // ---- invite and reply types ----
    PushNumberConstant(L, "INVITE_JOIN", 1);
    // Join-only in this SDK build (no spectate backing); the constant stays
    // per the contract and send_invite surfaces the note. See
    // discord_client.cpp for the send path details.
    PushNumberConstant(L, "INVITE_SPECTATE", 2);
    PushNumberConstant(L, "REPLY_NO", 0);
    PushNumberConstant(L, "REPLY_YES", 1);
    PushNumberConstant(L, "REPLY_IGNORE", 2);

    // ---- online status strings ----
    PushStringConstant(L, "STATUS_ONLINE", "online");
    PushStringConstant(L, "STATUS_DND", "dnd");
    PushStringConstant(L, "STATUS_IDLE", "idle");
    PushStringConstant(L, "STATUS_INVISIBLE", "invisible");
    PushStringConstant(L, "STATUS_OFFLINE", "offline");

    // ---- versions ----
    PushStringConstant(L, "VERSION", "1.0.5");
    PushSdkVersion(L, "SDK_VERSION");

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

} // unnamed namespace

// ---------------------------------------------------------------------------
// Extension lifecycle hooks
// ---------------------------------------------------------------------------

static dmExtension::Result AppInitializeExtension(dmExtension::AppParams* params)
{
    g_Config = params->m_ConfigFile;
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeExtension(dmExtension::Params* params)
{
    if (discordsdk_loader::Load(params->m_ConfigFile)) {
        dmLogInfo("DefoldDiscordSDK: loaded the Discord Social SDK from %s",
                  discordsdk_loader::LibraryPath());
    }
    DiscordSocialEvents_Init(params->m_L);
    LuaInit(params->m_L);
    return dmExtension::RESULT_OK;
}

static dmExtension::Result AppFinalizeExtension(dmExtension::AppParams* params)
{
    (void)params;
    // Instance finalize has already dropped the client and the Lua handlers;
    // releasing the library handle closes the process-level teardown cleanly.
    discordsdk_loader::Unload();
    return dmExtension::RESULT_OK;
}

static dmExtension::Result FinalizeExtension(dmExtension::Params* params)
{
    (void)params;
    // Handlers reference the Lua state about to go away; release them first.
    DiscordSocialEvents_ClearHandlers();
    discordsdk_presence::SetTarget(0, 0);
    discordsdk_client::Shutdown();
    g_StubWarned = false;
    g_ExplicitlyDisabled = false;
    return dmExtension::RESULT_OK;
}

static dmExtension::Result UpdateExtension(dmExtension::Params* params)
{
    (void)params;
    const DiscordSocialCapi* capi = discordsdk_loader::GetCapi();
    if (capi) {
        // Pumps queued (rate-windowed) presence updates.
        discordsdk_presence::Pump();
        discordsdk_client::PumpBrowserAuth();
        // SDK callbacks (status, invites, lobbies, messages, voice) flush on
        // the main thread. Guarded on load so a missing library never calls
        // through an empty table.
        capi->Discord_RunCallbacks();
    }
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, AppInitializeExtension, AppFinalizeExtension,
                     InitializeExtension, UpdateExtension, 0, FinalizeExtension)
