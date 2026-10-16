# Lobbies

Lobbies are client-side groups of players that share text chat, metadata, and optionally voice. Discord hosts them, so chat and membership work even when one of the players has no game connection for a while. A user can be in several lobbies at once; a lobby can hold up to one thousand members, and each user may hold up to one hundred lobbies per game.

The full social layer is required: initialize with `scopes = communication` or pass `discordsdk.scopes_communication` and have the player authorize. See [OAuth-and-Tokens.md](OAuth-and-Tokens.md).

## Create or join with one call

Lobbies are identified by a secret your game invents. `lobby_join_or_create` is atomic: the first player creates the lobby for that secret, everyone else joins it.

```lua
local discordsdk = require "discordsdk"

-- host derives a secret for this match
local session_secret = my_network.get_session_token()

discordsdk.lobby_join_or_create(session_secret, {
    mode = "survival",
    map = "floe",
    region = "eu",
})

discordsdk.set_callback("lobby_created", function(payload)
    my_session.bind(payload.lobby_id) -- payload.lobby_id for chat and voice calls
end)
```

| Rule | Detail |
| --- | --- |
| Secrets are per application | The same secret string in another game is a different lobby |
| Rejoining is idempotent | Leaving and re-joining with the same secret lands back in the same lobby |
| Calling while a member is fine | When the user is already in that lobby the call only updates their metadata |
| Metadata is a JSON blob | String keys and values carried with the lobby and each member |

## Membership lifecycle

- The lobby id arrives through `lobby_created`.
- Own and other member changes arrive through `lobby_member_added`, `lobby_member_removed`, and `lobby_member_updated`, all carrying `lobby_id` and `user_id`.
- `discordsdk.lobby_leave(lobby_id)` removes the user; removal is delivered to every member.
- `discordsdk.get_lobbies()` lists the lobbies the user is currently in.

Lobby membership survives a game crash or a lost network: Discord keeps the player in the lobby until the game explicitly calls `lobby_leave()` or the lobby idles out. Drop `lobby_leave()` into your shutdown path when players must not linger.

Lobby expiry: lobbies are ephemeral. When nobody is connected, the lobby becomes idle and Discord deletes it after an idle timeout, five minutes by default, with a long tail up to about a week for world-chat style lobbies. Any connected member resets the timer.

Client-created lobbies have one hard limit: the join secret expires thirty days after creation, so long-lived worlds should rotate secrets and avoid inviting through stale ones. See [Troubleshooting.md](Troubleshooting.md).

## Text chat

```lua
discordsdk.lobby_send_message(lobby_id, "Squad regroup at the gate")
```

Delivery, including the sender's own copy, arrives through the message events:

```lua
discordsdk.set_callback("message_created", function(msg)
    -- msg = { message_id, lobby_id, user_id, content, timestamp }
    chat_view.append(msg.content, msg.user_id, msg.timestamp)
end)
```

`message_updated` and `message_deleted` carry the same shape for edits and removals.

Because each lobby is backed by a Discord-side chat location, the SDK resolves it for you and code only deals in lobby ids; there is no channel type to manage in Lua. When a lobby is linked to a Discord text channel in a server through the SDK's linked channels feature, messages flow in through `message_created` as well, so the same chat view renders both. The first time a user writes into such a linked lobby, the SDK injects one private disclosure message about visibility in Discord; it is marked by the SDK's disclosure type and is private to the sender, so your chat UI can translate it, style it, or skip it.

### History

```lua
local recent = discordsdk.lobby_get_messages(lobby_id, 50) -- oldest to newest, call on join
```

History:

- Returns up to two hundred messages, and only the last seventy-two hours.
- Ordered oldest to newest; render top to bottom accordingly.
- Only available for lobbies the user currently belongs to.

Historical fetch on join gives players context before they speak. Cache what you render; the fetch is not a free replay of an entire season.

## Voice

Every lobby can have a live voice call. Voice and text are independent: a lobby works fine without voice, and chat keeps flowing during a call.

```lua
discordsdk.start_voice(lobby_id)
-- participants change through voice_participant_changed
discordsdk.stop_voice()
```

Best practice: keep voice calls at around twenty-five participants or fewer. Large membership numbers are what the text layer is for; they are not an invitation to start a megaphone call. Audio settings, devices, and krisp noise cancellation are documented in [Voice.md](Voice.md).

## Rate limits

While the application has no increased production limits, Discord enforces application-wide development caps, not per player:

| Operation | Development limit |
| --- | --- |
| `lobby_join_or_create`, that is the SDK create-or-join | 100 per two hours |
| Send lobby message | 100 per two hours |
| Linked channel connect and disconnect flows | 20 per two hours each |

Exceeding them makes requests fail with reported errors until the window rolls over. For testing, this is a real cap: a compact suite of lobby teardown and rebuild can eat the whole window. Production releases apply for higher limits through the communication features process documented by Discord; until then keep lobby rotations lean. See [Testing-Guide.md](Testing-Guide.md) for how this shapes the test matrix.

## Server-side alternative

Lobbies can also be created, managed, and deleted from your servers through the Discord HTTP API. Server-created lobbies are not joinable with the client's create-or-join flow; pick one model per lobby type. This extension covers the client-side flow.

## A minimal lobby suite

```lua
local discordsdk = require "discordsdk"

local lobby = {}

function lobby.open(secret, metadata)
    discordsdk.lobby_join_or_create(secret, metadata)
end

function lobby.bind(id)
    lobby.id = id
end

function lobby.say(text)
    discordsdk.lobby_send_message(lobby.id, text)
end

function lobby.close()
    discordsdk.stop_voice()
    discordsdk.lobby_leave(lobby.id)
    lobby.id = nil
end

return lobby
```

## See also

- [Voice.md](Voice.md): starting calls inside lobbies.
- [Game-Invites-and-Joining.md](Game-Invites-and-Joining.md): making lobbies reach players through invites.
- [API-Reference.md](API-Reference.md): signatures and events.
