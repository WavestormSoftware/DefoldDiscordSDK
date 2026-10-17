// Copyright (c) 2026 Wavestorm Software
// DefoldDiscordSDK Defold extension
//
// Owner of the single Discord_Client instance.
//
// Lifecycle (mirrors the official wrapper): the pointer-sized Discord_Client
// struct is Init()'d in place, SDK callbacks are registered, and Drop() is
// called on shutdown. The extension deliberately stays one client alive; a
// second initialize() tears the previous one down first. Auth order policy is
// explicit by design: authorize()/set_token() end in the `authorized` event,
// and connect() remains a separate, explicit step afterwards (UpdateToken
// completing is what makes Connect safe to call).
//
// Snapshot structs below are the only data crossing into the Lua glue
// (extension.cpp): SDK handles are read, converted and freed immediately, so
// no SDK object ever escapes this module. The same structs feed both the Lua
// facades and the event payloads pushed through discord_events.

#ifndef DISCORD_SOCIAL_CLIENT_H
#define DISCORD_SOCIAL_CLIENT_H

#include <string>
#include <vector>
#include <map>

#include <dmsdk/sdk.h>

#include "discordsdk_capi.h"

namespace discordsdk_client {

// Startup configuration, resolved from game.project + initialize() opts.
struct Options
{
    uint64_t m_ApplicationId;
    std::string m_Scopes;          // space separated OAuth2 scope list
    std::string m_DefaultRedirect; // redirect passed to GetToken when the authorize callback carries none
    uint32_t m_SteamAppId;
    bool m_RegisterLaunchCommand;  // auto_register_launch: register the current executable/protocol
    bool m_RegisterLaunchSteam;    // steam_app_id non-zero implies this as well

    Options()
        : m_ApplicationId(0)
        , m_SteamAppId(0)
        , m_RegisterLaunchCommand(true)
        , m_RegisterLaunchSteam(false)
    {}
};

// SDK handle wrappers resolved into plain values the moment they are read.
struct UserSnap
{
    uint64_t m_Id;
    std::string m_Username;
    std::string m_DisplayName;
    std::string m_Avatar;
    int m_Status;             // Discord_StatusType as defined in cdiscourd.h
    bool m_IsProvisional;
    bool m_HasGameActivity;   // target plays this game right now
    bool m_InLobby;           // filled from lobby member handles when present
    bool m_ConnectedToVoice;  // lobby member handle only

    UserSnap() : m_Id(0), m_Status(7), m_IsProvisional(false), m_HasGameActivity(false), m_InLobby(false), m_ConnectedToVoice(false) {}
};

struct InviteSnap
{
    uint64_t m_SenderId;
    uint64_t m_ChannelId;
    uint64_t m_MessageId;
    uint64_t m_ApplicationId;
    uint64_t m_ParentApplicationId;
    int m_Type;                // Discord_ActivityActionTypes (5 == join request)
    std::string m_PartyId;
    std::string m_SessionId;
    std::string m_Message;     // card text, when the invite message is still readable
    bool m_IsValid;
    bool m_HasActivity;        // activity summary of what is being joined, if resolvable
    UserSnap m_Sender;
    std::string m_ActivityName;
    std::string m_ActivityState;
    std::string m_ActivityDetails;

    InviteSnap()
        : m_SenderId(0)
        , m_ChannelId(0)
        , m_MessageId(0)
        , m_ApplicationId(0)
        , m_ParentApplicationId(0)
        , m_Type(0)
        , m_IsValid(false)
        , m_HasActivity(false)
    {}
};

struct RelationshipSnap
{
    int m_DiscordType;   // Discord_RelationshipType
    int m_GameType;      // game-side relationship type
    UserSnap m_User;

    RelationshipSnap() : m_DiscordType(0), m_GameType(0) {}
};

struct LobbyMemberSnap
{
    UserSnap m_User;
    bool m_HasUser;
    bool m_IsConnected;

    LobbyMemberSnap() : m_HasUser(false), m_IsConnected(false) {}
};

struct LobbySnap
{
    uint64_t m_Id;
    std::map<std::string, std::string> m_Metadata;
    std::vector<LobbyMemberSnap> m_Members;

    LobbySnap() : m_Id(0) {}
};

struct MessageSnap
{
    uint64_t m_Id;
    uint64_t m_ChannelId;
    uint64_t m_LobbyId;        // sent to a lobby? SDK reports membership explicitly
    bool m_HasLobby;
    uint64_t m_AuthorId;
    std::string m_AuthorUsername;
    std::string m_Content;
    uint64_t m_SentTimestampMs; // SDK unit: milliseconds since epoch (see notes)
    uint64_t m_EditedTimestampMs;
    bool m_SentFromGame;

    MessageSnap()
        : m_Id(0)
        , m_ChannelId(0)
        , m_LobbyId(0)
        , m_HasLobby(false)
        , m_AuthorId(0)
        , m_SentTimestampMs(0)
        , m_EditedTimestampMs(0)
        , m_SentFromGame(false)
    {}
};

struct VoiceSettingsSnap
{
    bool m_SelfMute;
    bool m_SelfDeaf;
    int m_InputMode;         // Discord_VoiceInputModeType
    float m_InputVolume;     // percent 0..100
    float m_OutputVolume;
    bool m_AutomaticGainControl;
    bool m_EchoCancellation;
    bool m_NoiseSuppression;
    bool m_NoiseCancellation;

    VoiceSettingsSnap()
        : m_SelfMute(false)
        , m_SelfDeaf(false)
        , m_InputMode(0)
        , m_InputVolume(100)
        , m_OutputVolume(100)
        , m_AutomaticGainControl(false)
        , m_EchoCancellation(false)
        , m_NoiseSuppression(false)
        , m_NoiseCancellation(false)
    {}
};

struct DeviceSnap
{
    std::string m_Id;
    std::string m_Name;
    bool m_IsDefault;
};

// Lua table pushers shared by extension.cpp and the event dispatch inside this
// module (events are pushed from here directly; payloads use identical shapes).
void PushUser(lua_State* L, const UserSnap& user);
void PushRelationship(lua_State* L, const RelationshipSnap& relationship);
void PushInvite(lua_State* L, const InviteSnap& invite);
void PushLobby(lua_State* L, const LobbySnap& lobby);
void PushMessage(lua_State* L, const MessageSnap& message);
void PushVoiceSettings(lua_State* L, const VoiceSettingsSnap& settings);
void PushDevice(lua_State* L, const DeviceSnap& device);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool IsRunning();
Discord_Client* GetClient();
const Options* GetOptions();
const DiscordSocialCapi* GetCapi();

// Creates and starts the client; tears down a previous instance first.
// Returns false when the capi table is not available (SDK not loaded).
bool Create(const DiscordSocialCapi* capi, const Options& opts);
// Stops the client (Drop) and discards caches. Later SDK calls are refused.
void Shutdown();

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void Connect();
void Disconnect();
// Human readable connection state, matching the status_changed payload.
std::string StatusName();
// Ready means the handshake event fired once (is_ready Lua semantics).
bool IsReady();

// ---------------------------------------------------------------------------
// OAuth and tokens
// ---------------------------------------------------------------------------

bool Authorize();                              // challenge -> args -> Authorize
bool SetToken(const char* access, const char* refresh); // UpdateToken, no auto-connect
bool ClearToken();                             // forgets stored credentials
bool RefreshToken();                           // refresh -> UpdateToken
bool RevokeToken();                            // server side grant invalidation
bool IsAuthenticated();
bool TokenConfigured();                        // a token pair is stored locally

// ---------------------------------------------------------------------------
// Presence and status
// ---------------------------------------------------------------------------

bool SetOnlineStatus(int status_type); // Discord_StatusType value

// ---------------------------------------------------------------------------
// Activity invites and join requests
// ---------------------------------------------------------------------------

bool SendInvite(uint64_t user_id, const char* message);
bool SendJoinRequest(uint64_t user_id);
// `reply` is advisory in the v-current SDK: only an explicit "allow" issues a
// SDK call; NO/IGNORE are recorded locally (see the note in discord_client.cpp).
bool SendJoinRequestReply(const Discord_ActivityInvite* invite);
bool SendJoinRequestReplyForSender(uint64_t sender_id, int reply);
bool AcceptInvite(uint64_t sender_id);       // consumes the cached invite

// ---------------------------------------------------------------------------
// Lobbies and messages
// ---------------------------------------------------------------------------

bool CreateOrJoinLobby(const char* secret,
                       const std::map<std::string, std::string>* lobby_metadata,
                       const std::map<std::string, std::string>* member_metadata);
bool LeaveLobby(uint64_t lobby_id);
bool SendLobbyMessage(uint64_t lobby_id, const char* content);
bool GetLobbyIds(std::vector<uint64_t>* out);
bool GetLobbySnap(uint64_t lobby_id, LobbySnap* out);
bool GetLobbyMessages(uint64_t lobby_id, int limit);
bool GetCachedMessages(uint64_t lobby_id, std::vector<MessageSnap>* out);

// ---------------------------------------------------------------------------
// Voice and audio
// ---------------------------------------------------------------------------

bool StartVoice(uint64_t lobby_id);
bool StopVoice(uint64_t lobby_id);   // 0 ends the most recent call
bool StopAllVoice();
bool SetSelfMute(bool muted);
bool SetSelfDeaf(bool deaf);
bool SetNoiseCancellation(bool on);
bool SetNoiseSuppression(bool on);
bool SetEchoCancellation(bool on);
bool SetAutomaticGainControl(bool on);
float GetInputVolume();
void SetInputVolume(float percent);
float GetOutputVolume();
void SetOutputVolume(float percent);
bool GetCachedInputDevices(std::vector<DeviceSnap>* out);
bool GetCachedOutputDevices(std::vector<DeviceSnap>* out);
bool FetchInputDevices(); // async refresh, lands in the next cache read
bool FetchOutputDevices();
bool SetInputDevice(const char* device_id);
bool SetOutputDevice(const char* device_id);

// ---------------------------------------------------------------------------
// Friends
// ---------------------------------------------------------------------------

bool GetRelationships(std::vector<RelationshipSnap>* out);
bool GetRelationship(uint64_t user_id, RelationshipSnap* out);
bool GetUserSnap(uint64_t user_id, UserSnap* out);
bool GetCurrentUserSnap(UserSnap* out);

} // namespace discordsdk_client

// ---------------------------------------------------------------------------
// FreeFn helpers shared beyond this module.
// All heap callback contexts are trivially destructible and allocated with
// plain new, so a single global operator delete releases them.
// ---------------------------------------------------------------------------

// Discord_FreeFn released for SDK callback user data used by this module
// (and discord_presence.cpp). Frees a plain-new heap buffer.
void DiscordSocialClient_FreeCallbackData(void* data);

// UpdateRichPresence completion callback: reports the acknowledged update
// through the `presence_result` event and drops the SDK result.
void DiscordSocialClient_OnRichPresenceResult(Discord_ClientResult* result, void* data);

#endif // DISCORD_SOCIAL_CLIENT_H
