# defolddiscordsdk source tree

Native Defold extension for the Discord Social SDK
(discord_partner_sdk). Engine-facing behavior and the Lua module
`discordsdk` are specified in ../wiki/API-Reference.md; this file
describes the sources and how to extend them.

## Build

Standard Defold native extension: the folder ships to Defold's extension
build service as-is. Nothing links the SDK; `discord_partner_sdk` (and
optionally `discord_krisp`) are runtime-loaded through dlsym/GetProcAddress
and the extension degrades to stub mode when they are missing. C++17,
exceptions-free. `myextension/` from the template was removed; the extension
manifest is ext.manifest with name DefoldDiscordSDK, matching
DM_DECLARE_EXTENSION in src/extension.cpp.

## Source map (src/)

| File | Responsibility |
| --- | --- |
| extension.cpp | Lua glue: module table, constants, initialize/opts merge, stub-mode guards, DM_DECLARE_EXTENSION, OnUpdate pump (presence Pump + Discord_RunCallbacks). |
| discord_client.h/.cpp | Owner of the single Discord_Client; SDK callbacks converted into snapshot structs and events. OAuth/token flow, invites and join requests, lobbies and messages, voice. |
| discord_presence.h/.cpp | update_presence table -> Discord_Activity, 5-per-20s rate limiter with pending queue, clear. |
| discord_events.h/.cpp | Event registry: 25 kinds (status_changed ... presence_result), name table, ref-stable callbacks, pcall-guarded dispatch. |
| discord_launch.h/.cpp | Command line parsing for `--discord-join` / `--discord-spectate` launch arguments (get_launch_args). |
| discord_loader.h/.cpp | Runtime search and dlsym loading of the partner sdk; krisp preload when staged. |
| discordsdk_capi.h/.cpp | Typed function pointer table X-macro list; runtime resolve loop; compile-time signature check against include/cdiscourd.h (static_assert block). |
| discord_util.h | Path/id parse/format helpers, no SDK dependency. |

include/ carries the vendored C header (cdiscourd.h) that every signature is
copied from verbatim; never invent SDK signatures there.

res/ holds the runtime binaries dropped per target (see res/README.md).

## Adding a C binding

1. Copy the declaration verbatim from include/cdiscourd.h into the X-macro
   list in src/discordsdk_capi.h; the pointer, resolve loop and
   static_assert signature check are generated from the row.
2. Read payloads through DiscordSocialCapi_ReadString/_ReadGetter
   (both release the SDK-owned buffer) or spans + per-member Drop + one
   Discord_Free for the span buffer, matching the ownership notes at the top
   of discord_client.cpp.
3. Never leak handles: every *Handle handed over by a callback or fetch is
   Drop()'ed by the reader.

## Design notes / this SDK build

- Join requests are delivered as ActivityInvite with type JoinRequest (=5)
  through SetActivityInviteCreatedCallback; they surface as the extra
  `activity_join_request` event. SendActivityJoinRequestReply has no reply
  enum in this build: REPLY_YES issues the call, REPLY_NO/REPLY_IGNORE are
  local no-ops with a log line (no decline/ignore SDK entry point exists).
- INVITE_SPECTATE has no SDK backing either (SendActivityInvite is
  join-only); the constant remains for surface compatibility and to keep
  wiki payloads stable.
- Timestamps: the SDK speaks milliseconds since epoch; the Lua surface gets
  UNIX seconds, converted once at the push boundary in discord_client.cpp.
- ActivityActionTypes supports only Join(1)/JoinRequest(5); join routing
  controls come through supported_platforms, where the wiki PLATFORM_*
  mask is mapped onto the SDK ActivityGamePlatforms bit set inside
  discord_presence.cpp.
- Auth order is explicit: initialize() builds and configures the client
  (no connection), authorize()/set_token() end in the `authorized` event
  and connect() is a separate step, which is safe once UpdateToken has
  completed.
