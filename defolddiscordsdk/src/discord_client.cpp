// Copyright 2023-2026
// DefoldDiscordSDK Defold extension
//
// See discord_client.h for the contract. Everything here assumes the
// single-threaded callback model (Discord_RunCallbacks on the main thread,
// the extension never enables Discord_SetFreeThreaded), so module state is
// guarded by construction rather than by locks.
//
// SDK payload ownership rules followed throughout (read off the official
// C++ wrapper in discordpp.h):
//   - every *Handle / result / invite / settings / call object handed to a
//     callback or returned into an out-parameter is owned by the extension
//     once read, and must be Drop()'ed;
//   - spans returned by the SDK (ids, devices, messages, relationships) are
//     released with a single Discord_Free(span.ptr) after the members, which
//     are themselves Drop()'ed individually, are copied out;
//   - string getters fill a Discord_String whose ptr is freed through
//     DiscordSocialCapi_ReadGetter / _ReadString.

#include "discord_client.h"

#include <string.h>
#include <time.h>

#include <string>
#include <utility>
#include <vector>

#include "discord_events.h"
#include "discord_util.h"

namespace {

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

struct ClientState
{
    const DiscordSocialCapi* m_Capi;
    Discord_Client m_Client;
    bool m_ClientInited;
    discordsdk_client::Options m_Options;

    std::vector<uint64_t> m_VoiceChannels; // channel ids of calls we started

    // Sender id -> stored clone of the newest invite from that sender. Used by
    // send_join_request_reply and accept_invite; expunged on accept.
    std::vector<uint64_t> m_PendingSenders;
    std::vector<Discord_ActivityInvite> m_PendingInvites;

    std::vector<discordsdk_client::DeviceSnap> m_InputDevices;
    std::vector<discordsdk_client::DeviceSnap> m_OutputDevices;

    // Lobby message history cache: lobby id -> snapshot list (newest last).
    // lobby_get_messages() returns this synchronously and refetches in the
    // background for the next call (the SDK fetch itself is async).
    std::vector<uint64_t> m_MessageCacheLobbies;
    std::vector<std::vector<discordsdk_client::MessageSnap> > m_MessageCache;

    // Mirrored token storage (see the OAuth notes in discord_client.h).
    std::string m_AccessToken;
    std::string m_RefreshToken;
    std::string m_GrantedScopes;
    int64_t m_ExpiresIn; // seconds, as reported by the exchange callback
    time_t m_ExpiresAt;  // unix time the access token dies (0 = unknown)

    std::string m_PendingVerifier; // PKCE verifier awaiting the authorize callback
    bool m_AuthorizePending;

    bool m_SeenReady;

    ClientState()
        : m_Capi(0)
        , m_ClientInited(false)
        , m_ExpiresIn(0)
        , m_ExpiresAt(0)
        , m_AuthorizePending(false)
        , m_SeenReady(false)
    {
        memset(&m_Client, 0, sizeof(m_Client));
    }
};

ClientState g_State;

bool ReadyForCalls()
{
    return g_State.m_Capi != 0 && g_State.m_Capi->m_Loaded && g_State.m_ClientInited;
}

// ---------------------------------------------------------------------------
// Event payloads. All are built from plain values (snapshot structs, strings).
// ---------------------------------------------------------------------------

struct EmptyContext
{
};
void PushNothing(lua_State* L, void*, int* arg_count)
{
    (void)L;
    *arg_count = 0;
}

std::string StatusWord(int status)
{
    // Discord SDK status vocabulary, lowercase per the Lua surface.
    static const char* names[7] = {
        "disconnected", "connecting", "connected", "ready", "reconnecting", "disconnecting", "http_wait",
    };
    if (status < 0 || status > 6) {
        return "";
    }
    return names[status];
}

void PushDictString(lua_State* L, const char* key, const std::string& value)
{
    if (value.empty()) {
        return;
    }
    lua_pushlstring(L, value.c_str(), value.size());
    lua_setfield(L, -2, key);
}

void PushStatusChangedEvent(int status, const std::string& error_name, int detail)
{
    struct StatusContext
    {
        std::string m_Status;
        std::string m_Error;
        int m_Detail;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            StatusContext* ctx = (StatusContext*)context;
            lua_newtable(L);
            PushDictString(L, "status", ctx->m_Status);
            PushDictString(L, "error", ctx->m_Error);
            lua_pushnumber(L, (lua_Number)ctx->m_Detail);
            lua_setfield(L, -2, "detail");
            *arg_count = 1;
        }
    };
    StatusContext context;
    context.m_Status = StatusWord(status);
    context.m_Error = error_name;
    context.m_Detail = detail;
    DiscordSocialEvents_Fire(DISCORD_SOCIAL_EVENT_STATUS_CHANGED, Push::OnPush, &context);
}

void FireError(const std::string& message, const std::string& detail)
{
    struct ErrorContext
    {
        std::string m_Message;
        std::string m_Detail;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            ErrorContext* ctx = (ErrorContext*)context;
            lua_newtable(L);
            PushDictString(L, "message", ctx->m_Message);
            PushDictString(L, "detail", ctx->m_Detail);
            *arg_count = 1;
        }
    };
    ErrorContext context;
    context.m_Message = message;
    context.m_Detail = detail;
    DiscordSocialEvents_Fire(DISCORD_SOCIAL_EVENT_ERROR, Push::OnPush, &context);
}

void PushFieldId(lua_State* L, const char* key, uint64_t value)
{
    lua_pushstring(L, discordsdk::Uint64ToString(value).c_str());
    lua_setfield(L, -2, key);
}

void PushOneFieldEvent(int event_kind, const char* field, const std::string& value)
{
    struct FieldContext
    {
        std::string m_Field;
        std::string m_Value;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            FieldContext* ctx = (FieldContext*)context;
            lua_newtable(L);
            lua_pushlstring(L, ctx->m_Value.c_str(), ctx->m_Value.size());
            lua_setfield(L, -2, ctx->m_Field.c_str());
            *arg_count = 1;
        }
    };
    FieldContext context;
    context.m_Field = field;
    context.m_Value = value;
    DiscordSocialEvents_Fire(event_kind, Push::OnPush, &context);
}

void PushPresenceResultEvent(bool ok, const std::string& error)
{
    struct ResultContext
    {
        bool m_Ok;
        std::string m_Error;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            ResultContext* ctx = (ResultContext*)context;
            lua_newtable(L);
            lua_pushboolean(L, ctx->m_Ok ? 1 : 0);
            lua_setfield(L, -2, "ok");
            if (!ctx->m_Ok) {
                lua_pushstring(L, ctx->m_Error.c_str());
                lua_setfield(L, -2, "error");
            }
            *arg_count = 1;
        }
    };
    ResultContext context;
    context.m_Ok = ok;
    context.m_Error = error;
    DiscordSocialEvents_Fire(DISCORD_SOCIAL_EVENT_PRESENCE_RESULT, Push::OnPush, &context);
}

// ---------------------------------------------------------------------------
// Lua table pushers (shared with extension.cpp / event payloads)
// ---------------------------------------------------------------------------

void PushUser(lua_State* L, const discordsdk_client::UserSnap& user)
{
    // Status vocabulary, lowercase as the Lua surface defines them.
    static const char* g_StatusNames[8] = {
        "online", "offline", "blocked", "idle", "dnd", "invisible", "streaming", "unknown",
    };
    lua_newtable(L);
    PushFieldId(L, "id", user.m_Id);
    PushDictString(L, "username", user.m_Username);
    PushDictString(L, "display_name", user.m_DisplayName);
    PushDictString(L, "avatar", user.m_Avatar);
    const char* status = user.m_Status >= 0 && user.m_Status < 8 ? g_StatusNames[user.m_Status] : "unknown";
    lua_pushstring(L, status);
    lua_setfield(L, -2, "online_status");
    lua_pushboolean(L, user.m_IsProvisional ? 1 : 0);
    lua_setfield(L, -2, "is_provisional");
    lua_pushboolean(L, user.m_HasGameActivity ? 1 : 0);
    lua_setfield(L, -2, "in_game");
}

void PushRelationship(lua_State* L, const discordsdk_client::RelationshipSnap& relationship)
{
    static const char* g_RelationshipNames[7] = {
        "none", "friend", "blocked", "pending_incoming", "pending_outgoing", "implicit", "suggestion",
    };
    lua_newtable(L);
    lua_pushnumber(L, (lua_Number)relationship.m_DiscordType);
    lua_setfield(L, -2, "type");
    const char* discord_type =
        relationship.m_DiscordType >= 0 && relationship.m_DiscordType < 7 ? g_RelationshipNames[relationship.m_DiscordType] : "unknown";
    const char* game_type =
        relationship.m_GameType >= 0 && relationship.m_GameType < 7 ? g_RelationshipNames[relationship.m_GameType] : "unknown";
    lua_pushstring(L, discord_type);
    lua_setfield(L, -2, "discord_type");
    lua_pushstring(L, game_type);
    lua_setfield(L, -2, "game_type");
    // "::" dispatch: the argument types live in discordsdk_client, so an
    // unqualified call here would ADL-match the public pushers below too
    // (and be ambiguous). Qualified calls skip ADL.
    ::PushUser(L, relationship.m_User);
    lua_setfield(L, -2, "user");
}

void PushInvite(lua_State* L, const discordsdk_client::InviteSnap& invite)
{
    lua_newtable(L);
    lua_pushnumber(L, (lua_Number)invite.m_Type);
    lua_setfield(L, -2, "type");
    PushFieldId(L, "sender_id", invite.m_SenderId);
    PushFieldId(L, "message_id", invite.m_MessageId);
    PushFieldId(L, "channel_id", invite.m_ChannelId);
    PushFieldId(L, "application_id", invite.m_ApplicationId);
    PushDictString(L, "party_id", invite.m_PartyId);
    PushDictString(L, "session_id", invite.m_SessionId);
    lua_pushboolean(L, invite.m_IsValid ? 1 : 0);
    lua_setfield(L, -2, "valid");
    PushDictString(L, "message", invite.m_Message);
    if (invite.m_HasActivity) {
        lua_newtable(L);
        PushDictString(L, "name", invite.m_ActivityName);
        PushDictString(L, "state", invite.m_ActivityState);
        PushDictString(L, "details", invite.m_ActivityDetails);
        lua_setfield(L, -2, "activity");
    }
    ::PushUser(L, invite.m_Sender);
    lua_setfield(L, -2, "sender");
}

void PushProperties(lua_State* L, const std::map<std::string, std::string>& metadata)
{
    lua_newtable(L);
    std::map<std::string, std::string>::const_iterator it = metadata.begin();
    for (; it != metadata.end(); ++it) {
        lua_pushlstring(L, it->first.c_str(), it->first.size());
        lua_pushlstring(L, it->second.c_str(), it->second.size());
        lua_settable(L, -3);
    }
}

void PushLobby(lua_State* L, const discordsdk_client::LobbySnap& lobby)
{
    lua_newtable(L);
    PushFieldId(L, "lobby_id", lobby.m_Id);
    PushProperties(L, lobby.m_Metadata);
    lua_setfield(L, -2, "metadata");

    lua_newtable(L);
    for (size_t i = 0; i < lobby.m_Members.size(); ++i) {
        const discordsdk_client::LobbyMemberSnap& member = lobby.m_Members[i];
        lua_newtable(L);
        PushFieldId(L, "user_id", member.m_User.m_Id);
        lua_pushboolean(L, member.m_IsConnected ? 1 : 0);
        lua_setfield(L, -2, "connected");
        if (member.m_HasUser) {
            ::PushUser(L, member.m_User);
            lua_setfield(L, -2, "user");
        }
        lua_rawseti(L, -2, (int)(i + 1));
    }
    lua_setfield(L, -2, "members");
}

// The SDK carries message timestamps in milliseconds (see cdiscourd.h
// MessageHandle_SentTimestamp and the SDK's own docs: "millis since the
// epoch"). The Lua surface exposes created_at as whole UNIX seconds; the
// conversion happens exactly once, here at the push boundary.
void PushMessage(lua_State* L, const discordsdk_client::MessageSnap& message)
{
    lua_newtable(L);
    PushFieldId(L, "id", message.m_Id);
    PushFieldId(L, "channel_id", message.m_ChannelId);
    if (message.m_HasLobby) {
        PushFieldId(L, "lobby_id", message.m_LobbyId);
    }
    PushFieldId(L, "author_id", message.m_AuthorId);
    PushDictString(L, "author_username", message.m_AuthorUsername);
    PushDictString(L, "content", message.m_Content);
    lua_pushnumber(L, (lua_Number)(message.m_SentTimestampMs / 1000ull));
    lua_setfield(L, -2, "created_at");
    lua_pushboolean(L, message.m_SentFromGame ? 1 : 0);
    lua_setfield(L, -2, "from_game");
}

void PushVoiceSettings(lua_State* L, const discordsdk_client::VoiceSettingsSnap& settings)
{
    lua_newtable(L);
    lua_pushboolean(L, settings.m_SelfMute ? 1 : 0);
    lua_setfield(L, -2, "muted");
    lua_pushboolean(L, settings.m_SelfDeaf ? 1 : 0);
    lua_setfield(L, -2, "deaf");
    lua_pushboolean(L, settings.m_NoiseCancellation ? 1 : 0);
    lua_setfield(L, -2, "noise_cancellation");
    lua_pushboolean(L, settings.m_NoiseSuppression ? 1 : 0);
    lua_setfield(L, -2, "noise_suppression");
    lua_pushboolean(L, settings.m_EchoCancellation ? 1 : 0);
    lua_setfield(L, -2, "echo_cancellation");
    lua_pushboolean(L, settings.m_AutomaticGainControl ? 1 : 0);
    lua_setfield(L, -2, "agc");
    lua_pushnumber(L, (lua_Number)settings.m_InputVolume);
    lua_setfield(L, -2, "input_volume");
    lua_pushnumber(L, (lua_Number)settings.m_OutputVolume);
    lua_setfield(L, -2, "output_volume");
}

void PushDevice(lua_State* L, const discordsdk_client::DeviceSnap& device)
{
    lua_newtable(L);
    PushDictString(L, "id", device.m_Id);
    PushDictString(L, "name", device.m_Name);
    lua_pushboolean(L, device.m_IsDefault ? 1 : 0);
    lua_setfield(L, -2, "is_default");
}

void PushDevices(lua_State* L, const std::vector<discordsdk_client::DeviceSnap>& devices, int* arg_count)
{
    lua_newtable(L);
    for (size_t i = 0; i < devices.size(); ++i) {
        ::PushDevice(L, devices[i]);
        lua_rawseti(L, -2, (int)(i + 1));
    }
    *arg_count = 1;
}

// ---------------------------------------------------------------------------
// Snapshot readers. Every reader leaves no SDK handle behind.
// ---------------------------------------------------------------------------

void ReadUserSnap(const DiscordSocialCapi& capi, Discord_UserHandle* handle,
                  discordsdk_client::UserSnap* out)
{
    *out = discordsdk_client::UserSnap();
    out->m_Id = capi.Discord_UserHandle_Id(handle);
    Discord_String s;
    memset(&s, 0, sizeof(s));
    capi.Discord_UserHandle_Username(handle, &s);
    out->m_Username = DiscordSocialCapi_ReadGetter(capi, &s);
    capi.Discord_UserHandle_DisplayName(handle, &s);
    out->m_DisplayName = DiscordSocialCapi_ReadGetter(capi, &s);
    capi.Discord_UserHandle_Avatar(handle, &s);
    out->m_Avatar = DiscordSocialCapi_ReadGetter(capi, &s);
    out->m_Status = (int)capi.Discord_UserHandle_Status(handle);
    out->m_IsProvisional = capi.Discord_UserHandle_IsProvisional(handle);
    Discord_Activity activity;
    memset(&activity, 0, sizeof(activity));
    out->m_HasGameActivity = capi.Discord_UserHandle_GameActivity(handle, &activity);
    capi.Discord_Activity_Drop(&activity);
}

void ReadActivitySummary(const DiscordSocialCapi& capi, Discord_Activity* activity,
                         std::string* name, std::string* state, std::string* details)
{
    Discord_String s;
    memset(&s, 0, sizeof(s));
    capi.Discord_Activity_Name(activity, &s);
    *name = DiscordSocialCapi_ReadGetter(capi, &s);
    capi.Discord_Activity_State(activity, &s);
    *state = DiscordSocialCapi_ReadGetter(capi, &s);
    capi.Discord_Activity_Details(activity, &s);
    *details = DiscordSocialCapi_ReadGetter(capi, &s);
}

void ReadMessageSnap(const DiscordSocialCapi& capi, Discord_MessageHandle* handle,
                     discordsdk_client::MessageSnap* out)
{
    *out = discordsdk_client::MessageSnap();
    out->m_Id = capi.Discord_MessageHandle_Id(handle);
    out->m_ChannelId = capi.Discord_MessageHandle_ChannelId(handle);
    Discord_String s;
    memset(&s, 0, sizeof(s));
    capi.Discord_MessageHandle_Content(handle, &s);
    out->m_Content = DiscordSocialCapi_ReadGetter(capi, &s);
    out->m_AuthorId = capi.Discord_MessageHandle_AuthorId(handle);
    Discord_UserHandle author;
    memset(&author, 0, sizeof(author));
    if (capi.Discord_MessageHandle_Author(handle, &author)) {
        Discord_String u;
        memset(&u, 0, sizeof(u));
        capi.Discord_UserHandle_Username(&author, &u);
        out->m_AuthorUsername = DiscordSocialCapi_ReadGetter(capi, &u);
        capi.Discord_UserHandle_Drop(&author);
    }
    out->m_SentTimestampMs = capi.Discord_MessageHandle_SentTimestamp(handle);
    out->m_EditedTimestampMs = capi.Discord_MessageHandle_EditedTimestamp(handle);
    out->m_SentFromGame = capi.Discord_MessageHandle_SentFromGame(handle);

    // Lobby membership is asked explicitly so a normal DM channel is never
    // mistaken for a lobby message just because it has a channel id.
    Discord_LobbyHandle lobby;
    memset(&lobby, 0, sizeof(lobby));
    bool has_lobby = capi.Discord_MessageHandle_Lobby(handle, &lobby) && lobby.opaque != 0;
    if (has_lobby) {
        uint64_t lobby_id = capi.Discord_LobbyHandle_Id(&lobby);
        if (lobby_id != 0) {
            out->m_HasLobby = true;
            out->m_LobbyId = lobby_id;
        }
    }
    capi.Discord_LobbyHandle_Drop(&lobby);
}

void ReadInviteSnap(const DiscordSocialCapi& capi, Discord_ActivityInvite* handle,
                    discordsdk_client::InviteSnap* out)
{
    *out = discordsdk_client::InviteSnap();
    out->m_SenderId = capi.Discord_ActivityInvite_SenderId(handle);
    out->m_ChannelId = capi.Discord_ActivityInvite_ChannelId(handle);
    out->m_MessageId = capi.Discord_ActivityInvite_MessageId(handle);
    out->m_Type = (int)capi.Discord_ActivityInvite_Type(handle);
    out->m_ApplicationId = capi.Discord_ActivityInvite_ApplicationId(handle);
    out->m_ParentApplicationId = capi.Discord_ActivityInvite_ParentApplicationId(handle);
    Discord_String s;
    memset(&s, 0, sizeof(s));
    capi.Discord_ActivityInvite_PartyId(handle, &s);
    out->m_PartyId = DiscordSocialCapi_ReadGetter(capi, &s);
    capi.Discord_ActivityInvite_SessionId(handle, &s);
    out->m_SessionId = DiscordSocialCapi_ReadGetter(capi, &s);
    out->m_IsValid = capi.Discord_ActivityInvite_IsValid(handle);
    out->m_Sender.m_Id = out->m_SenderId;

    // Sender profile (may be unavailable before relationships are synced).
    if (out->m_SenderId != 0) {
        Discord_UserHandle sender;
        memset(&sender, 0, sizeof(sender));
        if (capi.Discord_Client_GetUser(&g_State.m_Client, out->m_SenderId, &sender)) {
            ReadUserSnap(capi, &sender, &out->m_Sender);
            Discord_Activity activity;
            memset(&activity, 0, sizeof(activity));
            if (capi.Discord_UserHandle_GameActivity(&sender, &activity)) {
                ReadActivitySummary(capi, &activity, &out->m_ActivityName, &out->m_ActivityState,
                                    &out->m_ActivityDetails);
                out->m_HasActivity = true;
                capi.Discord_Activity_Drop(&activity);
            }
            capi.Discord_UserHandle_Drop(&sender);
        }
    }

    // Invite card text: invites travel as messages the SDK filters out of the
    // message-created stream, so the text is only reachable through the raw
    // message handle.
    if (out->m_MessageId != 0) {
        Discord_MessageHandle message;
        memset(&message, 0, sizeof(message));
        if (capi.Discord_Client_GetMessageHandle(&g_State.m_Client, out->m_MessageId, &message)) {
            Discord_String c;
            memset(&c, 0, sizeof(c));
            capi.Discord_MessageHandle_Content(&message, &c);
            out->m_Message = DiscordSocialCapi_ReadGetter(capi, &c);
            capi.Discord_MessageHandle_Drop(&message);
        }
    }
}

// ---------------------------------------------------------------------------
// Pending invite cache (sender id -> handled copies for reply/accept)
// ---------------------------------------------------------------------------

int PendingIndexForSender(uint64_t sender_id)
{
    for (size_t i = 0; i < g_State.m_PendingSenders.size(); ++i) {
        if (g_State.m_PendingSenders[i] == sender_id) {
            return (int)i;
        }
    }
    return -1;
}

void StoreInvite(uint64_t sender_id, const Discord_ActivityInvite* source)
{
    if (sender_id == 0 || !ReadyForCalls()) {
        return;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    int index = PendingIndexForSender(sender_id);
    if (index < 0) {
        index = (int)g_State.m_PendingSenders.size();
        g_State.m_PendingSenders.push_back(sender_id);
        Discord_ActivityInvite stored;
        memset(&stored, 0, sizeof(stored));
        capi.Discord_ActivityInvite_Init(&stored);
        g_State.m_PendingInvites.push_back(stored);
    }
    // Replace with a fresh clone of the newest invite from that sender.
    Discord_ActivityInvite clone;
    memset(&clone, 0, sizeof(clone));
    capi.Discord_ActivityInvite_Clone(&clone, source);
    Discord_ActivityInvite* slot = &g_State.m_PendingInvites[(size_t)index];
    capi.Discord_ActivityInvite_Drop(slot);
    *slot = clone;
}

bool TakeCachedInvite(uint64_t sender_id, Discord_ActivityInvite* out)
{
    if (sender_id == 0 || !ReadyForCalls()) {
        return false;
    }
    int index = PendingIndexForSender(sender_id);
    if (index < 0) {
        return false;
    }
    g_State.m_Capi->Discord_ActivityInvite_Clone(out, &g_State.m_PendingInvites[(size_t)index]);
    return true;
}

void ExpungeCachedInvite(uint64_t sender_id)
{
    if (!ReadyForCalls()) {
        return;
    }
    int index = PendingIndexForSender(sender_id);
    if (index < 0) {
        return;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    capi.Discord_ActivityInvite_Drop(&g_State.m_PendingInvites[(size_t)index]);
    memset(&g_State.m_PendingInvites[(size_t)index], 0, sizeof(Discord_ActivityInvite));
    g_State.m_PendingSenders.erase(g_State.m_PendingSenders.begin() + index);
    g_State.m_PendingInvites.erase(g_State.m_PendingInvites.begin() + index);
}

void ExpungeAllInvites()
{
    if (g_State.m_Capi && g_State.m_Capi->m_Loaded) {
        const DiscordSocialCapi& capi = *g_State.m_Capi;
        for (size_t i = 0; i < g_State.m_PendingInvites.size(); ++i) {
            capi.Discord_ActivityInvite_Drop(&g_State.m_PendingInvites[i]);
        }
    }
    g_State.m_PendingSenders.clear();
    g_State.m_PendingInvites.clear();
}

void DropClientResult(Discord_ClientResult* result)
{
    // The SDK always hands result ownership to the callback in this binding.
    if (g_State.m_Capi && result) {
        g_State.m_Capi->Discord_ClientResult_Drop(result);
    }
}

std::string ResultErrorString(const DiscordSocialCapi& capi, Discord_ClientResult* result)
{
    Discord_String s;
    memset(&s, 0, sizeof(s));
    capi.Discord_ClientResult_Error(result, &s);
    std::string text = DiscordSocialCapi_ReadString(capi, s);
    if (!text.empty()) {
        return text;
    }
    capi.Discord_ClientResult_ToString(result, &s);
    return DiscordSocialCapi_ReadString(capi, s);
}

// ---------------------------------------------------------------------------
// Message history cache
// ---------------------------------------------------------------------------

const size_t kMessageCacheLimit = 100;

std::vector<discordsdk_client::MessageSnap>* MessageCacheFor(uint64_t lobby_id, bool create)
{
    for (size_t i = 0; i < g_State.m_MessageCacheLobbies.size(); ++i) {
        if (g_State.m_MessageCacheLobbies[i] == lobby_id) {
            return &g_State.m_MessageCache[i];
        }
    }
    if (!create) {
        return 0;
    }
    g_State.m_MessageCacheLobbies.push_back(lobby_id);
    g_State.m_MessageCache.push_back(std::vector<discordsdk_client::MessageSnap>());
    return &g_State.m_MessageCache[g_State.m_MessageCache.size() - 1];
}

void PushMessageIntoCache(const discordsdk_client::MessageSnap& message)
{
    uint64_t key = message.m_HasLobby ? message.m_LobbyId : 0;
    if (key == 0) {
        return; // only lobby messages are cached; DMs are out of scope here
    }
    std::vector<discordsdk_client::MessageSnap>* cache = MessageCacheFor(key, true);
    cache->push_back(message);
    if (cache->size() > kMessageCacheLimit) {
        cache->erase(cache->begin());
    }
}

void HandleLobbyMessagesFetched(uint64_t lobby_id,
                                const std::vector<discordsdk_client::MessageSnap>& messages)
{
    std::vector<discordsdk_client::MessageSnap>* cache = MessageCacheFor(lobby_id, true);
    cache->insert(cache->end(), messages.begin(), messages.end());
    if (cache->size() > kMessageCacheLimit) {
        // Keep the freshest entries.
        size_t overflow = cache->size() - kMessageCacheLimit;
        cache->erase(cache->begin(), cache->begin() + (long)overflow);
    }
}

void DeleteMessageCacheEntries(uint64_t lobby_id)
{
    if (!g_State.m_MessageCacheLobbies.empty()) {
        std::vector<uint64_t>::iterator lobby_it = g_State.m_MessageCacheLobbies.begin();
        std::vector<std::vector<discordsdk_client::MessageSnap> >::iterator cache_it =
            g_State.m_MessageCache.begin();
        for (; lobby_it != g_State.m_MessageCacheLobbies.end(); ++lobby_it, ++cache_it) {
            if (*lobby_it == lobby_id) {
                cache_it->clear();
                return;
            }
        }
    }
}

struct FetchMessagesContext
{
    uint64_t m_LobbyId;
    FetchMessagesContext() : m_LobbyId(0) {}
};

struct FetchDevicesContext
{
    bool m_Input;
    FetchDevicesContext() : m_Input(true) {}
};

// ---------------------------------------------------------------------------
// Event dispatchers for callbacks that carry nothing but ids
// ---------------------------------------------------------------------------

void FireSimpleIdEvent(int event_kind, const char* field, uint64_t id)
{
    PushOneFieldEvent(event_kind, field, discordsdk::Uint64ToString(id));
}

void HandleStatusChanged(Discord_Client_Status status, Discord_Client_Error error, int32_t error_detail)
{
    std::string status_name = StatusWord(status);
    std::string error_name;
    if (error != Discord_Client_Error_None && g_State.m_Capi) {
        const DiscordSocialCapi& capi = *g_State.m_Capi;
        Discord_String e;
        memset(&e, 0, sizeof(e));
        capi.Discord_Client_ErrorToString(error, &e);
        error_name = DiscordSocialCapi_ReadGetter(capi, &e);
    }

    PushStatusChangedEvent(status, error_name, (int)error_detail);

    if (error != Discord_Client_Error_None) {
        FireError(error_name.empty() ? std::string("connection failure") : error_name,
                  StatusWord(status));
    }

    if (status == Discord_Client_Status_Ready && !g_State.m_SeenReady) {
        g_State.m_SeenReady = true;
        DiscordSocialEvents_Fire(DISCORD_SOCIAL_EVENT_READY, PushNothing, (void*)&g_State);
    }
}

void HandleLog(Discord_String message, Discord_LoggingSeverity severity)
{
    std::string text = DiscordSocialCapi_ReadString(*g_State.m_Capi, message);
    if (text.empty()) {
        return;
    }
    switch (severity) {
    case Discord_LoggingSeverity_Warning:
        dmLogWarning("DefoldDiscordSDK: %s", text.c_str());
        break;
    case Discord_LoggingSeverity_Error:
        dmLogError("DefoldDiscordSDK: %s", text.c_str());
        break;
    default:
        dmLogInfo("DefoldDiscordSDK: %s", text.c_str());
        break;
    }
}

void HandleTokenExpired()
{
    g_State.m_AccessToken.clear();
    g_State.m_AuthorizePending = false;
    PushOneFieldEvent(DISCORD_SOCIAL_EVENT_TOKEN_EXPIRED, "token", "expired");
}

void HandleUserUpdated(uint64_t user_id)
{
    FireSimpleIdEvent(DISCORD_SOCIAL_EVENT_USER_UPDATED, "user_id", user_id);
}

void HandleRelationshipEvent(int event_kind, uint64_t user_id, bool is_discord_update)
{
    struct RelationshipContext
    {
        std::string m_UserId;
        bool m_IsDiscordUpdate;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            RelationshipContext* ctx = (RelationshipContext*)context;
            lua_newtable(L);
            PushFieldId(L, "user_id", 0); // id text arrives via ctx below
            lua_pop(L, 1);                // replaced directly for clarity
            lua_pushstring(L, ctx->m_UserId.c_str());
            lua_setfield(L, -2, "user_id");
            lua_pushboolean(L, ctx->m_IsDiscordUpdate ? 1 : 0);
            lua_setfield(L, -2, "is_discord_relationship_update");
            *arg_count = 1;
        }
    };
    RelationshipContext context;
    context.m_UserId = discordsdk::Uint64ToString(user_id);
    context.m_IsDiscordUpdate = is_discord_update;
    DiscordSocialEvents_Fire(event_kind, Push::OnPush, &context);
}

void HandleLobbyEvent(int event_kind, uint64_t lobby_id)
{
    FireSimpleIdEvent(event_kind, "lobby_id", lobby_id);
}

void HandleLobbyMemberEvent(int event_kind, uint64_t lobby_id, uint64_t member_id)
{
    struct MemberContext
    {
        std::string m_LobbyId;
        std::string m_UserId;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            MemberContext* ctx = (MemberContext*)context;
            lua_newtable(L);
            lua_pushstring(L, ctx->m_LobbyId.c_str());
            lua_setfield(L, -2, "lobby_id");
            lua_pushstring(L, ctx->m_UserId.c_str());
            lua_setfield(L, -2, "user_id");
            *arg_count = 1;
        }
    };
    MemberContext context;
    context.m_LobbyId = discordsdk::Uint64ToString(lobby_id);
    context.m_UserId = discordsdk::Uint64ToString(member_id);
    DiscordSocialEvents_Fire(event_kind, Push::OnPush, &context);
}

void HandleMessageEvent(int event_kind, uint64_t message_id)
{
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_MessageHandle handle;
    memset(&handle, 0, sizeof(handle));
    if (!capi.Discord_Client_GetMessageHandle(&g_State.m_Client, message_id, &handle)) {
        dmLogWarning("DefoldDiscordSDK: message %llu was reported but its handle is gone",
                     (unsigned long long)message_id);
        return;
    }
    discordsdk_client::MessageSnap snap;
    ReadMessageSnap(capi, &handle, &snap);
    capi.Discord_MessageHandle_Drop(&handle);

    PushMessageIntoCache(snap);

    struct MessageContext
    {
        discordsdk_client::MessageSnap m_Snap;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            MessageContext* ctx = (MessageContext*)context;
            ::PushMessage(L, ctx->m_Snap);
            *arg_count = 1;
        }
    };
    MessageContext context;
    context.m_Snap = snap;
    DiscordSocialEvents_Fire(event_kind, Push::OnPush, &context);
}

void HandleInvite(int event_kind, Discord_ActivityInvite* invite)
{
    const DiscordSocialCapi& capi = *g_State.m_Capi;

    discordsdk_client::InviteSnap snap;
    ReadInviteSnap(capi, invite, &snap);

    // Keep the newest invite per sender for send_join_request_reply and
    // accept_invite (accept consumes it).
    StoreInvite(snap.m_SenderId, invite);

    struct InviteContext
    {
        discordsdk_client::InviteSnap m_Snap;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            InviteContext* ctx = (InviteContext*)context;
            ::PushInvite(L, ctx->m_Snap);
            *arg_count = 1;
        }
    };
    InviteContext context;
    context.m_Snap = snap;
    DiscordSocialEvents_Fire(event_kind, Push::OnPush, &context);

    if (snap.m_Type == (int)Discord_ActivityActionTypes_JoinRequest) {
        PushOneFieldEvent(DISCORD_SOCIAL_EVENT_ACTIVITY_JOIN_REQUEST, "sender_id",
                          discordsdk::Uint64ToString(snap.m_SenderId));
    }
}

void HandleActivityJoin(const std::string& secret)
{
    PushOneFieldEvent(DISCORD_SOCIAL_EVENT_ACTIVITY_JOIN, "secret", secret);
}

void HandleVoiceParticipants(uint64_t lobby_id, uint64_t member_id, bool added)
{
    const DiscordSocialCapi& capi = *g_State.m_Capi;

    std::vector<discordsdk_client::UserSnap> participants;
    Discord_Call call;
    memset(&call, 0, sizeof(call));
    if (capi.Discord_Client_GetCall(&g_State.m_Client, lobby_id, &call)) {
        Discord_UInt64Span ids;
        memset(&ids, 0, sizeof(ids));
        capi.Discord_Call_GetParticipants(&call, &ids);
        for (size_t i = 0; i < ids.size; ++i) {
            discordsdk_client::UserSnap snap;
            Discord_UserHandle user;
            memset(&user, 0, sizeof(user));
            if (capi.Discord_Client_GetUser(&g_State.m_Client, ids.ptr[i], &user)) {
                ReadUserSnap(capi, &user, &snap);
                capi.Discord_UserHandle_Drop(&user);
            } else {
                snap = discordsdk_client::UserSnap();
                snap.m_Id = ids.ptr[i];
            }
            participants.push_back(snap);
        }
        if (ids.ptr) {
            capi.Discord_Free(ids.ptr);
        }
        capi.Discord_Call_Drop(&call);
    }

    struct VoiceContext
    {
        std::string m_LobbyId;
        std::string m_UserId;
        bool m_Added;
        std::vector<discordsdk_client::UserSnap> m_Users;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            VoiceContext* ctx = (VoiceContext*)context;
            lua_newtable(L);
            lua_pushstring(L, ctx->m_LobbyId.c_str());
            lua_setfield(L, -2, "lobby_id");
            lua_pushstring(L, ctx->m_UserId.c_str());
            lua_setfield(L, -2, "user_id");
            lua_pushboolean(L, ctx->m_Added ? 1 : 0);
            lua_setfield(L, -2, "added");
            lua_newtable(L);
            for (size_t i = 0; i < ctx->m_Users.size(); ++i) {
                ::PushUser(L, ctx->m_Users[i]);
                lua_rawseti(L, -2, (int)(i + 1));
            }
            lua_setfield(L, -2, "users");
            *arg_count = 1;
        }
    };
    VoiceContext context;
    context.m_LobbyId = discordsdk::Uint64ToString(lobby_id);
    context.m_UserId = discordsdk::Uint64ToString(member_id);
    context.m_Added = added;
    context.m_Users.swap(participants);
    DiscordSocialEvents_Fire(DISCORD_SOCIAL_EVENT_VOICE_PARTICIPANT_CHANGED, Push::OnPush, &context);
}

void HandleVoiceSettingsUpdated(Discord_VoiceSettings* settings)
{
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    discordsdk_client::VoiceSettingsSnap snap;
    snap.m_SelfMute = capi.Discord_VoiceSettings_SelfMute(settings);
    snap.m_SelfDeaf = capi.Discord_VoiceSettings_SelfDeaf(settings);
    snap.m_InputMode = (int)capi.Discord_VoiceSettings_InputMode(settings);
    snap.m_InputVolume = capi.Discord_VoiceSettings_InputVolume(settings);
    snap.m_OutputVolume = capi.Discord_VoiceSettings_OutputVolume(settings);
    snap.m_AutomaticGainControl = capi.Discord_VoiceSettings_AutomaticGainControl(settings);
    snap.m_EchoCancellation = capi.Discord_VoiceSettings_EchoCancellation(settings);
    snap.m_NoiseSuppression = capi.Discord_VoiceSettings_NoiseSuppression(settings);
    snap.m_NoiseCancellation = capi.Discord_VoiceSettings_NoiseCancellation(settings);
    capi.Discord_VoiceSettings_Drop(settings);

    struct SettingsContext
    {
        discordsdk_client::VoiceSettingsSnap m_Snap;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            SettingsContext* ctx = (SettingsContext*)context;
            ::PushVoiceSettings(L, ctx->m_Snap);
            *arg_count = 1;
        }
    };
    SettingsContext context;
    context.m_Snap = snap;
    DiscordSocialEvents_Fire(DISCORD_SOCIAL_EVENT_VOICE_SETTINGS_UPDATED, Push::OnPush, &context);
}

// ---------------------------------------------------------------------------
// Result plumbing for failed async calls
// ---------------------------------------------------------------------------

void ProcessFailedResult(Discord_ClientResult* result, const char* what)
{
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    std::string message = ResultErrorString(capi, result);
    DropClientResult(result);
    FireError(message.empty() ? std::string("operation failed") : message, what ? what : "");
}

void ProcessResultOnly(Discord_ClientResult* result, const char* what)
{
    if (g_State.m_Capi->Discord_ClientResult_Successful(result)) {
        DropClientResult(result);
        return;
    }
    ProcessFailedResult(result, what);
}

// ---------------------------------------------------------------------------
// SDK callback adapters (static C functions with the exact callback types)
// ---------------------------------------------------------------------------

void DiscordSocialClient_OnStatusChanged(Discord_Client_Status status, Discord_Client_Error error,
                                         int32_t error_detail, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleStatusChanged(status, error, error_detail);
}

void DiscordSocialClient_OnLog(Discord_String message, Discord_LoggingSeverity severity, void*)
{
    if (!g_State.m_Capi || !g_State.m_Capi->m_Loaded) {
        Discord_Free(message.ptr);
        return;
    }
    HandleLog(message, severity);
}

void DiscordSocialClient_OnTokenExpired(void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleTokenExpired();
}

void DiscordSocialClient_OnUserUpdated(uint64_t user_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleUserUpdated(user_id);
}

void DiscordSocialClient_OnRelationshipCreated(uint64_t user_id, bool is_discord_update, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleRelationshipEvent(DISCORD_SOCIAL_EVENT_RELATIONSHIP_CREATED, user_id, is_discord_update);
}

void DiscordSocialClient_OnRelationshipDeleted(uint64_t user_id, bool is_discord_update, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleRelationshipEvent(DISCORD_SOCIAL_EVENT_RELATIONSHIP_DELETED, user_id, is_discord_update);
}

void DiscordSocialClient_OnRelationshipGroupsUpdated(uint64_t user_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleRelationshipEvent(DISCORD_SOCIAL_EVENT_RELATIONSHIP_GROUPS_UPDATED, user_id, false);
}

void DiscordSocialClient_OnLobbyCreated(uint64_t lobby_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleLobbyEvent(DISCORD_SOCIAL_EVENT_LOBBY_CREATED, lobby_id);
}

void DiscordSocialClient_OnLobbyUpdated(uint64_t lobby_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleLobbyEvent(DISCORD_SOCIAL_EVENT_LOBBY_UPDATED, lobby_id);
}

void DiscordSocialClient_OnLobbyDeleted(uint64_t lobby_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleLobbyEvent(DISCORD_SOCIAL_EVENT_LOBBY_DELETED, lobby_id);
    DeleteMessageCacheEntries(lobby_id);
}

void DiscordSocialClient_OnLobbyMemberAdded(uint64_t lobby_id, uint64_t member_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleLobbyMemberEvent(DISCORD_SOCIAL_EVENT_LOBBY_MEMBER_ADDED, lobby_id, member_id);
}

void DiscordSocialClient_OnLobbyMemberRemoved(uint64_t lobby_id, uint64_t member_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleLobbyMemberEvent(DISCORD_SOCIAL_EVENT_LOBBY_MEMBER_REMOVED, lobby_id, member_id);
}

void DiscordSocialClient_OnLobbyMemberUpdated(uint64_t lobby_id, uint64_t member_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleLobbyMemberEvent(DISCORD_SOCIAL_EVENT_LOBBY_MEMBER_UPDATED, lobby_id, member_id);
}

void DiscordSocialClient_OnMessageCreated(uint64_t message_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleMessageEvent(DISCORD_SOCIAL_EVENT_MESSAGE_CREATED, message_id);
}

void DiscordSocialClient_OnMessageUpdated(uint64_t message_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleMessageEvent(DISCORD_SOCIAL_EVENT_MESSAGE_UPDATED, message_id);
}

void DiscordSocialClient_OnMessageDeleted(uint64_t message_id, uint64_t channel_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    struct DeletedContext
    {
        std::string m_MessageId;
        std::string m_ChannelId;
    };
    struct Push
    {
        static void OnPush(lua_State* L, void* context, int* arg_count)
        {
            DeletedContext* ctx = (DeletedContext*)context;
            lua_newtable(L);
            lua_pushstring(L, ctx->m_MessageId.c_str());
            lua_setfield(L, -2, "message_id");
            // The callback carries the channel id only. Lobby channel ids are
            // equal to lobby ids in this SDK, so the same value is exposed
            // under both keys (channel_id for DMs, lobby_id for lobby flow);
            // callers that care which case applies can re-fetch members.
            lua_pushstring(L, ctx->m_ChannelId.c_str());
            lua_setfield(L, -2, "channel_id");
            lua_pushstring(L, ctx->m_ChannelId.c_str());
            lua_setfield(L, -2, "lobby_id");
            *arg_count = 1;
        }
    };
    DeletedContext context;
    context.m_MessageId = discordsdk::Uint64ToString(message_id);
    context.m_ChannelId = discordsdk::Uint64ToString(channel_id);
    DiscordSocialEvents_Fire(DISCORD_SOCIAL_EVENT_MESSAGE_DELETED, Push::OnPush, &context);
}

void DiscordSocialClient_OnInviteCreated(Discord_ActivityInvite* invite, void*)
{
    if (ReadyForCalls()) {
        HandleInvite(DISCORD_SOCIAL_EVENT_ACTIVITY_INVITE_CREATED, invite);
    }
    if (g_State.m_Capi) {
        g_State.m_Capi->Discord_ActivityInvite_Drop(invite);
    }
}

void DiscordSocialClient_OnInviteUpdated(Discord_ActivityInvite* invite, void*)
{
    if (ReadyForCalls()) {
        HandleInvite(DISCORD_SOCIAL_EVENT_ACTIVITY_INVITE_UPDATED, invite);
    }
    if (g_State.m_Capi) {
        g_State.m_Capi->Discord_ActivityInvite_Drop(invite);
    }
}

void DiscordSocialClient_OnActivityJoin(Discord_String join_secret, void*)
{
    if (!ReadyForCalls()) {
        Discord_Free(join_secret.ptr);
        return;
    }
    std::string secret = DiscordSocialCapi_ReadString(*g_State.m_Capi, join_secret);
    HandleActivityJoin(secret);
}

void DiscordSocialClient_OnVoiceParticipantChanged(uint64_t lobby_id, uint64_t member_id, bool added, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    HandleVoiceParticipants(lobby_id, member_id, added);
}

void DiscordSocialClient_OnVoiceSettingsUpdated(Discord_VoiceSettings* settings, void*)
{
    if (ReadyForCalls()) {
        HandleVoiceSettingsUpdated(settings);
        return;
    }
    if (g_State.m_Capi) {
        g_State.m_Capi->Discord_VoiceSettings_Drop(settings);
    }
}

} // unnamed namespace

namespace discordsdk_client {

// ---------------------------------------------------------------------------
// Lua table pushers (re-exported for extension.cpp)
// ---------------------------------------------------------------------------

void PushUser(lua_State* L, const UserSnap& user) { ::PushUser(L, user); }
void PushRelationship(lua_State* L, const RelationshipSnap& relationship) { ::PushRelationship(L, relationship); }
void PushInvite(lua_State* L, const InviteSnap& invite) { ::PushInvite(L, invite); }
void PushLobby(lua_State* L, const LobbySnap& lobby) { ::PushLobby(L, lobby); }
void PushMessage(lua_State* L, const MessageSnap& message) { ::PushMessage(L, message); }
void PushVoiceSettings(lua_State* L, const VoiceSettingsSnap& settings) { ::PushVoiceSettings(L, settings); }
void PushDevice(lua_State* L, const DeviceSnap& device) { ::PushDevice(L, device); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool IsRunning() { return ReadyForCalls(); }

Discord_Client* GetClient() { return ReadyForCalls() ? &g_State.m_Client : 0; }

const Options* GetOptions() { return ReadyForCalls() ? &g_State.m_Options : 0; }

const DiscordSocialCapi* GetCapi() { return ReadyForCalls() ? g_State.m_Capi : 0; }

void Shutdown()
{
    if (g_State.m_ClientInited) {
        // Drop is the whole teardown: callbacks flush through
        // Discord_RunCallbacks in the main loop before this point.
        if (g_State.m_Capi && g_State.m_Capi->m_Loaded) {
            g_State.m_Capi->Discord_Client_Drop(&g_State.m_Client);
        }
        memset(&g_State.m_Client, 0, sizeof(g_State.m_Client));
        g_State.m_ClientInited = false;
    }
    ExpungeAllInvites();
    g_State.m_VoiceChannels.clear();
    g_State.m_InputDevices.clear();
    g_State.m_OutputDevices.clear();
    g_State.m_MessageCacheLobbies.clear();
    g_State.m_MessageCache.clear();
    g_State.m_AccessToken.clear();
    g_State.m_RefreshToken.clear();
    g_State.m_GrantedScopes.clear();
    g_State.m_ExpiresIn = 0;
    g_State.m_ExpiresAt = 0;
    g_State.m_PendingVerifier.clear();
    g_State.m_AuthorizePending = false;
    g_State.m_SeenReady = false;
}

void DiscordSocialClient_OnSimpleResult(Discord_ClientResult* result, void*);

bool Create(const DiscordSocialCapi* capi, const Options& opts)
{
    if (!capi || !capi->m_Loaded) {
        return false;
    }
    if (g_State.m_ClientInited) {
        // A second initialize() reruns the setup on top of a fresh client.
        Shutdown();
    }

    g_State.m_Capi = capi;
    g_State.m_Options = opts;
    const DiscordSocialCapi& api = *capi;
    Discord_Client* client = &g_State.m_Client;

    // Discord_Client is a pointer-sized wrapper; Init() creates the impl and
    // Drop() releases it (the pattern the official wrapper uses).
    api.Discord_Client_Init(client);
    g_State.m_ClientInited = true;

    if (opts.m_ApplicationId != 0) {
        api.Discord_Client_SetApplicationId(client, opts.m_ApplicationId);
    } else {
        dmLogWarning("DefoldDiscordSDK: no application id was configured; rich presence and"
                     " social features cannot bind to a Discord application.");
    }

    // Launch registration is expected right after startup so join buttons in
    // Discord can launch the game later. Empty command = current executable /
    // current bundle (see the SDK doc for RegisterLaunchCommand).
    if (opts.m_RegisterLaunchCommand && opts.m_ApplicationId != 0) {
        Discord_String command;
        command.ptr = 0;
        command.size = 0;
        if (!api.Discord_Client_RegisterLaunchCommand(client, opts.m_ApplicationId, command)) {
            dmLogWarning("DefoldDiscordSDK: launch command registration failed for this machine");
        }
    }
    if (opts.m_RegisterLaunchSteam && opts.m_ApplicationId != 0 && opts.m_SteamAppId != 0) {
        if (!api.Discord_Client_RegisterLaunchSteamApplication(client, opts.m_ApplicationId, opts.m_SteamAppId)) {
            dmLogWarning("DefoldDiscordSDK: Steam application launch registration failed");
        }
    }

    // Logging: route SDK logs (Info and above) through Defold's own logger.
    api.Discord_Client_AddLogCallback(client, &DiscordSocialClient_OnLog, &DiscordSocialClient_FreeCallbackData,
                                      0, Discord_LoggingSeverity_Info);

    api.Discord_Client_SetStatusChangedCallback(client, &DiscordSocialClient_OnStatusChanged,
                                                &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetTokenExpirationCallback(client, &DiscordSocialClient_OnTokenExpired,
                                                  &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetUserUpdatedCallback(client, &DiscordSocialClient_OnUserUpdated,
                                              &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetRelationshipCreatedCallback(client, &DiscordSocialClient_OnRelationshipCreated,
                                                      &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetRelationshipDeletedCallback(client, &DiscordSocialClient_OnRelationshipDeleted,
                                                      &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetRelationshipGroupsUpdatedCallback(client, &DiscordSocialClient_OnRelationshipGroupsUpdated,
                                                            &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetActivityInviteCreatedCallback(client, &DiscordSocialClient_OnInviteCreated,
                                                        &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetActivityInviteUpdatedCallback(client, &DiscordSocialClient_OnInviteUpdated,
                                                        &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetActivityJoinCallback(client, &DiscordSocialClient_OnActivityJoin,
                                               &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetLobbyCreatedCallback(client, &DiscordSocialClient_OnLobbyCreated,
                                               &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetLobbyUpdatedCallback(client, &DiscordSocialClient_OnLobbyUpdated,
                                               &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetLobbyDeletedCallback(client, &DiscordSocialClient_OnLobbyDeleted,
                                               &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetLobbyMemberAddedCallback(client, &DiscordSocialClient_OnLobbyMemberAdded,
                                                   &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetLobbyMemberRemovedCallback(client, &DiscordSocialClient_OnLobbyMemberRemoved,
                                                     &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetLobbyMemberUpdatedCallback(client, &DiscordSocialClient_OnLobbyMemberUpdated,
                                                     &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetMessageCreatedCallback(client, &DiscordSocialClient_OnMessageCreated,
                                                 &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetMessageUpdatedCallback(client, &DiscordSocialClient_OnMessageUpdated,
                                                 &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetMessageDeletedCallback(client, &DiscordSocialClient_OnMessageDeleted,
                                                 &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetVoiceParticipantChangedCallback(client, &DiscordSocialClient_OnVoiceParticipantChanged,
                                                          &DiscordSocialClient_FreeCallbackData, 0);
    api.Discord_Client_SetVoiceSettingsUpdatedCallback(client, &DiscordSocialClient_OnVoiceSettingsUpdated,
                                                       &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void Connect()
{
    if (ReadyForCalls()) {
        g_State.m_Capi->Discord_Client_Connect(&g_State.m_Client);
    }
}

void Disconnect()
{
    if (ReadyForCalls()) {
        g_State.m_Capi->Discord_Client_Disconnect(&g_State.m_Client);
    }
}

std::string StatusName()
{
    if (!ReadyForCalls()) {
        return std::string();
    }
    return StatusWord((int)g_State.m_Capi->Discord_Client_GetStatus(&g_State.m_Client));
}

bool IsReady() { return ReadyForCalls() && g_State.m_SeenReady; }

// ---------------------------------------------------------------------------
// OAuth and tokens
// ---------------------------------------------------------------------------

void DiscordSocialClient_OnAuthorizeCompleted(Discord_ClientResult* result, Discord_String code,
                                              Discord_String redirect_uri, void*);

void DiscordSocialClient_OnTokenExchange(Discord_ClientResult* result, Discord_String access_token,
                                         Discord_String refresh_token, Discord_AuthorizationTokenType token_type,
                                         int32_t expires_in, Discord_String scopes, void*);

void DiscordSocialClient_OnUpdateToken(Discord_ClientResult* result, void*);

void DiscordSocialClient_OnRevokeCompleted(Discord_ClientResult* result, void*);

// Fires the `authorized` event once UpdateToken has made the client usable.
void LinkAuthorized();

void DiscordSocialClient_OnAcceptCompleted(Discord_ClientResult* result, Discord_String join_secret, void*);

bool Authorize()
{
    if (!ReadyForCalls()) {
        return false;
    }
    if (g_State.m_Options.m_ApplicationId == 0) {
        FireError("authorize called without an application id", "");
        return false;
    }
    if (g_State.m_AuthorizePending) {
        dmLogWarning("DefoldDiscordSDK: authorize() is already running; ignoring the extra call");
        return true;
    }

    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_Client* client = &g_State.m_Client;

    // PKCE: verifier is produced together with the challenge; the challenge
    // goes into the args, the verifier survives until the token exchange.
    Discord_AuthorizationCodeVerifier verifier;
    memset(&verifier, 0, sizeof(verifier));
    capi.Discord_Client_CreateAuthorizationCodeVerifier(client, &verifier);
    Discord_AuthorizationCodeChallenge challenge;
    memset(&challenge, 0, sizeof(challenge));
    capi.Discord_AuthorizationCodeVerifier_Challenge(&verifier, &challenge);

    Discord_String verifier_text;
    memset(&verifier_text, 0, sizeof(verifier_text));
    capi.Discord_AuthorizationCodeVerifier_Verifier(&verifier, &verifier_text);
    g_State.m_PendingVerifier = DiscordSocialCapi_ReadGetter(capi, &verifier_text);
    g_State.m_AuthorizePending = true;

    Discord_AuthorizationArgs args;
    memset(&args, 0, sizeof(args));
    capi.Discord_AuthorizationArgs_Init(&args);
    capi.Discord_AuthorizationArgs_SetClientId(&args, g_State.m_Options.m_ApplicationId);
    Discord_String scopes_arg;
    scopes_arg.ptr = (uint8_t*)g_State.m_Options.m_Scopes.data();
    scopes_arg.size = g_State.m_Options.m_Scopes.size();
    capi.Discord_AuthorizationArgs_SetScopes(&args, scopes_arg);
    capi.Discord_AuthorizationArgs_SetCodeChallenge(&args, &challenge);

    capi.Discord_Client_Authorize(client, &args, &DiscordSocialClient_OnAuthorizeCompleted,
                                  &DiscordSocialClient_FreeCallbackData, 0);

    // The args payload is consumed synchronously by Authorize (the official
    // wrapper passes equally scoped temporaries).
    capi.Discord_AuthorizationArgs_Drop(&args);
    capi.Discord_AuthorizationCodeChallenge_Drop(&challenge);
    capi.Discord_AuthorizationCodeVerifier_Drop(&verifier);
    return true;
}

void DiscordSocialClient_OnAuthorizeCompleted(Discord_ClientResult* result, Discord_String code,
                                              Discord_String redirect_uri, void*)
{
    if (!ReadyForCalls()) {
        Discord_Free(code.ptr);
        Discord_Free(redirect_uri.ptr);
        return;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    std::string code_text = DiscordSocialCapi_ReadString(capi, code);
    std::string redirect = DiscordSocialCapi_ReadString(capi, redirect_uri);
    std::string verifier = g_State.m_PendingVerifier;
    g_State.m_PendingVerifier.clear();
    g_State.m_AuthorizePending = false;

    if (!result || !capi.Discord_ClientResult_Successful(result)) {
        // OAuth failures surface here: closed consent window, mismatched
        // redirect, denied scopes.
        ProcessFailedResult(result, "authorize");
        return;
    }
    DropClientResult(result);

    if (code_text.empty() || verifier.empty()) {
        FireError("authorization returned no code or the verifier was lost", "authorize");
        return;
    }
    if (redirect.empty()) {
        redirect = g_State.m_Options.m_DefaultRedirect;
    }

    Discord_String code_arg;
    code_arg.ptr = (uint8_t*)code_text.data();
    code_arg.size = code_text.size();
    Discord_String verifier_arg;
    verifier_arg.ptr = (uint8_t*)verifier.data();
    verifier_arg.size = verifier.size();
    Discord_String redirect_arg;
    redirect_arg.ptr = (uint8_t*)redirect.data();
    redirect_arg.size = redirect.size();

    capi.Discord_Client_GetToken(&g_State.m_Client, g_State.m_Options.m_ApplicationId, code_arg, verifier_arg,
                                 redirect_arg, &DiscordSocialClient_OnTokenExchange,
                                 &DiscordSocialClient_FreeCallbackData, 0);
}

void RememberTokens(const std::string& access, const std::string& refresh, const std::string& scopes,
                    int64_t expires_in)
{
    // If a previous pending token existed, it is replaced wholesale. The SDK
    // keeps its own live copy; the state mirror drives refresh/revoke and the
    // is_authenticated fallback.
    g_State.m_AccessToken = access;
    g_State.m_RefreshToken = refresh;
    g_State.m_GrantedScopes = scopes;
    g_State.m_ExpiresIn = expires_in;
    g_State.m_ExpiresAt = expires_in > 0 ? time(0) + (time_t)expires_in : 0;
}

void DiscordSocialClient_OnTokenExchange(Discord_ClientResult* result, Discord_String access_token,
                                         Discord_String refresh_token, Discord_AuthorizationTokenType token_type,
                                         int32_t expires_in, Discord_String scopes, void*)
{
    (void)token_type;
    if (!ReadyForCalls()) {
        Discord_Free(access_token.ptr);
        Discord_Free(refresh_token.ptr);
        Discord_Free(scopes.ptr);
        return;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    std::string access = DiscordSocialCapi_ReadString(capi, access_token);
    std::string refresh = DiscordSocialCapi_ReadString(capi, refresh_token);
    std::string granted_scopes = DiscordSocialCapi_ReadString(capi, scopes);

    if (!result || !capi.Discord_ClientResult_Successful(result)) {
        ProcessFailedResult(result, "token exchange");
        return;
    }
    DropClientResult(result);
    RememberTokens(access, refresh, granted_scopes, (int64_t)expires_in);

    // Hand the fresh access token to the client so the account link becomes
    // live; the `authorized` event fires once that completes (and it is then
    // safe to call connect()).
    Discord_String token_arg;
    token_arg.ptr = (uint8_t*)access.data();
    token_arg.size = access.size();
    capi.Discord_Client_UpdateToken(&g_State.m_Client, Discord_AuthorizationTokenType_User, token_arg,
                                    &DiscordSocialClient_OnUpdateToken, &DiscordSocialClient_FreeCallbackData, 0);
}

void DiscordSocialClient_OnUpdateToken(Discord_ClientResult* result, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    if (!result || !capi.Discord_ClientResult_Successful(result)) {
        ProcessFailedResult(result, "update token");
        return;
    }
    DropClientResult(result);

    // The account link lives now; connect() is the explicit next step.
    LinkAuthorized();
}

void LinkAuthorized()
{
    DiscordSocialEvents_Fire(DISCORD_SOCIAL_EVENT_AUTHORIZED, PushNothing, 0);
}

bool SetToken(const char* access, const char* refresh)
{
    if (!ReadyForCalls()) {
        return false;
    }
    std::string access_text = access ? access : "";
    RememberTokens(access_text, refresh ? refresh : std::string(), g_State.m_GrantedScopes, 0);
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_String token_arg;
    token_arg.ptr = (uint8_t*)access_text.data();
    token_arg.size = access_text.size();
    capi.Discord_Client_UpdateToken(&g_State.m_Client, Discord_AuthorizationTokenType_User, token_arg,
                                    &DiscordSocialClient_OnUpdateToken, &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

bool ClearToken()
{
    g_State.m_AccessToken.clear();
    g_State.m_RefreshToken.clear();
    g_State.m_GrantedScopes.clear();
    g_State.m_ExpiresIn = 0;
    g_State.m_ExpiresAt = 0;
    return true;
}

bool RefreshToken()
{
    if (!ReadyForCalls() || g_State.m_RefreshToken.empty()) {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_String refresh_arg;
    refresh_arg.ptr = (uint8_t*)g_State.m_RefreshToken.data();
    refresh_arg.size = g_State.m_RefreshToken.size();
    capi.Discord_Client_RefreshToken(&g_State.m_Client, g_State.m_Options.m_ApplicationId, refresh_arg,
                                     &DiscordSocialClient_OnTokenExchange,
                                     &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

bool RevokeToken()
{
    if (!ReadyForCalls() || g_State.m_AccessToken.empty()) {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_String access_arg;
    access_arg.ptr = (uint8_t*)g_State.m_AccessToken.data();
    access_arg.size = g_State.m_AccessToken.size();
    capi.Discord_Client_RevokeToken(&g_State.m_Client, g_State.m_Options.m_ApplicationId, access_arg,
                                    &DiscordSocialClient_OnRevokeCompleted,
                                    &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

void DiscordSocialClient_OnRevokeCompleted(Discord_ClientResult* result, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    if (result && capi.Discord_ClientResult_Successful(result)) {
        DropClientResult(result);
        ClearToken();
        return;
    }
    ProcessFailedResult(result, "revoke token");
}

bool TokenConfigured()
{
    return !g_State.m_AccessToken.empty() || !g_State.m_RefreshToken.empty();
}

bool IsAuthenticated()
{
    if (ReadyForCalls()) {
        return g_State.m_Capi->Discord_Client_IsAuthenticated(&g_State.m_Client);
    }
    // Degraded mirror of the SDK flag: a stored non-expired token pair.
    if (g_State.m_AccessToken.empty()) {
        return false;
    }
    return g_State.m_ExpiresAt == 0 || time(0) < g_State.m_ExpiresAt;
}

// ---------------------------------------------------------------------------
// Presence and status
// ---------------------------------------------------------------------------

bool SetOnlineStatus(int status_type)
{
    if (!ReadyForCalls()) {
        return false;
    }
    g_State.m_Capi->Discord_Client_SetOnlineStatus(&g_State.m_Client, (Discord_StatusType)status_type,
                                                   &DiscordSocialClient_OnSimpleResult,
                                                   &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Activity invites and join requests
// ---------------------------------------------------------------------------

void DiscordSocialClient_OnSimpleResult(Discord_ClientResult* result, void*);

bool SendInvite(uint64_t user_id, const char* message)
{
    if (!ReadyForCalls() || user_id == 0) {
        return false;
    }
    std::string content = message ? message : "";
    Discord_String content_arg;
    content_arg.ptr = (uint8_t*)content.data();
    content_arg.size = content.size();
    g_State.m_Capi->Discord_Client_SendActivityInvite(&g_State.m_Client, user_id, content_arg,
                                                      &DiscordSocialClient_OnSimpleResult,
                                                      &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

bool SendJoinRequest(uint64_t user_id)
{
    if (!ReadyForCalls() || user_id == 0) {
        return false;
    }
    g_State.m_Capi->Discord_Client_SendActivityJoinRequest(&g_State.m_Client, user_id,
                                                           &DiscordSocialClient_OnSimpleResult,
                                                           &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

void DiscordSocialClient_OnSimpleResult(Discord_ClientResult* result, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    if (result && !g_State.m_Capi->Discord_ClientResult_Successful(result)) {
        ProcessFailedResult(result, "");
        return;
    }
    DropClientResult(result);
}

// reply semantics for send_join_request_reply: the shipped SDK build carries
// no decline/ignore call (Discord_Client_SendActivityJoinRequestReply takes an
// invite only, see discordpp.h). REPLY_YES sends the "allow" reply, which
// delivers a follow-up invite to the requester; REPLY_NO / REPLY_IGNORE are
// recorded as local answers with no SDK traffic and the request simply stays
// unanswered until Discord expires it.
bool SendJoinRequestReply(const Discord_ActivityInvite* invite)
{
    if (!ReadyForCalls() || !invite) {
        return false;
    }
    // The SDK call takes the invite non-const (it read-backs into its own
    // storage); the cached clone is not mutated from here.
    g_State.m_Capi->Discord_Client_SendActivityJoinRequestReply(&g_State.m_Client,
                                                                const_cast<Discord_ActivityInvite*>(invite),
                                                                &DiscordSocialClient_OnSimpleResult,
                                                                &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

bool SendJoinRequestReplyForSender(uint64_t sender_id, int reply)
{
    if (reply != 1) { // 1 == REPLY_YES
        dmLogInfo("DefoldDiscordSDK: join request reply is local-only (no SDK call exists for decline/ignore"
                  " in this SDK build)");
        return true;
    }
    Discord_ActivityInvite invite;
    memset(&invite, 0, sizeof(invite));
    if (!TakeCachedInvite(sender_id, &invite)) {
        return false;
    }
    bool submitted = SendJoinRequestReply(&invite);
    return submitted;
}

bool AcceptInvite(uint64_t sender_id)
{
    if (!ReadyForCalls() || sender_id == 0) {
        return false;
    }
    Discord_ActivityInvite invite;
    memset(&invite, 0, sizeof(invite));
    if (!TakeCachedInvite(sender_id, &invite)) {
        return false;
    }
    g_State.m_Capi->Discord_Client_AcceptActivityInvite(&g_State.m_Client, &invite,
                                                        &DiscordSocialClient_OnAcceptCompleted,
                                                        &DiscordSocialClient_FreeCallbackData, 0);
    // The cached handle was consumed by the accept call; drop the cache entry
    // (per the API contract an invite can be accepted once).
    ExpungeCachedInvite(sender_id);
    return true;
}

void DiscordSocialClient_OnAcceptCompleted(Discord_ClientResult* result, Discord_String join_secret, void*)
{
    if (!ReadyForCalls()) {
        Discord_Free(join_secret.ptr);
        return;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    std::string secret = DiscordSocialCapi_ReadString(capi, join_secret);
    if (!result || !capi.Discord_ClientResult_Successful(result)) {
        ProcessFailedResult(result, "accept invite");
        return;
    }
    DropClientResult(result);
    // In-game accept delivers through the same event the Discord client's own
    // Join button uses so game code has one join path.
    HandleActivityJoin(secret);
}

// ---------------------------------------------------------------------------
// Lobbies and messages
// ---------------------------------------------------------------------------

void DiscordSocialClient_OnCreateOrJoinCompleted(Discord_ClientResult* result, uint64_t lobby_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    if (!g_State.m_Capi->Discord_ClientResult_Successful(result)) {
        ProcessFailedResult(result, "lobby create or join");
        return;
    }
    DropClientResult(result);
    (void)lobby_id;
    // lobby_created arrives from SetLobbyCreatedCallback, which also covers
    // being added to an existing lobby and lobby recovery.
}

void DiscordSocialClient_OnLeaveCompleted(Discord_ClientResult* result, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    if (!g_State.m_Capi->Discord_ClientResult_Successful(result)) {
        ProcessFailedResult(result, "leave lobby");
        return;
    }
    DropClientResult(result);
}

bool CreateOrJoinLobby(const char* secret,
                       const std::map<std::string, std::string>* lobby_metadata,
                       const std::map<std::string, std::string>* member_metadata)
{
    if (!ReadyForCalls() || !secret || secret[0] == '\0') {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    std::string secret_text(secret);
    Discord_String secret_arg;
    secret_arg.ptr = (uint8_t*)secret_text.data();
    secret_arg.size = secret_text.size();

    if (lobby_metadata != 0 && member_metadata != 0) {
        Discord_Properties lobby_props = DiscordSocialCapi_MakeProperties(capi, *lobby_metadata);
        Discord_Properties member_props = DiscordSocialCapi_MakeProperties(capi, *member_metadata);
        capi.Discord_Client_CreateOrJoinLobbyWithMetadata(&g_State.m_Client, secret_arg, lobby_props, member_props,
                                                          &DiscordSocialClient_OnCreateOrJoinCompleted,
                                                          &DiscordSocialClient_FreeCallbackData, 0);
        capi.Discord_FreeProperties(lobby_props);
        capi.Discord_FreeProperties(member_props);
    } else {
        capi.Discord_Client_CreateOrJoinLobby(&g_State.m_Client, secret_arg,
                                              &DiscordSocialClient_OnCreateOrJoinCompleted,
                                              &DiscordSocialClient_FreeCallbackData, 0);
    }
    return true;
}

bool LeaveLobby(uint64_t lobby_id)
{
    if (!ReadyForCalls() || lobby_id == 0) {
        return false;
    }
    g_State.m_Capi->Discord_Client_LeaveLobby(&g_State.m_Client, lobby_id,
                                              &DiscordSocialClient_OnLeaveCompleted,
                                              &DiscordSocialClient_FreeCallbackData, 0);
    DeleteMessageCacheEntries(lobby_id);
    return true;
}

void DiscordSocialClient_OnSendLobbyMessageCompleted(Discord_ClientResult* result, uint64_t message_id, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    if (!g_State.m_Capi->Discord_ClientResult_Successful(result)) {
        ProcessFailedResult(result, "lobby message");
        return;
    }
    DropClientResult(result);
    (void)message_id; // delivery arrives through message_created
}

bool SendLobbyMessage(uint64_t lobby_id, const char* content)
{
    if (!ReadyForCalls() || lobby_id == 0 || !content) {
        return false;
    }
    std::string text(content);
    Discord_String content_arg;
    content_arg.ptr = (uint8_t*)text.data();
    content_arg.size = text.size();
    g_State.m_Capi->Discord_Client_SendLobbyMessage(&g_State.m_Client, lobby_id, content_arg,
                                                    &DiscordSocialClient_OnSendLobbyMessageCompleted,
                                                    &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

bool GetLobbyIds(std::vector<uint64_t>* out)
{
    if (!ReadyForCalls() || !out) {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_UInt64Span span;
    memset(&span, 0, sizeof(span));
    capi.Discord_Client_GetLobbyIds(&g_State.m_Client, &span);
    out->assign(span.ptr, span.ptr + span.size);
    if (span.ptr) {
        capi.Discord_Free(span.ptr);
    }
    return true;
}

bool GetLobbySnap(uint64_t lobby_id, LobbySnap* out)
{
    if (!ReadyForCalls() || lobby_id == 0 || !out) {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    *out = LobbySnap();
    out->m_Id = lobby_id;

    Discord_LobbyHandle handle;
    memset(&handle, 0, sizeof(handle));
    if (!capi.Discord_Client_GetLobbyHandle(&g_State.m_Client, lobby_id, &handle)) {
        return false;
    }
    Discord_Properties properties;
    memset(&properties, 0, sizeof(properties));
    capi.Discord_LobbyHandle_Metadata(&handle, &properties);
    DiscordSocialCapi_ReadProperties(capi, properties, &out->m_Metadata);

    Discord_UInt64Span members;
    memset(&members, 0, sizeof(members));
    capi.Discord_LobbyHandle_LobbyMemberIds(&handle, &members);
    for (size_t i = 0; i < members.size; ++i) {
        LobbyMemberSnap member;
        member.m_User.m_Id = members.ptr[i];
        Discord_LobbyMemberHandle member_handle;
        memset(&member_handle, 0, sizeof(member_handle));
        if (capi.Discord_LobbyHandle_GetLobbyMemberHandle(&handle, members.ptr[i], &member_handle)) {
            member.m_IsConnected = capi.Discord_LobbyMemberHandle_Connected(&member_handle);
            Discord_UserHandle user;
            memset(&user, 0, sizeof(user));
            if (capi.Discord_LobbyMemberHandle_User(&member_handle, &user)) {
                member.m_HasUser = true;
                ReadUserSnap(capi, &user, &member.m_User);
                capi.Discord_UserHandle_Drop(&user);
            }
            capi.Discord_LobbyMemberHandle_Drop(&member_handle);
        }
        out->m_Members.push_back(member);
    }
    if (members.ptr) {
        capi.Discord_Free(members.ptr);
    }
    capi.Discord_LobbyHandle_Drop(&handle);
    return true;
}

void DiscordSocialClient_OnFetchedMessages(Discord_ClientResult* result, Discord_MessageHandleSpan messages,
                                           void* data)
{
    FetchMessagesContext* context = (FetchMessagesContext*)data;
    uint64_t lobby_id = context->m_LobbyId;
    delete context;

    if (!ReadyForCalls()) {
        for (size_t i = 0; i < messages.size; ++i) {
            g_State.m_Capi->Discord_MessageHandle_Drop(&messages.ptr[i]);
        }
        if (messages.ptr) {
            g_State.m_Capi->Discord_Free(messages.ptr);
        }
        DropClientResult(result);
        return;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    if (!result || !capi.Discord_ClientResult_Successful(result)) {
        for (size_t i = 0; i < messages.size; ++i) {
            capi.Discord_MessageHandle_Drop(&messages.ptr[i]);
        }
        if (messages.ptr) {
            capi.Discord_Free(messages.ptr);
        }
        ProcessFailedResult(result, "lobby message history");
        return;
    }
    DropClientResult(result);

    std::vector<MessageSnap> fetched;
    for (size_t i = 0; i < messages.size; ++i) {
        MessageSnap snap;
        ReadMessageSnap(capi, &messages.ptr[i], &snap);
        capi.Discord_MessageHandle_Drop(&messages.ptr[i]);
        fetched.push_back(snap);
    }
    if (messages.ptr) {
        capi.Discord_Free(messages.ptr);
    }
    HandleLobbyMessagesFetched(lobby_id, fetched);
}

bool GetLobbyMessages(uint64_t lobby_id, int limit)
{
    if (!ReadyForCalls() || lobby_id == 0) {
        return false;
    }
    FetchMessagesContext* context = new FetchMessagesContext();
    context->m_LobbyId = lobby_id;
    g_State.m_Capi->Discord_Client_GetLobbyMessagesWithLimit(&g_State.m_Client, lobby_id, limit,
                                                             &DiscordSocialClient_OnFetchedMessages,
                                                             &DiscordSocialClient_FreeCallbackData, context);
    return true;
}

bool GetCachedMessages(uint64_t lobby_id, std::vector<MessageSnap>* out)
{
    if (!out) {
        return false;
    }
    out->clear();
    std::vector<MessageSnap>* cache = MessageCacheFor(lobby_id, false);
    if (!cache) {
        return false;
    }
    out->assign(cache->begin(), cache->end());
    return true;
}

// ---------------------------------------------------------------------------
// Voice and audio
// ---------------------------------------------------------------------------

bool StartVoice(uint64_t lobby_id)
{
    if (!ReadyForCalls() || lobby_id == 0) {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    for (size_t i = 0; i < g_State.m_VoiceChannels.size(); ++i) {
        if (g_State.m_VoiceChannels[i] == lobby_id) {
            return true; // already talking here
        }
    }
    Discord_Call call;
    memset(&call, 0, sizeof(call));
    if (!capi.Discord_Client_StartCall(&g_State.m_Client, lobby_id, &call)) {
        return false;
    }
    // The handle value is consumed immediately; the call itself keeps running
    // and later fetches come back through GetCall.
    capi.Discord_Call_Drop(&call);
    g_State.m_VoiceChannels.push_back(lobby_id);
    return true;
}

void DiscordSocialClient_OnEndCallCompleted(void*)
{
    // EndCall / EndCalls carry only user data in this SDK build (no result).
}

bool EndVoiceChannel(uint64_t channel_id)
{
    g_State.m_Capi->Discord_Client_EndCall(&g_State.m_Client, channel_id,
                                           &DiscordSocialClient_OnEndCallCompleted,
                                           &DiscordSocialClient_FreeCallbackData, 0);
    for (size_t i = 0; i < g_State.m_VoiceChannels.size(); ++i) {
        if (g_State.m_VoiceChannels[i] == channel_id) {
            g_State.m_VoiceChannels.erase(g_State.m_VoiceChannels.begin() + i);
            break;
        }
    }
    return true;
}

bool StopVoice(uint64_t lobby_id)
{
    if (!ReadyForCalls()) {
        return false;
    }
    if (lobby_id != 0) {
        for (size_t i = 0; i < g_State.m_VoiceChannels.size(); ++i) {
            if (g_State.m_VoiceChannels[i] == lobby_id) {
                return EndVoiceChannel(lobby_id);
            }
        }
        return false;
    }
    // No argument: leave the most recent call we started (ordered back).
    if (!g_State.m_VoiceChannels.empty()) {
        return EndVoiceChannel(g_State.m_VoiceChannels.back());
    }

    // The engine, the game or the player could have joined another call from
    // the Discord side; fall back to enumerating the active calls.
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_CallSpan calls;
    memset(&calls, 0, sizeof(calls));
    capi.Discord_Client_GetCalls(&g_State.m_Client, &calls);
    bool found_any = calls.size > 0;
    for (size_t i = 0; i < calls.size; ++i) {
        capi.Discord_Call_Drop(&calls.ptr[i]);
    }
    if (calls.ptr) {
        capi.Discord_Free(calls.ptr);
    }
    if (!found_any) {
        return false;
    }
    capi.Discord_Client_EndCalls(&g_State.m_Client, &DiscordSocialClient_OnEndCallCompleted,
                                 &DiscordSocialClient_FreeCallbackData, 0);
    g_State.m_VoiceChannels.clear();
    return true;
}

bool StopAllVoice()
{
    if (!ReadyForCalls()) {
        return false;
    }
    g_State.m_Capi->Discord_Client_EndCalls(&g_State.m_Client, &DiscordSocialClient_OnEndCallCompleted,
                                            &DiscordSocialClient_FreeCallbackData, 0);
    g_State.m_VoiceChannels.clear();
    return true;
}

bool SetSelfMute(bool muted)
{
    if (!ReadyForCalls()) {
        return false;
    }
    g_State.m_Capi->Discord_Client_SetSelfMuteAll(&g_State.m_Client, muted);
    return true;
}

bool SetSelfDeaf(bool deaf)
{
    if (!ReadyForCalls()) {
        return false;
    }
    g_State.m_Capi->Discord_Client_SetSelfDeafAll(&g_State.m_Client, deaf);
    return true;
}

bool SetNoiseCancellation(bool on)
{
    if (!ReadyForCalls()) {
        return false;
    }
    g_State.m_Capi->Discord_Client_SetNoiseCancellation(&g_State.m_Client, on);
    return true;
}

bool SetNoiseSuppression(bool on)
{
    if (!ReadyForCalls()) {
        return false;
    }
    g_State.m_Capi->Discord_Client_SetNoiseSuppression(&g_State.m_Client, on);
    return true;
}

bool SetEchoCancellation(bool on)
{
    if (!ReadyForCalls()) {
        return false;
    }
    g_State.m_Capi->Discord_Client_SetEchoCancellation(&g_State.m_Client, on);
    return true;
}

bool SetAutomaticGainControl(bool on)
{
    if (!ReadyForCalls()) {
        return false;
    }
    g_State.m_Capi->Discord_Client_SetAutomaticGainControl(&g_State.m_Client, on);
    return true;
}

float GetInputVolume()
{
    if (!ReadyForCalls()) {
        return 100.0f;
    }
    return g_State.m_Capi->Discord_Client_GetInputVolume(&g_State.m_Client);
}

void SetInputVolume(float percent)
{
    if (!ReadyForCalls()) {
        return;
    }
    if (percent < 0.0f) {
        percent = 0.0f;
    }
    if (percent > 100.0f) {
        percent = 100.0f;
    }
    g_State.m_Capi->Discord_Client_SetInputVolume(&g_State.m_Client, percent);
}

float GetOutputVolume()
{
    if (!ReadyForCalls()) {
        return 100.0f;
    }
    return g_State.m_Capi->Discord_Client_GetOutputVolume(&g_State.m_Client);
}

void SetOutputVolume(float percent)
{
    if (!ReadyForCalls()) {
        return;
    }
    if (percent < 0.0f) {
        percent = 0.0f;
    }
    if (percent > 100.0f) {
        percent = 100.0f;
    }
    g_State.m_Capi->Discord_Client_SetOutputVolume(&g_State.m_Client, percent);
}

void DiscordSocialClient_OnInputDevicesFetched(Discord_AudioDeviceSpan devices, void* data)
{
    FetchDevicesContext* context = (FetchDevicesContext*)data;
    bool input_devices = context ? context->m_Input : true;
    delete context;

    if (!g_State.m_Capi) {
        return;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    std::vector<DeviceSnap> out;
    for (size_t i = 0; i < devices.size; ++i) {
        DeviceSnap snap;
        Discord_String s;
        memset(&s, 0, sizeof(s));
        capi.Discord_AudioDevice_Id(&devices.ptr[i], &s);
        snap.m_Id = DiscordSocialCapi_ReadString(capi, s);
        capi.Discord_AudioDevice_Name(&devices.ptr[i], &s);
        snap.m_Name = DiscordSocialCapi_ReadString(capi, s);
        snap.m_IsDefault = capi.Discord_AudioDevice_IsDefault(&devices.ptr[i]);
        out.push_back(snap);
        capi.Discord_AudioDevice_Drop(&devices.ptr[i]);
    }
    if (devices.ptr) {
        capi.Discord_Free(devices.ptr);
    }
    std::vector<DeviceSnap>& target = input_devices ? g_State.m_InputDevices : g_State.m_OutputDevices;
    target = out;
}

bool GetCachedInputDevices(std::vector<DeviceSnap>* out)
{
    if (!out) {
        return false;
    }
    out->assign(g_State.m_InputDevices.begin(), g_State.m_InputDevices.end());
    return true;
}

bool GetCachedOutputDevices(std::vector<DeviceSnap>* out)
{
    if (!out) {
        return false;
    }
    out->assign(g_State.m_OutputDevices.begin(), g_State.m_OutputDevices.end());
    return true;
}

bool FetchInputDevices()
{
    if (!ReadyForCalls()) {
        return false;
    }
    FetchDevicesContext* context = new FetchDevicesContext();
    context->m_Input = true;
    g_State.m_Capi->Discord_Client_GetInputDevices(&g_State.m_Client,
                                                   &DiscordSocialClient_OnInputDevicesFetched,
                                                   &DiscordSocialClient_FreeCallbackData, context);
    return true;
}

bool FetchOutputDevices()
{
    if (!ReadyForCalls()) {
        return false;
    }
    FetchDevicesContext* context = new FetchDevicesContext();
    context->m_Input = false;
    g_State.m_Capi->Discord_Client_GetOutputDevices(&g_State.m_Client,
                                                    &DiscordSocialClient_OnInputDevicesFetched,
                                                    &DiscordSocialClient_FreeCallbackData, context);
    return true;
}

void DiscordSocialClient_OnInputDeviceSet(Discord_ClientResult* result, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    ProcessResultOnly(result, "input device");
}

void DiscordSocialClient_OnOutputDeviceSet(Discord_ClientResult* result, void*)
{
    if (!ReadyForCalls()) {
        return;
    }
    ProcessResultOnly(result, "output device");
}

bool SetInputDevice(const char* device_id)
{
    if (!ReadyForCalls() || !device_id) {
        return false;
    }
    std::string id(device_id);
    Discord_String id_arg;
    id_arg.ptr = (uint8_t*)id.data();
    id_arg.size = id.size();
    g_State.m_Capi->Discord_Client_SetInputDevice(&g_State.m_Client, id_arg,
                                                  &DiscordSocialClient_OnInputDeviceSet,
                                                  &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

bool SetOutputDevice(const char* device_id)
{
    if (!ReadyForCalls() || !device_id) {
        return false;
    }
    std::string id(device_id);
    Discord_String id_arg;
    id_arg.ptr = (uint8_t*)id.data();
    id_arg.size = id.size();
    g_State.m_Capi->Discord_Client_SetOutputDevice(&g_State.m_Client, id_arg,
                                                   &DiscordSocialClient_OnOutputDeviceSet,
                                                   &DiscordSocialClient_FreeCallbackData, 0);
    return true;
}

// ---------------------------------------------------------------------------
// Friends
// ---------------------------------------------------------------------------

bool GetRelationships(std::vector<RelationshipSnap>* out)
{
    if (!ReadyForCalls() || !out) {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_RelationshipHandleSpan span;
    memset(&span, 0, sizeof(span));
    capi.Discord_Client_GetRelationships(&g_State.m_Client, &span);
    for (size_t i = 0; i < span.size; ++i) {
        RelationshipSnap relationship;
        relationship.m_DiscordType = (int)capi.Discord_RelationshipHandle_DiscordRelationshipType(&span.ptr[i]);
        relationship.m_GameType = (int)capi.Discord_RelationshipHandle_GameRelationshipType(&span.ptr[i]);
        Discord_UserHandle user;
        memset(&user, 0, sizeof(user));
        if (capi.Discord_RelationshipHandle_User(&span.ptr[i], &user)) {
            ReadUserSnap(capi, &user, &relationship.m_User);
            capi.Discord_UserHandle_Drop(&user);
        } else {
            uint64_t id = capi.Discord_RelationshipHandle_Id(&span.ptr[i]);
            relationship.m_User = UserSnap();
            relationship.m_User.m_Id = id;
        }
        out->push_back(relationship);
        capi.Discord_RelationshipHandle_Drop(&span.ptr[i]);
    }
    if (span.ptr) {
        capi.Discord_Free(span.ptr);
    }
    return true;
}

bool GetRelationship(uint64_t user_id, RelationshipSnap* out)
{
    if (!ReadyForCalls() || user_id == 0 || !out) {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_RelationshipHandle handle;
    memset(&handle, 0, sizeof(handle));
    capi.Discord_Client_GetRelationshipHandle(&g_State.m_Client, user_id, &handle);
    bool found = capi.Discord_RelationshipHandle_Id(&handle) != 0;
    if (found) {
        *out = RelationshipSnap();
        out->m_DiscordType = (int)capi.Discord_RelationshipHandle_DiscordRelationshipType(&handle);
        out->m_GameType = (int)capi.Discord_RelationshipHandle_GameRelationshipType(&handle);
        Discord_UserHandle user;
        memset(&user, 0, sizeof(user));
        if (capi.Discord_RelationshipHandle_User(&handle, &user)) {
            ReadUserSnap(capi, &user, &out->m_User);
            capi.Discord_UserHandle_Drop(&user);
        } else {
            out->m_User.m_Id = user_id;
        }
        out->m_User.m_Id = user_id;
    }
    capi.Discord_RelationshipHandle_Drop(&handle);
    return found;
}

bool GetUserSnap(uint64_t user_id, UserSnap* out)
{
    if (!ReadyForCalls() || user_id == 0 || !out) {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_UserHandle user;
    memset(&user, 0, sizeof(user));
    if (!capi.Discord_Client_GetUser(&g_State.m_Client, user_id, &user)) {
        return false;
    }
    ReadUserSnap(capi, &user, out);
    capi.Discord_UserHandle_Drop(&user);
    return true;
}

bool GetCurrentUserSnap(UserSnap* out)
{
    if (!ReadyForCalls() || !out) {
        return false;
    }
    const DiscordSocialCapi& capi = *g_State.m_Capi;
    Discord_UserHandle user;
    memset(&user, 0, sizeof(user));
    // GetCurrentUserV2 is the supported variant (its predecessor is deprecated
    // in the SDK header).
    if (!capi.Discord_Client_GetCurrentUserV2(&g_State.m_Client, &user)) {
        return false;
    }
    ReadUserSnap(capi, &user, out);
    capi.Discord_UserHandle_Drop(&user);
    return true;
}

} // namespace discordsdk_client

// ---------------------------------------------------------------------------
// Global helpers shared with discord_presence.cpp
// ---------------------------------------------------------------------------

void DiscordSocialClient_FreeCallbackData(void* data)
{
    // All screen of callback contexts in this extension are trivially
    // destructible and allocated with plain new, so the global operator
    // delete is the single release point.
    if (data != 0) {
        ::operator delete(data);
    }
}

void DiscordSocialClient_OnRichPresenceResult(Discord_ClientResult* result, void*)
{
    bool ok = false;
    std::string error;
    if (result) {
        if (g_State.m_Capi && g_State.m_Capi->m_Loaded) {
            ok = g_State.m_Capi->Discord_ClientResult_Successful(result);
            if (!ok) {
                error = ResultErrorString(*g_State.m_Capi, result);
            }
        }
        DropClientResult(result);
    }
    PushPresenceResultEvent(ok, error);
}
