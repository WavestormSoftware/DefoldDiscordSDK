// Copyright 2023-2026
// DefoldDiscordSDK Defold extension
//
// Rich presence state: builds a Discord_Activity from the canonical Lua
// table, enforces the update rate limit (5 updates / 20 seconds) and queues
// the newest update when the window is full.

#ifndef DISCORD_SOCIAL_PRESENCE_H
#define DISCORD_SOCIAL_PRESENCE_H

#include <dmsdk/sdk.h>

struct DiscordSocialCapi;
struct Discord_Client;

namespace discordsdk_presence {

// Binds the module to the runtime symbol table and the live client. Call with
// (0, 0) on shutdown. Paired with the client lifetime.
void SetTarget(const DiscordSocialCapi* capi, Discord_Client* client);

// Builds an Activity from the Lua table at `index` and either dispatches it
// immediately or stores it as the pending update (rate limited). Returns true
// when the update was accepted (dispatched or queued), false when the SDK
// target is unavailable. Programmer misuse inside the table raises Lua errors.
bool Update(lua_State* L, int index);

// Cancels any pending update and dispatches the SDK clear call immediately.
// Clearing is deliberately not rate limited.
void Clear();

// Called every frame from OnUpdate: dispatches the pending update as soon as
// the rate window has room again.
void Pump();

} // namespace discordsdk_presence

#endif // DISCORD_SOCIAL_PRESENCE_H
