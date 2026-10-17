// Copyright (c) 2026 Wavestorm Software
// DefoldDiscordSDK Defold extension
//
// Lua callback registry for SDK events.
//
// Events fire synchronously inside Discord_RunCallbacks on the main thread
// (the extension never enables Discord_SetFreeThreaded), so callbacks can be
// dispatched straight from the SDK bridge without queues. Registered Lua
// functions are pinned with dmScript::Ref in the registry, together with the
// script instance (self) at registration time; both are released with
// dmScript::Unref when replaced or at FinalizeExtension.

#ifndef DISCORD_SOCIAL_EVENTS_H
#define DISCORD_SOCIAL_EVENTS_H

#include <stdint.h>

#include <dmsdk/sdk.h>

// Event names. The list is the canonical Lua API surface; adding a kind
// requires updating the name table in discord_events.cpp and the registry
// wiring in discord_client.cpp.
enum DiscordSocialEventKind
{
    DISCORD_SOCIAL_EVENT_STATUS_CHANGED = 0,
    DISCORD_SOCIAL_EVENT_READY,
    DISCORD_SOCIAL_EVENT_ERROR,
    DISCORD_SOCIAL_EVENT_AUTHORIZED,
    DISCORD_SOCIAL_EVENT_TOKEN_EXPIRED,
    DISCORD_SOCIAL_EVENT_USER_UPDATED,
    DISCORD_SOCIAL_EVENT_ACTIVITY_INVITE_CREATED,
    DISCORD_SOCIAL_EVENT_ACTIVITY_INVITE_UPDATED,
    DISCORD_SOCIAL_EVENT_ACTIVITY_JOIN,
    // Fires in addition to activity_invite_created for invites whose action
    // type is Discord_ActivityActionTypes_JoinRequest (5). The SDK has no
    // dedicated "ask to join" callback: an incoming join request is delivered
    // through the invite-created callback as an ActivityInvite of the
    // JoinRequest kind (discordsdk_capi.cpp read of cdiscourd
    // ActivityActionTypes). See discord_client.cpp OnActivityInviteCreated.
    DISCORD_SOCIAL_EVENT_ACTIVITY_JOIN_REQUEST,
    DISCORD_SOCIAL_EVENT_RELATIONSHIP_CREATED,
    DISCORD_SOCIAL_EVENT_RELATIONSHIP_DELETED,
    DISCORD_SOCIAL_EVENT_RELATIONSHIP_GROUPS_UPDATED,
    DISCORD_SOCIAL_EVENT_LOBBY_CREATED,
    DISCORD_SOCIAL_EVENT_LOBBY_UPDATED,
    DISCORD_SOCIAL_EVENT_LOBBY_DELETED,
    DISCORD_SOCIAL_EVENT_LOBBY_MEMBER_ADDED,
    DISCORD_SOCIAL_EVENT_LOBBY_MEMBER_REMOVED,
    DISCORD_SOCIAL_EVENT_LOBBY_MEMBER_UPDATED,
    DISCORD_SOCIAL_EVENT_MESSAGE_CREATED,
    DISCORD_SOCIAL_EVENT_MESSAGE_UPDATED,
    DISCORD_SOCIAL_EVENT_MESSAGE_DELETED,
    DISCORD_SOCIAL_EVENT_VOICE_PARTICIPANT_CHANGED,
    DISCORD_SOCIAL_EVENT_VOICE_SETTINGS_UPDATED,
    DISCORD_SOCIAL_EVENT_PRESENCE_RESULT,
    DISCORD_SOCIAL_EVENT_KIND_COUNT
};

// Resolves a name ("status_changed", ...) to an event kind. Returns false for
// unknown names (caller surfaces a clear luaL_error).
bool DiscordSocialEvents_KindFromName(const char* name, int* out_kind);

// The canonical name for a kind, as used by set_callback and error reporting.
const char* DiscordSocialEvents_KindName(int kind);

// Registers the script state. Call once from LuaInit.
void DiscordSocialEvents_Init(lua_State* L);

// Stable self-registration: lua_set_callback(table, idx) semantics live in the
// extension glue; this helper replaces (or clears, on nil) the handler for a
// kind. `index` must hold a function or nil.
void DiscordSocialEvents_SetHandler(lua_State* L, int kind, int index);

// Removes all handler references (called from FinalizeExtension before the
// Lua state goes away).
void DiscordSocialEvents_ClearHandlers();

// True when any handler is registered for the kind.
bool DiscordSocialEvents_HasHandler(int kind);

// Pushes the event payload onto the Lua stack. Implementations must push
// exactly `arg_count` values (they receive the context pointer allocated by
// the dispatch site).
typedef void (*DiscordSocialEventPush)(lua_State* L, void* context, int* out_arg_count);

// Fires a registered handler. Returns true when a handler ran. The payload
// is produced by `push`; handler errors are pcall-guarded and logged.
// The context pointer must match what `push` expects.
bool DiscordSocialEvents_Fire(int kind, DiscordSocialEventPush push, void* context);

#endif // DISCORD_SOCIAL_EVENTS_H
