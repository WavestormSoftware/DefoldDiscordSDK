# API Reference

The complete surface of the `discordsdk` module. Every function, constant, event, and payload table in one place. The implementation of each area may add pages with context; this page is the contract.

```lua
local discordsdk = require "discordsdk"
```

## Contracts

- **Graceful degradation.** When the SDK library is unavailable, or `enabled = false`, every network call returns `false`, every getter returns `nil` or an empty table, and setters return `false`. The game keeps running. One log line explains the state; there are no hard errors.
- **Ids are strings.** Discord ids are uint64 snowflakes and would lose precision as Lua numbers, so user ids, lobby ids, message ids, and the application id are passed and returned as strings, for example `"1349146942634065960"`.
- **Asynchronous model.** Calls that reach Discord return `true` when the request was accepted into the extension's send path. Results arrive through events. Presence additionally reports per update through `presence_result`.
- **Payload naming.** All payload tables use snake_case keys.
- **Thread safety.** All calls are made from the Defold main script context; callbacks arrive on the same context as normal Defold callbacks.

## Constants

### Scope presets

```lua
discordsdk.scopes_presence       = { "openid", "sdk.social_layer_presence" }
discordsdk.scopes_communication  = { "openid", "sdk.social_layer" }
```

Tables of OAuth scope strings used by `initialize()` and `authorize()`. `communication` is a superset of `presence` and unlocks messaging, lobbies, and voice. `scopes` in `initialize()` also accepts a custom array of scope strings.

### Activity types

| Constant | Value | Meaning |
| --- | --- | --- |
| `discordsdk.TYPE_PLAYING` | 0 | Playing a game; the standard choice |
| `discordsdk.TYPE_STREAMING` | 1 | Streaming to an external provider |
| `discordsdk.TYPE_LISTENING` | 2 | Listening to audio |
| `discordsdk.TYPE_WATCHING` | 3 | Watching a stream or video |
| `discordsdk.TYPE_CUSTOM` | 4 | Custom activity text |
| `discordsdk.TYPE_COMPETING` | 5 | Competing in a match |

Discord routes streaming and custom types only for specific flows; rich presence integrations should use `TYPE_PLAYING` unless deliberately exercising the others.

### Status display types

Which field of the activity is shown as the user's status text in the member list.

| Constant | Value | Shown field |
| --- | --- | --- |
| `discordsdk.DISPLAY_NAME` | 0 | The game name, default |
| `discordsdk.DISPLAY_STATE` | 1 | The `state` field |
| `discordsdk.DISPLAY_DETAILS` | 2 | The `details` field |

### Supported platform mask

Bitmask passed through `update_presence` as `supported_platforms`; it controls where Discord offers join actions. Members combine with a bitwise OR (`bit.bor`).

| Constant | Value | Meaning |
| --- | --- | --- |
| `discordsdk.PLATFORM_DESKTOP` | 1 | Any desktop target; the common value for desktop games |
| `discordsdk.PLATFORM_WINDOWS` | 2 | Windows specifically |
| `discordsdk.PLATFORM_MACOS` | 4 | macOS specifically |
| `discordsdk.PLATFORM_LINUX` | 8 | Linux specifically |
| `discordsdk.PLATFORM_ANDROID` | 16 | Android deep links |
| `discordsdk.PLATFORM_IOS` | 32 | iOS deep links, not usable for presence-only RPC |

The mask values shown here are the extension's binding of the SDK's platform flag set; when in doubt print the constants from a running game. Deep-link routing for the mobile members follows the SDK's own platform constants.

### Invite and reply types

| Constant | Value | Meaning |
| --- | --- | --- |
| `discordsdk.INVITE_JOIN` | 1 | Invite the user to join the session |
| `discordsdk.INVITE_SPECTATE` | 2 | Invite the user to spectate the session |
| `discordsdk.REPLY_NO` | 0 | Decline a join request |
| `discordsdk.REPLY_YES` | 1 | Accept a join request |
| `discordsdk.REPLY_IGNORE` | 2 | Dismiss a join request without an answer |

Numeric values are the extension's passthrough of the SDK's reply and action enums; `INVITE_JOIN` with `send_invite`, `REPLY_*` with `send_join_request_reply`.

### Online status strings

Strings, used with `set_online_status()` and returned in user objects.

```lua
discordsdk.STATUS_ONLINE      = "online"
discordsdk.STATUS_DND         = "dnd"
discordsdk.STATUS_IDLE        = "idle"
discordsdk.STATUS_INVISIBLE   = "invisible"
discordsdk.STATUS_OFFLINE     = "offline"
```

### Version strings

```lua
discordsdk.VERSION            = "1.0.0"  -- extension version, for example
discordsdk.SDK_VERSION        = "x.y.z"  -- Discord Social SDK version discovered at load time
```

`SDK_VERSION` is `nil` when the library is missing. `version()` builds one readable line from both.

## Object shapes

Payload tables are regular Lua tables; missing fields are `nil`.

### user

| Field | Type | Notes |
| --- | --- | --- |
| `id` | string | Snowflake id |
| `username` | string | Unique handle |
| `display_name` | string | Name shown in Discord UI |
| `discriminator` | string | `"0"` for most accounts now |
| `avatar` | string or nil | Avatar hash; build the CDN URL as documented on the friends page |
| `is_provisional` | boolean | True for in-game Game Friends users without a linked Discord profile |
| `online_status` | string | One of `STATUS_*` |
| `in_game` | boolean | True when the user's activity points at your application |

### relationship

| Field | Type | Notes |
| --- | --- | --- |
| `user` | user | Target user |
| `discord_type` | string | `"none"`, `"friend"`, `"pending_incoming"`, `"pending_outgoing"`, `"blocked"` |
| `game_type` | string | Same value set for the in-game relationship side |

### lobby

| Field | Type | Notes |
| --- | --- | --- |
| `lobby_id` | string | Snowflake id |
| `secret` | string | The create-or-join secret |
| `metadata` | table | String keys and values set at creation or update |

### message

| Field | Type | Notes |
| --- | --- | --- |
| `message_id` | string | Snowflake id |
| `lobby_id` | string or nil | Set for lobby messages |
| `user_id` | string | Author |
| `content` | string | Text |
| `timestamp` | number | Unix seconds |

### activity (invite payload activity)

| Field | Type | Notes |
| --- | --- | --- |
| `name` | string | Game or activity name |
| `details` | string or nil | As set in presence |
| `state` | string or nil | As set in presence |

## Functions

### Lifecycle

#### discordsdk.initialize(opts)

```lua
local ok = discordsdk.initialize({
    application_id = "1349146942634065960",
    scopes = discordsdk.scopes_communication,
    auto_register_launch = true,
    steam_app_id = "480",
    default_redirect = "https://mygame.example/oauth",
    enabled = true,
})
```

| Argument | Type | Default |
| --- | --- | --- |
| `opts` | table with the fields above | all fall back to `[defolddiscordsdk]` in game.project |

Returns `boolean`, `true` when the SDK is available and startup started. Call once, before everything else. Events after initialization: `ready`, `status_changed`, `error`.

#### discordsdk.connect()

`boolean`. Establishes the SDK's online connection after authorization when manual control is desired; normally unnecessary because the connection follows `authorize()` automatically. Safe to call when already connected.

#### discordsdk.disconnect()

`boolean`. Drops the online connection. Presence mode keeps working through the local client; social calls go dormant until a new `connect()` or `authorize()`.

#### discordsdk.get_status()

`string`. The current connection status, the same strings `status_changed` delivers, for example `"connected"` or `"disconnected"`. Empty string when unknown.

#### discordsdk.is_ready()

`boolean`. True after the `ready` event, the point at which the friends list and the rest of the surface are usable.

#### discordsdk.get_current_user()

`user` or `nil`. The linked user after `authorized`; in presence mode `nil`.

#### discordsdk.version()

`string`. Human readable version line containing `VERSION` and `SDK_VERSION`.

### Callbacks

#### discordsdk.set_callback(name, fn_or_nil)

```lua
discordsdk.set_callback("activity_join", function(payload)
    print(payload.secret)
end)
```

`name` is one of the event names in the Events section. Pass a function to install it and `nil` to remove it. Returns `boolean` whether the event name is known. Events arrive in a single call, on the main script context.

### Presence

#### discordsdk.update_presence(activity)

`boolean` accepted into the queue. Full field list in [Rich-Presence.md](Rich-Presence.md). Queued at most five updates per twenty seconds, deduplicated against the last payload. Results arrive one per acknowledged update through `presence_result`.

#### discordsdk.clear_presence()

`boolean`. Removes the activity. A confirmed clear does not emit a `presence_result`.

### Invites and joining

#### discordsdk.send_invite(user_id, invite_type, message)

```lua
discordsdk.send_invite(friend_id, discordsdk.INVITE_JOIN, "Join the raid")
```

| Argument | Type | Notes |
| --- | --- | --- |
| `user_id` | string | Recipient |
| `invite_type` | number | `discordsdk.INVITE_JOIN` or `discordsdk.INVITE_SPECTATE` |
| `message` | string or nil | Optional card message; requires communication scopes, otherwise sent empty |

Returns `boolean`. Requires an active presence with party, join secret, and supported platforms. Incoming invites arrive as `activity_invite_created`.

#### discordsdk.accept_invite(user_id)

`boolean` when the accept request was submitted. Confirmation and the join secret arrive through `activity_join`, the same event the Discord client's own Join button produces.

#### discordsdk.send_join_request(user_id)

`boolean`. Asks to join the user's party; the target receives a join request and answers with `send_join_request_reply`. A granted request arrives as an invite of the join request kind.

#### discordsdk.send_join_request_reply(user_id, reply)

| Argument | Type | Notes |
| --- | --- | --- |
| `user_id` | string | The requester, from `activity_join_request` or `activity_invite_created` |
| `reply` | number | `discordsdk.REPLY_YES`, `discordsdk.REPLY_NO`, or `discordsdk.REPLY_IGNORE` |

Returns `boolean`.

#### discordsdk.get_launch_args()

```lua
local args = discordsdk.get_launch_args()
-- args = { join = "secret", spectate = nil }
```

`table`. The join or spectate secrets embedded when Discord started the game through a deep link. Call it early; if the game was not launched by Discord both fields are `nil`.

#### discordsdk.register_launch_command(cmd)

`boolean`. Registers a local launch value, for example a `mygame://` protocol, so Discord can start the game on this machine. Local per machine; call each run.

#### discordsdk.register_steam(steam_id)

`boolean`. Registers the Steam application id on this machine so Discord launches the game through Steam.

### Lobbies

#### discordsdk.lobby_join_or_create(secret, metadata_table)

```lua
discordsdk.lobby_join_or_create("world-7-floe", { map = "floe", mode = "survival" })
```

| Argument | Type | Notes |
| --- | --- | --- |
| `secret` | string | Unique per application; a new lobby is created when no lobby uses it |
| `metadata_table` | table or nil | String keys and values attached to the user or lobby when present |

Returns `boolean`. Joining while already a member updates metadata instead. The resulting lobby id arrives through `lobby_created`; membership changes through the lobby member events.

#### discordsdk.lobby_leave(lobby_id)

`boolean`. Leaves the requested lobby. Membership removal arrives through `lobby_member_removed`, locally and to the other members.

#### discordsdk.lobby_send_message(lobby_id, message)

`boolean`. Broadcasts text to the lobby. Delivery, including to the sender, arrives through `message_created`.

#### discordsdk.lobby_get_messages(lobby_id, limit)

`table`. Array of `message` objects requested from the lobby history, ordered oldest to newest. `limit` caps the fetch at up to two hundred messages, covering at most the last seventy-two hours, and history is available only for lobbies the user currently belongs to.

#### discordsdk.get_lobbies()

`table`. Array of `lobby` objects the user currently belongs to.

### Voice

#### discordsdk.start_voice(lobby_id)

`boolean`. Joins the lobby's voice call. Sound changes arrive through `voice_participant_changed`. Keep calls to about twenty-five participants or fewer.

#### discordsdk.stop_voice()

`boolean`. Leaves the active call when participating in one.

#### discordsdk.stop_all_voice()

`boolean`. Leaves every active call across lobbies.

Control setters all take booleans and return `boolean`; settings changes are reported through `voice_settings_updated`.

```lua
discordsdk.set_muted(true)
discordsdk.set_deaf(false)
```

| Function | Action |
| --- | --- |
| `discordsdk.set_muted(b)` | Self microphone mute |
| `discordsdk.set_deaf(b)` | Self deafened state, implies muted behavior |
| `discordsdk.set_noise_cancellation(b)` | krisp machine learning cancellation; no-op when the optional krisp assets are not staged |
| `discordsdk.set_noise_suppression(b)` | SDK built-in suppression, always available |
| `discordsdk.set_echo_cancellation(b)` | Acoustic echo cancellation |
| `discordsdk.set_agc(b)` | Automatic gain control |

| Function | Action |
| --- | --- |
| `discordsdk.set_input_volume(n)` | Microphone volume, percent 0 to 100 |
| `discordsdk.set_output_volume(n)` | Playback volume, percent 0 to 100 |

Device selection:

```lua
local inputs  = discordsdk.get_input_devices()   -- { { id = "...", name = "Headset Mic", ... } }
local outputs = discordsdk.get_output_devices()  -- { { id = "...", name = "Speakers", ... } }

discordsdk.set_input_device(inputs[1].id)
discordsdk.set_output_device(outputs[1].id)
```

Both getters return empty tables when voice is not connected; the setters return `false` for unknown ids.

### Online status

#### discordsdk.set_online_status(status_str)

`boolean`. Sets the linked user's presence status on Discord. Accepts any `STATUS_*` value:

```lua
discordsdk.set_online_status(discordsdk.STATUS_IDLE)
discordsdk.set_online_status(discordsdk.STATUS_ONLINE)
```

Strings outside the set fail and return `false`.

### OAuth and tokens

#### discordsdk.authorize()

`boolean` when the flow started. Opens the system browser with the configured scopes and completes through the `authorized` event; failures surface through `error`. Full flow in [OAuth-and-Tokens.md](OAuth-and-Tokens.md).

#### discordsdk.set_token(access_token, refresh_token)

`boolean`. Injects tokens from your own storage to restore a previous linking without a new browser round trip. Persist with `sys.save`; see the pattern on the OAuth page.

#### discordsdk.clear_token()

`boolean`. Forgets stored tokens locally; call `revoke_token()` as well to invalidate them server side.

#### discordsdk.refresh_token()

`boolean`. Exchanges the refresh token for a fresh pair; success is confirmed through `authorized`.

#### discordsdk.revoke_token()

`boolean`. Invalidates the current grant at Discord and clears the local copy.

#### discordsdk.is_authenticated()

`boolean`. True when a token pair is loaded and not known to have expired.

### Friends

#### discordsdk.get_friends()

`table`. Array of `relationship` objects for the unified list, Discord friends and Game Friends together. Blocked users are included so your UI can filter or manage them.

#### discordsdk.get_friend(user_id)

`relationship` or `nil` for a specific id.

## Events

Register with `discordsdk.set_callback(name, fn)`. Payload tables are snake_case; unknown future fields may appear, ignore extras.

| Event | Fires when | Payload |
| --- | --- | --- |
| `status_changed` | The SDK connection state changes | `{ status = "connected" }` and comparable state strings; the query counterpart is `get_status()` |
| `ready` | First initialization handshake completed; the full surface is usable | `{ }` |
| `error` | Any SDK error surfaces | `{ message = "...", code = <number or nil>, detail = <string or nil> }` |
| `authorized` | Account linking completed or tokens were restored or refreshed | `{ }` |
| `token_expired` | The access token is known to have expired; call `refresh_token()` or re-authorize | `{ }` |
| `user_updated` | A user in the relationship set changed profile or availability | `{ user_id = "..." }`, refresh through `get_friend(user_id)` |
| `activity_invite_created` | An invite for this user arrived | `invite = { type, sender_id, message_id, message, activity }` |
| `activity_invite_updated` | A previously delivered invite changed | Same table as `activity_invite_created` |
| `activity_join` | Someone accepted an invite or clicked Join on this user's presence | `{ secret = "joinsecret" }` |
| `activity_join_request` | Another player asked to join this user's party | `{ sender_id = "..." }`; answer with `send_join_request_reply` |
| `relationship_created` | A relationship appeared | `{ user_id = "...", is_discord_relationship_update = true or false }` |
| `relationship_deleted` | A relationship was removed | Same shape as `relationship_created` |
| `relationship_groups_updated` | Grouping of the unified list changed; rebuild the UI | `{ user_id = "..." }` |
| `lobby_created` | `lobby_join_or_create` landed the user in a new or existing lobby | `{ lobby_id = "..." }` |
| `lobby_updated` | Lobby metadata or state changed | `{ lobby_id = "..." }` |
| `lobby_deleted` | The lobby no longer exists | `{ lobby_id = "..." }` |
| `lobby_member_added` | A member joined | `{ lobby_id = "...", user_id = "..." }` |
| `lobby_member_removed` | A member left or was removed | `{ lobby_id = "...", user_id = "..." }` |
| `lobby_member_updated` | A member changed their metadata or state | `{ lobby_id = "...", user_id = "..." }` |
| `message_created` | A lobby text message was delivered to members, including the sender | `message` table |
| `message_updated` | A message changed | `message` table |
| `message_deleted` | A message was removed | `{ message_id = "...", lobby_id = "..." }` |
| `voice_participant_changed` | Voice participants in a call changed | `{ lobby_id = "...", users = { user, ... } }` |
| `voice_settings_updated` | Local voice settings changed | `{ muted, deaf, noise_cancellation, noise_suppression, echo_cancellation, agc }` |
| `presence_result` | One queued presence update was acknowledged by Discord | `{ ok = true or false, error = <string or nil> }` |

Notes:

- `activity_invite_created` and `activity_invite_updated` deliver invite tables containing `type` (`discordsdk.INVITE_JOIN`, `discordsdk.INVITE_SPECTATE`, or the join request kind), `sender_id`, `message_id`, the optional `message` text, and the `activity` summary of what is being joined.
- `activity_join` is the moment to transport the player: the secret identifies the session, as described in [Game-Invites-and-Joining.md](Game-Invites-and-Joining.md).
- `message_created` payloads for a first message in a lobby linked to a Discord channel can include a private disclosure entry for the sender, defined by the SDK's disclosure type; render or skip it deliberately, see [Lobbies.md](Lobbies.md).

## See also

- [Rich-Presence.md](Rich-Presence.md) for the presence payload in depth.
- [Game-Invites-and-Joining.md](Game-Invites-and-Joining.md) and [Lobbies.md](Lobbies.md) for flow narratives.
- [OAuth-and-Tokens.md](OAuth-and-Tokens.md) for the token lifecycle pattern.
- [Troubleshooting.md](Troubleshooting.md) for failure causes.
