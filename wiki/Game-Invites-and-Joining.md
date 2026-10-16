# Game Invites and Joining

Game invites let players pull friends into an exact session: the same match, lobby, or world. This page documents the full lifecycle: sending programmatically, receiving and accepting, ask to join, and deep links that start a closed game and hand it the join secret.

Invites are powered entirely by rich presence. Without an active presence carrying party data, a join secret, and supported platforms, nothing invite related works. Presence is the prerequisite; the `communication` scope preset is only needed for the text inside the invite message. See [Rich-Presence.md](Rich-Presence.md) first if presence is not configured yet.

## Prerequisite presence

Discord turns the following into an invitable activity:

```lua
local discordsdk = require "discordsdk"

discordsdk.update_presence({
    details = "Defending the gate",
    state = "In a group",
    party = {
        id = "1204-party",
        size = 1,
        max = 4,
    },
    secrets = {
        join = "gate-9182",             -- your session token, format is yours
    },
    supported_platforms = discordsdk.PLATFORM_DESKTOP,
    assets = { invite_cover_image = "gate-cover" },  -- banner on the invite card
})
```

Missing any of `party.id`, `party.size` with `party.max`, `secrets.join`, or a non-empty `supported_platforms` silently disables joining; the presence still displays. The full checklist is in [Troubleshooting.md](Troubleshooting.md).

## Registering how the game launches

Before invites arrive at a closed game, Discord needs to know how to start it. Registration is local to the machine, so call it every run:

```lua
discordsdk.register_launch_command("mygame://")  -- protocol launch value
-- or, for Steam builds:
discordsdk.register_steam("1204")                -- Steam application id
```

`auto_register_launch = 1` in `[defolddiscordsdk]` runs the standard registration during `initialize()`, so most games need nothing here. The custom functions are for games with a dedicated protocol or for toggling behavior at run time.

## Sending invites

Two paths exist:

1. **In the Discord client.** Users right-click a friend or use the context menu on the presence; Discord builds the card from the presence data above. Nothing for the game to do.
2. **From the game.** The extension matches the SDK's direct invite:

```lua
local sent = discordsdk.send_invite(friend_id, discordsdk.INVITE_JOIN, "Gate is open, squad full in 4")
-- spectate variant:
discordsdk.send_invite(friend_id, discordsdk.INVITE_SPECTATE, nil)
```

`send_invite` requires that active presence configuration, otherwise it fails with a reported `error`. The message text is optional; presence-only scope games send invites with empty text and the card still works.

When the presence is not yet visible to others, in particular with buttons and join affordances, use a second account or a second client; the sender never sees their own invite card.

## Receiving invites

```lua
discordsdk.set_callback("activity_invite_created", function(payload)
    -- payload = { type, sender_id, message_id, message, activity }
    -- payload.type is discordsdk.INVITE_JOIN, INVITE_SPECTATE, or the join request kind
    -- payload.activity echoes the presence summary of the session being joined
end)
```

Shows the invite in your own UI with the sender's data; fetch the portrait through `get_friend(sender_id)`. Declined invites expire by themselves; long-lived invitational UI may listen to `activity_invite_updated` as well.

## Accepting

```lua
discordsdk.accept_invite(sender_id)
```

The call submits the accept; the confirmed flow arrives through `activity_join`:

```lua
discordsdk.set_callback("activity_join", function(payload)
    -- payload.secret is the host's join secret; connect to that session here
    game.connect_to(payload.secret)
end)
```

The same `activity_join` event fires when someone clicks the join button on a presence in the Discord client, so one handler covers both programmatic acceptance and client-driven joining.

## Ask to join

When the party is full or entry is owner approved, players can request instead of being invited:

1. The requester calls `discordsdk.send_join_request(target_id)`.
2. The target receives `activity_join_request`, payload `{ sender_id = "..." }`.
3. The target answers once:

```lua
discordsdk.set_callback("activity_join_request", function(payload)
    -- present your own prompt, then:
    discordsdk.send_join_request_reply(payload.sender_id, discordsdk.REPLY_YES)
    -- or discordsdk.REPLY_NO, or discordsdk.REPLY_IGNORE to dismiss quietly
end)
```

4. A granted request delivers the join secret to the requester through the invite path, and acceptance continues as above.

## Deep links from a closed game

When Discord starts the game for a player who clicked an invite, the secret travels inside the launch:

```lua
local args = discordsdk.get_launch_args()
if args and args.join then
    game.connect_to(args.join)
elseif args and args.spectate then
    game.connect_to(args.spectate, true)
end
```

Call `get_launch_args()` once early; it returns `{ join, spectate }` with both fields `nil` for normal launches.

Note on application scoped joins: the SDK also carries an `activity_join_with_application` entry point in its C surface for activities tied to a different application id. The extension routes the standard case, games join by their own application, and its `activity_join` event with the same secret semantics; custom application joins appear the same way to Lua code.

## Invites into lobbies

The standard multiplayer loop combines this page with [Lobbies.md](Lobbies.md):

1. Host: `discordsdk.lobby_join_or_create(session_secret)`, then publishes presence whose `secrets.join` carries that same secret.
2. Guest: receives the secret through an accepted invite or a join click, then calls `discordsdk.lobby_join_or_create(secret)` to land in the same lobby.
3. Host republishes presence with the new `party.size`; the loop repeats on each membership change.

## See also

- [Rich-Presence.md](Rich-Presence.md): the payload that carries all invite data.
- [Lobbies.md](Lobbies.md): the target session model most games join into.
- [API-Reference.md](API-Reference.md): exact signatures and payloads.
- [Configuration.md](Configuration.md): `auto_register_launch` and scope choices.
