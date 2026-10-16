// Copyright 2023-2026
// DefoldDiscordSDK Defold extension
//
// See discord_events.h for the contract.

#include "discord_events.h"

#include <string.h>

#include <string>

namespace {

struct DiscordSocialCallbackInfo
{
    lua_State* m_L;
    int m_Callback;
    int m_Self;
};

DiscordSocialCallbackInfo g_Callbacks[DISCORD_SOCIAL_EVENT_KIND_COUNT];
lua_State* g_ScriptState = 0;

const char* g_EventNames[DISCORD_SOCIAL_EVENT_KIND_COUNT] = {
    "status_changed",
    "ready",
    "error",
    "authorized",
    "token_expired",
    "user_updated",
    "activity_invite_created",
    "activity_invite_updated",
    "activity_join",
    // Sent for invite-created events whose join action type is JoinRequest;
    // see discord_events.h for why the two events coexist.
    "activity_join_request",
    "relationship_created",
    "relationship_deleted",
    "relationship_groups_updated",
    "lobby_created",
    "lobby_updated",
    "lobby_deleted",
    "lobby_member_added",
    "lobby_member_removed",
    "lobby_member_updated",
    "message_created",
    "message_updated",
    "message_deleted",
    "voice_participant_changed",
    "voice_settings_updated",
    "presence_result",
};

void ClearCallback(DiscordSocialCallbackInfo* info)
{
    if (info->m_Callback != LUA_NOREF && info->m_L) {
        dmScript::Unref(info->m_L, LUA_REGISTRYINDEX, info->m_Callback);
        dmScript::Unref(info->m_L, LUA_REGISTRYINDEX, info->m_Self);
    }
    info->m_Callback = LUA_NOREF;
    info->m_Self = LUA_NOREF;
    info->m_L = 0;
}

} // unnamed namespace

bool DiscordSocialEvents_KindFromName(const char* name, int* out_kind)
{
    if (!name) {
        return false;
    }
    for (int i = 0; i < DISCORD_SOCIAL_EVENT_KIND_COUNT; ++i) {
        if (strcmp(name, g_EventNames[i]) == 0) {
            if (out_kind) {
                *out_kind = i;
            }
            return true;
        }
    }
    return false;
}

const char* DiscordSocialEvents_KindName(int kind)
{
    if (kind < 0 || kind >= DISCORD_SOCIAL_EVENT_KIND_COUNT) {
        return "unknown";
    }
    return g_EventNames[kind];
}

void DiscordSocialEvents_Init(lua_State* L)
{
    g_ScriptState = L;
    for (int i = 0; i < DISCORD_SOCIAL_EVENT_KIND_COUNT; ++i) {
        ClearCallback(&g_Callbacks[i]);
    }
}

void DiscordSocialEvents_SetHandler(lua_State* L, int kind, int index)
{
    if (kind < 0 || kind >= DISCORD_SOCIAL_EVENT_KIND_COUNT) {
        return;
    }
    DiscordSocialCallbackInfo* callback_info = &g_Callbacks[kind];
    ClearCallback(callback_info);

    if (lua_isnil(L, index)) {
        return;
    }
    luaL_checktype(L, index, LUA_TFUNCTION);

    callback_info->m_L = dmScript::GetMainThread(L);
    if (!callback_info->m_L) {
        callback_info->m_L = L;
    }
    lua_pushvalue(L, index);
    callback_info->m_Callback = dmScript::Ref(callback_info->m_L, LUA_REGISTRYINDEX);

    // Pin the owning script instance so callbacks fire with the right self
    // even when triggered from a different script context.
    dmScript::GetInstance(L);
    callback_info->m_Self = dmScript::Ref(callback_info->m_L, LUA_REGISTRYINDEX);
}

void DiscordSocialEvents_ClearHandlers()
{
    for (int i = 0; i < DISCORD_SOCIAL_EVENT_KIND_COUNT; ++i) {
        ClearCallback(&g_Callbacks[i]);
    }
    g_ScriptState = 0;
}

bool DiscordSocialEvents_HasHandler(int kind)
{
    if (kind < 0 || kind >= DISCORD_SOCIAL_EVENT_KIND_COUNT) {
        return false;
    }
    return g_Callbacks[kind].m_Callback != LUA_NOREF && g_Callbacks[kind].m_L != 0;
}

bool DiscordSocialEvents_Fire(int kind, DiscordSocialEventPush push, void* context)
{
    if (kind < 0 || kind >= DISCORD_SOCIAL_EVENT_KIND_COUNT) {
        return false;
    }
    DiscordSocialCallbackInfo* handler = &g_Callbacks[kind];
    if (!push || handler->m_Callback == LUA_NOREF || !handler->m_L) {
        return false;
    }

    lua_State* L = handler->m_L;
    int top = lua_gettop(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, handler->m_Callback);

    // Restore the script instance that owned the registration.
    lua_rawgeti(L, LUA_REGISTRYINDEX, handler->m_Self);
    dmScript::SetInstance(L);

    int arg_count = 0;
    push(L, context, &arg_count);

    int result = lua_pcall(L, arg_count, 0, 0);
    if (result != 0) {
        const char* error = lua_tostring(L, -1);
        dmLogError("DefoldDiscordSDK: error in %s event handler: %s", g_EventNames[kind],
                   error ? error : "unknown error");
        lua_pop(L, 1);
    }

    assert(top == lua_gettop(L));
    return true;
}
