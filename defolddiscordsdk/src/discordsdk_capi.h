// Copyright (c) 2026 Wavestorm Software
// DefoldDiscordSDK Defold extension
//
// Typed function pointer table for the Discord Social SDK C API.
//
// The extension does NOT link the SDK. discord_partner_sdk is runtime-loaded
// and every entry below is resolved with dlsym/GetProcAddress. The signatures
// are copied verbatim from the vendored C header (include/cdiscourd.h) and are
// compile-time verified against it (see the static_assert block at the bottom
// of discordsdk_capi.cpp), so drift between the two cannot go unnoticed.

#ifndef DISCORD_SOCIAL_CAPI_H
#define DISCORD_SOCIAL_CAPI_H

#include <map>
#include <string>
#include <stdint.h>
#include <stddef.h>

// The vendored header keeps its own export-attribute noise; the extension only
// declares these symbols, it never defines or references them at link time.
#define DISCORD_API
#include "cdiscourd.h"
#undef DISCORD_API

// The full X-macro list. Format:
//   X(return_type, function_name, (param_types))
// To add a new binding:
//   1. Add an X(...) row here, signature copied from include/cdiscourd.h.
//   2. No other step: the pointer, the resolve loop and the signature check
//      are generated from this list (see DISCORD_SOCIAL_CAPI_FUNCTIONS).
#define DISCORD_SOCIAL_CAPI_FUNCTIONS(X)                                                                              \
    /* core */                                                                                                        \
    X(void, Discord_RunCallbacks, (void))                                                                             \
    X(void*, Discord_Alloc, (size_t))                                                                                 \
    X(void, Discord_Free, (void*))                                                                                    \
    X(void, Discord_FreeProperties, (Discord_Properties))                                                             \
    /* version + scopes (static, no client object needed) */                                                          \
    X(int32_t, Discord_Client_GetVersionMajor, (void))                                                                \
    X(int32_t, Discord_Client_GetVersionMinor, (void))                                                                \
    X(int32_t, Discord_Client_GetVersionPatch, (void))                                                                \
    X(void, Discord_Client_GetVersionHash, (Discord_String*))                                                         \
    X(void, Discord_Client_GetDefaultPresenceScopes, (Discord_String*))                                                \
    X(void, Discord_Client_GetDefaultCommunicationScopes, (Discord_String*))                                          \
    /* client lifecycle */                                                                                            \
    X(void, Discord_Client_Init, (Discord_Client*))                                                                   \
    X(void, Discord_Client_Drop, (Discord_Client*))                                                                   \
    X(void, Discord_Client_SetApplicationId, (Discord_Client*, uint64_t))                                             \
    X(void, Discord_Client_Connect, (Discord_Client*))                                                                \
    X(void, Discord_Client_Disconnect, (Discord_Client*))                                                             \
    X(Discord_Client_Status, Discord_Client_GetStatus, (Discord_Client*))                                             \
    X(void, Discord_Client_ErrorToString, (Discord_Client_Error, Discord_String*))                                    \
    X(bool, Discord_Client_IsAuthenticated, (Discord_Client*))                                                        \
    /* logging + status event plumbing */                                                                            \
    X(void, Discord_Client_SetStatusChangedCallback, (Discord_Client*, Discord_Client_OnStatusChanged, Discord_FreeFn, void*)) \
    X(void, Discord_Client_AddLogCallback, (Discord_Client*, Discord_Client_LogCallback, Discord_FreeFn, void*, Discord_LoggingSeverity)) \
    /* auth */                                                                                                        \
    X(void, Discord_Client_Authorize, (Discord_Client*, Discord_AuthorizationArgs*, Discord_Client_AuthorizationCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_CreateAuthorizationCodeVerifier, (Discord_Client*, Discord_AuthorizationCodeVerifier*))      \
    X(void, Discord_Client_GetToken, (Discord_Client*, uint64_t, Discord_String, Discord_String, Discord_String, Discord_Client_TokenExchangeCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_RefreshToken, (Discord_Client*, uint64_t, Discord_String, Discord_Client_TokenExchangeCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_RevokeToken, (Discord_Client*, uint64_t, Discord_String, Discord_Client_RevokeTokenCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_UpdateToken, (Discord_Client*, Discord_AuthorizationTokenType, Discord_String, Discord_Client_UpdateTokenCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetTokenExpirationCallback, (Discord_Client*, Discord_Client_TokenExpirationCallback, Discord_FreeFn, void*)) \
    /* authorization argument objects */                                                                             \
    X(void, Discord_AuthorizationArgs_Init, (Discord_AuthorizationArgs*))                                             \
    X(void, Discord_AuthorizationArgs_Drop, (Discord_AuthorizationArgs*))                                             \
    X(void, Discord_AuthorizationArgs_SetClientId, (Discord_AuthorizationArgs*, uint64_t))                            \
    X(void, Discord_AuthorizationArgs_SetScopes, (Discord_AuthorizationArgs*, Discord_String))                        \
    X(void, Discord_AuthorizationArgs_SetCodeChallenge, (Discord_AuthorizationArgs*, Discord_AuthorizationCodeChallenge*)) \
    X(void, Discord_AuthorizationCodeChallenge_Init, (Discord_AuthorizationCodeChallenge*))                           \
    X(void, Discord_AuthorizationCodeChallenge_Drop, (Discord_AuthorizationCodeChallenge*))                           \
    X(void, Discord_AuthorizationCodeVerifier_Drop, (Discord_AuthorizationCodeVerifier*))                             \
    X(void, Discord_AuthorizationCodeVerifier_Challenge, (Discord_AuthorizationCodeVerifier*, Discord_AuthorizationCodeChallenge*)) \
    X(void, Discord_AuthorizationCodeVerifier_SetVerifier, (Discord_AuthorizationCodeVerifier*, Discord_String))       \
    X(void, Discord_AuthorizationCodeVerifier_Verifier, (Discord_AuthorizationCodeVerifier*, Discord_String*))         \
    /* launch registration */                                                                                        \
    X(bool, Discord_Client_RegisterLaunchCommand, (Discord_Client*, uint64_t, Discord_String))                        \
    X(bool, Discord_Client_RegisterLaunchSteamApplication, (Discord_Client*, uint64_t, uint32_t))                     \
    /* presence */                                                                                                   \
    X(void, Discord_Client_UpdateRichPresence, (Discord_Client*, Discord_Activity*, Discord_Client_UpdateRichPresenceCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_ClearRichPresence, (Discord_Client*))                                                      \
    X(void, Discord_Client_SetOnlineStatus, (Discord_Client*, Discord_StatusType, Discord_Client_UpdateStatusCallback, Discord_FreeFn, void*)) \
    /* activity + sub-objects */                                                                                     \
    X(void, Discord_Activity_Init, (Discord_Activity*))                                                               \
    X(void, Discord_Activity_Drop, (Discord_Activity*))                                                               \
    X(void, Discord_Activity_SetName, (Discord_Activity*, Discord_String))                                            \
    X(void, Discord_Activity_SetType, (Discord_Activity*, Discord_ActivityTypes))                                     \
    X(void, Discord_Activity_SetStatusDisplayType, (Discord_Activity*, Discord_StatusDisplayTypes*))                  \
    X(void, Discord_Activity_SetState, (Discord_Activity*, Discord_String*))                                          \
    X(void, Discord_Activity_SetStateUrl, (Discord_Activity*, Discord_String*))                                       \
    X(void, Discord_Activity_SetDetails, (Discord_Activity*, Discord_String*))                                        \
    X(void, Discord_Activity_SetDetailsUrl, (Discord_Activity*, Discord_String*))                                     \
    X(void, Discord_Activity_SetSupportedPlatforms, (Discord_Activity*, Discord_ActivityGamePlatforms))               \
    X(void, Discord_Activity_AddButton, (Discord_Activity*, Discord_ActivityButton const*))                           \
    X(void, Discord_Activity_SetTimestamps, (Discord_Activity*, Discord_ActivityTimestamps*))                         \
    X(void, Discord_Activity_SetParty, (Discord_Activity*, Discord_ActivityParty*))                                   \
    X(void, Discord_Activity_SetSecrets, (Discord_Activity*, Discord_ActivitySecrets*))                               \
    X(void, Discord_Activity_SetAssets, (Discord_Activity*, Discord_ActivityAssets*))                                 \
    X(void, Discord_ActivityAssets_Init, (Discord_ActivityAssets*))                                                   \
    X(void, Discord_ActivityAssets_Drop, (Discord_ActivityAssets*))                                                   \
    X(void, Discord_ActivityAssets_SetLargeImage, (Discord_ActivityAssets*, Discord_String*))                         \
    X(void, Discord_ActivityAssets_SetLargeText, (Discord_ActivityAssets*, Discord_String*))                          \
    X(void, Discord_ActivityAssets_SetLargeUrl, (Discord_ActivityAssets*, Discord_String*))                           \
    X(void, Discord_ActivityAssets_SetSmallImage, (Discord_ActivityAssets*, Discord_String*))                         \
    X(void, Discord_ActivityAssets_SetSmallText, (Discord_ActivityAssets*, Discord_String*))                          \
    X(void, Discord_ActivityAssets_SetSmallUrl, (Discord_ActivityAssets*, Discord_String*))                           \
    X(void, Discord_ActivityAssets_SetInviteCoverImage, (Discord_ActivityAssets*, Discord_String*))                   \
    X(void, Discord_ActivityTimestamps_Init, (Discord_ActivityTimestamps*))                                           \
    X(void, Discord_ActivityTimestamps_Drop, (Discord_ActivityTimestamps*))                                           \
    X(void, Discord_ActivityTimestamps_SetStart, (Discord_ActivityTimestamps*, uint64_t))                             \
    X(void, Discord_ActivityTimestamps_SetEnd, (Discord_ActivityTimestamps*, uint64_t))                               \
    X(void, Discord_ActivityParty_Init, (Discord_ActivityParty*))                                                     \
    X(void, Discord_ActivityParty_Drop, (Discord_ActivityParty*))                                                     \
    X(void, Discord_ActivityParty_SetId, (Discord_ActivityParty*, Discord_String))                                    \
    X(void, Discord_ActivityParty_SetCurrentSize, (Discord_ActivityParty*, int32_t))                                  \
    X(void, Discord_ActivityParty_SetMaxSize, (Discord_ActivityParty*, int32_t))                                      \
    X(void, Discord_ActivityParty_SetPrivacy, (Discord_ActivityParty*, Discord_ActivityPartyPrivacy))                 \
    X(void, Discord_ActivitySecrets_Init, (Discord_ActivitySecrets*))                                                 \
    X(void, Discord_ActivitySecrets_Drop, (Discord_ActivitySecrets*))                                                 \
    X(void, Discord_ActivitySecrets_SetJoin, (Discord_ActivitySecrets*, Discord_String))                              \
    X(void, Discord_ActivityButton_Init, (Discord_ActivityButton*))                                                   \
    X(void, Discord_ActivityButton_Drop, (Discord_ActivityButton*))                                                   \
    X(void, Discord_ActivityButton_SetLabel, (Discord_ActivityButton*, Discord_String))                               \
    X(void, Discord_ActivityButton_SetUrl, (Discord_ActivityButton*, Discord_String))                                 \
    /* activity invites + join */                                                                                    \
    X(void, Discord_ActivityInvite_Init, (Discord_ActivityInvite*))                                                   \
    X(void, Discord_ActivityInvite_Drop, (Discord_ActivityInvite*))                                                   \
    X(void, Discord_ActivityInvite_Clone, (Discord_ActivityInvite*, Discord_ActivityInvite const*))                   \
    X(uint64_t, Discord_ActivityInvite_SenderId, (Discord_ActivityInvite*))                                           \
    X(uint64_t, Discord_ActivityInvite_ChannelId, (Discord_ActivityInvite*))                                          \
    X(uint64_t, Discord_ActivityInvite_MessageId, (Discord_ActivityInvite*))                                          \
    X(Discord_ActivityActionTypes, Discord_ActivityInvite_Type, (Discord_ActivityInvite*))                            \
    X(uint64_t, Discord_ActivityInvite_ApplicationId, (Discord_ActivityInvite*))                                      \
    X(uint64_t, Discord_ActivityInvite_ParentApplicationId, (Discord_ActivityInvite*))                                \
    X(void, Discord_ActivityInvite_PartyId, (Discord_ActivityInvite*, Discord_String*))                               \
    X(void, Discord_ActivityInvite_SessionId, (Discord_ActivityInvite*, Discord_String*))                             \
    X(bool, Discord_ActivityInvite_IsValid, (Discord_ActivityInvite*))                                                \
    X(void, Discord_Client_SetActivityInviteCreatedCallback, (Discord_Client*, Discord_Client_ActivityInviteCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetActivityInviteUpdatedCallback, (Discord_Client*, Discord_Client_ActivityInviteCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetActivityJoinCallback, (Discord_Client*, Discord_Client_ActivityJoinCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SendActivityInvite, (Discord_Client*, uint64_t, Discord_String, Discord_Client_SendActivityInviteCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SendActivityJoinRequest, (Discord_Client*, uint64_t, Discord_Client_SendActivityInviteCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SendActivityJoinRequestReply, (Discord_Client*, Discord_ActivityInvite*, Discord_Client_SendActivityInviteCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_AcceptActivityInvite, (Discord_Client*, Discord_ActivityInvite*, Discord_Client_AcceptActivityInviteCallback, Discord_FreeFn, void*)) \
    /* lobbies */                                                                                                    \
    X(void, Discord_Client_CreateOrJoinLobby, (Discord_Client*, Discord_String, Discord_Client_CreateOrJoinLobbyCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_CreateOrJoinLobbyWithMetadata, (Discord_Client*, Discord_String, Discord_Properties, Discord_Properties, Discord_Client_CreateOrJoinLobbyCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_LeaveLobby, (Discord_Client*, uint64_t, Discord_Client_LeaveLobbyCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SendLobbyMessage, (Discord_Client*, uint64_t, Discord_String, Discord_Client_SendUserMessageCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_GetLobbyMessagesWithLimit, (Discord_Client*, uint64_t, int32_t, Discord_Client_GetLobbyMessagesCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_GetLobbyIds, (Discord_Client*, Discord_UInt64Span*))                                       \
    X(bool, Discord_Client_GetLobbyHandle, (Discord_Client*, uint64_t, Discord_LobbyHandle*))                         \
    X(void, Discord_LobbyHandle_Drop, (Discord_LobbyHandle*))                                                        \
    X(uint64_t, Discord_LobbyHandle_Id, (Discord_LobbyHandle*))                                                       \
    X(void, Discord_LobbyHandle_Metadata, (Discord_LobbyHandle*, Discord_Properties*))                                \
    X(void, Discord_LobbyHandle_LobbyMemberIds, (Discord_LobbyHandle*, Discord_UInt64Span*))                          \
    X(bool, Discord_LobbyHandle_GetLobbyMemberHandle, (Discord_LobbyHandle*, uint64_t, Discord_LobbyMemberHandle*))   \
    X(void, Discord_LobbyMemberHandle_Drop, (Discord_LobbyMemberHandle*))                                             \
    X(uint64_t, Discord_LobbyMemberHandle_Id, (Discord_LobbyMemberHandle*))                                           \
    X(bool, Discord_LobbyMemberHandle_Connected, (Discord_LobbyMemberHandle*))                                        \
    X(void, Discord_LobbyMemberHandle_Metadata, (Discord_LobbyMemberHandle*, Discord_Properties*))                    \
    X(bool, Discord_LobbyMemberHandle_User, (Discord_LobbyMemberHandle*, Discord_UserHandle*))                        \
    /* lobby + message events */                                                                                     \
    X(void, Discord_Client_SetLobbyCreatedCallback, (Discord_Client*, Discord_Client_LobbyCreatedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetLobbyDeletedCallback, (Discord_Client*, Discord_Client_LobbyDeletedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetLobbyUpdatedCallback, (Discord_Client*, Discord_Client_LobbyUpdatedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetLobbyMemberAddedCallback, (Discord_Client*, Discord_Client_LobbyMemberAddedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetLobbyMemberRemovedCallback, (Discord_Client*, Discord_Client_LobbyMemberRemovedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetLobbyMemberUpdatedCallback, (Discord_Client*, Discord_Client_LobbyMemberUpdatedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetMessageCreatedCallback, (Discord_Client*, Discord_Client_MessageCreatedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetMessageDeletedCallback, (Discord_Client*, Discord_Client_MessageDeletedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetMessageUpdatedCallback, (Discord_Client*, Discord_Client_MessageUpdatedCallback, Discord_FreeFn, void*)) \
    X(bool, Discord_Client_GetMessageHandle, (Discord_Client*, uint64_t, Discord_MessageHandle*))                     \
    X(void, Discord_MessageHandle_Drop, (Discord_MessageHandle*))                                                     \
    X(uint64_t, Discord_MessageHandle_Id, (Discord_MessageHandle*))                                                   \
    X(uint64_t, Discord_MessageHandle_ChannelId, (Discord_MessageHandle*))                                            \
    X(void, Discord_MessageHandle_Content, (Discord_MessageHandle*, Discord_String*))                                 \
    X(uint64_t, Discord_MessageHandle_AuthorId, (Discord_MessageHandle*))                                             \
    X(bool, Discord_MessageHandle_Author, (Discord_MessageHandle*, Discord_UserHandle*))                              \
    X(uint64_t, Discord_MessageHandle_SentTimestamp, (Discord_MessageHandle*))                                        \
    X(uint64_t, Discord_MessageHandle_EditedTimestamp, (Discord_MessageHandle*))                                      \
    X(bool, Discord_MessageHandle_SentFromGame, (Discord_MessageHandle*))                                             \
    /* voice */                                                                                                      \
    X(bool, Discord_Client_StartCall, (Discord_Client*, uint64_t, Discord_Call*))                                     \
    X(bool, Discord_Client_GetCall, (Discord_Client*, uint64_t, Discord_Call*))                                       \
    X(void, Discord_Client_EndCall, (Discord_Client*, uint64_t, Discord_Client_EndCallCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_EndCalls, (Discord_Client*, Discord_Client_EndCallsCallback, Discord_FreeFn, void*))       \
    X(void, Discord_Call_Drop, (Discord_Call*))                                                                      \
    X(void, Discord_Call_GetParticipants, (Discord_Call*, Discord_UInt64Span*))                                       \
    X(void, Discord_Client_SetVoiceParticipantChangedCallback, (Discord_Client*, Discord_Client_VoiceParticipantChangedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetVoiceSettingsUpdatedCallback, (Discord_Client*, Discord_Client_VoiceSettingsUpdatedCallback, Discord_FreeFn, void*)) \
    X(bool, Discord_Client_GetSelfMuteAll, (Discord_Client*))                                                        \
    X(bool, Discord_Client_GetSelfDeafAll, (Discord_Client*))                                                        \
    X(void, Discord_Client_SetSelfMuteAll, (Discord_Client*, bool))                                                  \
    X(void, Discord_Client_SetSelfDeafAll, (Discord_Client*, bool))                                                  \
    X(void, Discord_Client_SetNoiseCancellation, (Discord_Client*, bool))                                            \
    X(void, Discord_Client_SetNoiseSuppression, (Discord_Client*, bool))                                             \
    X(void, Discord_Client_SetEchoCancellation, (Discord_Client*, bool))                                             \
    X(void, Discord_Client_SetAutomaticGainControl, (Discord_Client*, bool))                                         \
    X(float, Discord_Client_GetInputVolume, (Discord_Client*))                                                       \
    X(void, Discord_Client_SetInputVolume, (Discord_Client*, float))                                                 \
    X(float, Discord_Client_GetOutputVolume, (Discord_Client*))                                                      \
    X(void, Discord_Client_SetOutputVolume, (Discord_Client*, float))                                                \
    X(void, Discord_Client_GetInputDevices, (Discord_Client*, Discord_Client_GetInputDevicesCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_GetOutputDevices, (Discord_Client*, Discord_Client_GetOutputDevicesCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetInputDevice, (Discord_Client*, Discord_String, Discord_Client_SetInputDeviceCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetOutputDevice, (Discord_Client*, Discord_String, Discord_Client_SetOutputDeviceCallback, Discord_FreeFn, void*)) \
    X(void, Discord_VoiceSettings_Drop, (Discord_VoiceSettings*))                                                    \
    X(bool, Discord_VoiceSettings_SelfMute, (Discord_VoiceSettings*))                                                \
    X(bool, Discord_VoiceSettings_SelfDeaf, (Discord_VoiceSettings*))                                                \
    X(Discord_VoiceInputModeType, Discord_VoiceSettings_InputMode, (Discord_VoiceSettings*))                         \
    X(float, Discord_VoiceSettings_InputVolume, (Discord_VoiceSettings*))                                            \
    X(float, Discord_VoiceSettings_OutputVolume, (Discord_VoiceSettings*))                                           \
    X(bool, Discord_VoiceSettings_AutomaticGainControl, (Discord_VoiceSettings*))                                    \
    X(bool, Discord_VoiceSettings_EchoCancellation, (Discord_VoiceSettings*))                                        \
    X(bool, Discord_VoiceSettings_NoiseSuppression, (Discord_VoiceSettings*))                                        \
    X(bool, Discord_VoiceSettings_NoiseCancellation, (Discord_VoiceSettings*))                                       \
    X(void, Discord_AudioDevice_Id, (Discord_AudioDevice*, Discord_String*))                                         \
    X(void, Discord_AudioDevice_Name, (Discord_AudioDevice*, Discord_String*))                                       \
    X(bool, Discord_AudioDevice_IsDefault, (Discord_AudioDevice*))                                                   \
    /* relationships */                                                                                              \
    X(void, Discord_Client_SetRelationshipCreatedCallback, (Discord_Client*, Discord_Client_RelationshipCreatedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetRelationshipDeletedCallback, (Discord_Client*, Discord_Client_RelationshipDeletedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetRelationshipGroupsUpdatedCallback, (Discord_Client*, Discord_Client_RelationshipGroupsUpdatedCallback, Discord_FreeFn, void*)) \
    X(void, Discord_Client_SetUserUpdatedCallback, (Discord_Client*, Discord_Client_UserUpdatedCallback, Discord_FreeFn, void*)) \
    X(bool, Discord_Client_GetUser, (Discord_Client*, uint64_t, Discord_UserHandle*)) \
    /* rows below were added on top of the original binding list for the full    \
       Lua surface (see README-SRC.md, "adding a C binding"). Signatures are    \
       copied verbatim from include/cdiscourd.h and hold the same               \
       compile-time guarantee as every row above. */                            \
    /* client result reading: every async SDK call hands ownership of the       \
       result to the callback, so the extension needs Drop + accessors. */      \
    X(void, Discord_ClientResult_Drop, (Discord_ClientResult*))                 \
    X(bool, Discord_ClientResult_Successful, (Discord_ClientResult*))           \
    X(void, Discord_ClientResult_Error, (Discord_ClientResult*, Discord_String*)) \
    X(void, Discord_ClientResult_ToString, (Discord_ClientResult*, Discord_String*)) \
    /* friends list query and relationship extras */                            \
    X(void, Discord_Client_GetRelationships, (Discord_Client*, Discord_RelationshipHandleSpan*)) \
    X(uint64_t, Discord_RelationshipHandle_Id, (Discord_RelationshipHandle*))   \
    X(Discord_RelationshipType, Discord_RelationshipHandle_GameRelationshipType, (Discord_RelationshipHandle*)) \
    /* user extra fields for the Lua user table */                              \
    X(bool, Discord_UserHandle_IsProvisional, (Discord_UserHandle*))            \
    X(bool, Discord_UserHandle_GameActivity, (Discord_UserHandle*, Discord_Activity*)) \
    /* activity getters used for invite summaries and in-game detection */      \
    X(void, Discord_Activity_Name, (Discord_Activity*, Discord_String*))        \
    X(bool, Discord_Activity_State, (Discord_Activity*, Discord_String*))       \
    X(bool, Discord_Activity_Details, (Discord_Activity*, Discord_String*))     \
    /* message -> lobby association for the message payload's lobby_id field */ \
    X(bool, Discord_MessageHandle_Lobby, (Discord_MessageHandle*, Discord_LobbyHandle*)) \
    /* active call enumeration used by the no-argument stop_voice() */           \
    X(void, Discord_Client_GetCalls, (Discord_Client*, Discord_CallSpan*))       \
    /* individual span members carry their own impl payload and must be         \
       dropped separately after reading (see the official wrapper). */           \
    X(void, Discord_AudioDevice_Drop, (Discord_AudioDevice*))                    \
    X(bool, Discord_Client_GetCurrentUserV2, (Discord_Client*, Discord_UserHandle*))                                 \
    X(void, Discord_Client_GetRelationshipHandle, (Discord_Client*, uint64_t, Discord_RelationshipHandle*))          \
    X(void, Discord_RelationshipHandle_Drop, (Discord_RelationshipHandle*))                                          \
    X(Discord_RelationshipType, Discord_RelationshipHandle_DiscordRelationshipType, (Discord_RelationshipHandle*))    \
    X(bool, Discord_RelationshipHandle_User, (Discord_RelationshipHandle*, Discord_UserHandle*))                      \
    /* user handle accessors */                                                                                      \
    X(void, Discord_UserHandle_Drop, (Discord_UserHandle*))                                                          \
    X(uint64_t, Discord_UserHandle_Id, (Discord_UserHandle*))                                                        \
    X(void, Discord_UserHandle_Username, (Discord_UserHandle*, Discord_String*))                                     \
    X(void, Discord_UserHandle_DisplayName, (Discord_UserHandle*, Discord_String*))                                  \
    X(bool, Discord_UserHandle_Avatar, (Discord_UserHandle*, Discord_String*))                                       \
    X(Discord_StatusType, Discord_UserHandle_Status, (Discord_UserHandle*))

struct DiscordSocialCapi
{
    bool m_Loaded;

#define X(ret_type, fn_name, fn_params) ret_type (*fn_name) fn_params;
    DISCORD_SOCIAL_CAPI_FUNCTIONS(X)
#undef X
};

// Resolves every function in the table from an opened module. When any symbol
// is missing, the whole table is zeroed and false is returned; a partially
// bound table would produce inconsistent runtime behavior.
// get_symbol returns a void* for a name (or 0) and log_missing explains a miss.
bool DiscordSocialCapi_Resolve(DiscordSocialCapi* table, void* module,
                               void* (*get_symbol)(void* module, const char* name),
                               void (*log_missing)(const char* name, const char* reason));

// Fills `out` with Destring copies and frees the SDK-owned payload.
std::string DiscordSocialCapi_ReadString(const DiscordSocialCapi& capi, const Discord_String& s);

// Reads string getters ending at a `Discord_String* returnValue` parameter and
// free afterwards, returning "" when unset/empty.
std::string DiscordSocialCapi_ReadGetter(const DiscordSocialCapi& capi, Discord_String* s);

// Converts returned properties into a map and releases the payload.
void DiscordSocialCapi_ReadProperties(const DiscordSocialCapi& capi, const Discord_Properties& props,
                                      std::map<std::string, std::string>* out);

// Allocates a Discord_Properties payload from a map. The caller must free the
// result with Discord_FreeProperties (either immediately after passing the
// value on, or via the same helper used by the cleanup path).
Discord_Properties DiscordSocialCapi_MakeProperties(const DiscordSocialCapi& capi,
                                                    const std::map<std::string, std::string>& map);

#endif // DISCORD_SOCIAL_CAPI_H
