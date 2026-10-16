# Friends and Relationships

The unified friends list blends two worlds into one list: Discord friends, and in-game Game Friends provisioned through the SDK's provisional accounts. One call, `discordsdk.get_friends()`, returns every relationship, so a game without a social system of its own gains one, and a game with one can merge both sides.

Requires presence scopes plus a linked account, see [OAuth-and-Tokens.md](OAuth-and-Tokens.md). The whole relationship set loads when the client becomes ready.

## Reading the list

```lua
local discordsdk = require "discordsdk"

discordsdk.set_callback("ready", function()
    local friends = discordsdk.get_friends()
    for _, rel in ipairs(friends) do
        local u = rel.user
        print(u.display_name, u.online_status, rel.discord_type, rel.game_type)
    end
end)
```

### relationship

| Field | Notes |
| --- | --- |
| `user` | The user table, see the shapes section in [API-Reference.md](API-Reference.md) |
| `discord_type` | `"none"`, `"friend"`, `"pending_incoming"`, `"pending_outgoing"`, `"blocked"` |
| `game_type` | The same set for the in-game relationship side |

The two type fields matter for a unified list: a player may be a Discord friend and an in-game friend at once, or only one of the two. Discord's own guidance sorts the list into three groups: friends online and playing your game, friends online elsewhere, and offline friends. `in_game` on the user and `online_status` give you the two axes.

### user

| Field | Notes |
| --- | --- |
| `id` | String snowflake |
| `username`, `display_name`, `discriminator` | Profile names; `discriminator` is `"0"` for most accounts |
| `avatar` | Avatar hash; see the URL recipe below |
| `is_provisional` | True for Game Friends who have not claimed a Discord account yet |
| `online_status` | One of the `STATUS_*` strings |
| `in_game` | True when their activity points at your application |

Provisional users make the list feel alive from day one: they are real players with progress, addable as friends, and their rows render without a Discord avatar.

## Avatars

`avatar` is a hash, not a URL. Build the CDN address as a hint rather than storing it:

```lua
local function avatar_url(user, size)
    if not user.avatar then return nil end
    local ext = "webp"
    -- .webp renders everywhere; .gif animates only when the hash starts with "a_"
    local url = "https://cdn.discordapp.com/avatars/" .. user.id .. "/" .. user.avatar .. "." .. ext
    if size then url = url .. "?size=" .. size end
    return url
end
```

A `nil` avatar hash means no custom avatar; show your own default, typically the user's initial on a background tinted from their id. Avatars are game-side fetched assets, not part of the extension bundle; download and cache them yourself with an image node.

## Live updates

Registers like everything else:

```lua
local dirty = false
discordsdk.set_callback("relationship_created",     function() dirty = true end)
discordsdk.set_callback("relationship_deleted",     function() dirty = true end)
discordsdk.set_callback("relationship_groups_updated", function() dirty = true end)
discordsdk.set_callback("user_updated",             function() dirty = true end)
```

| Event | Meaning | Payload |
| --- | --- | --- |
| `relationship_created` | A friend was accepted or added, a block happened, or a pending row appeared | `{ user_id, is_discord_relationship_update }`, where the boolean says which relationship side changed, Discord or game |
| `relationship_deleted` | A removal, decline, or unblock | Same shape |
| `relationship_groups_updated` | Regrouping of the list, for example a friend went online | `{ user_id }` |
| `user_updated` | Profile, status, or in-game activity of a known user changed | `{ user_id }` |

`user_updated` fires often, every status flicker. Rebuild the visible rows lazily: mark dirty, process once per frame or on a timer.

`discordsdk.get_friend(user_id)` fetches one relationship object, useful right after an invite lands to show who invited.

## Pending requests and blocks

- `pending_incoming` and `pending_outgoing` render in a separate section from friends; the extension exposes the list, acceptance and decline of requests happen from the user's own actions, see the management functions on the API page.
- `blocked` rows are delivered so your UI can hide or manage them; never invite or auto-join with them. As a courtesy note, the SDK also offers local muting of blocked players inside lobby voice calls.
- Provisional friends typically show `game_type = "friend"` with `discord_type = "none"`; after account linking merges, the relationship row stays the same body with both types set.

## Sorting that survives contact with players

1. Group one: online and in your game, sorted by most relevant in-game activity or progress signal.
2. Group two: online elsewhere, players who own your game float up first.
3. Group three: offline.
4. Within each group sort by display name; remember case-sensitivity differs between locales, so prefer a case-insensitive key.

## See also

- [API-Reference.md](API-Reference.md): object shapes and exact event payloads.
- [Voice.md](Voice.md): local muting for blocked players in calls.
- [Game-Invites-and-Joining.md](Game-Invites-and-Joining.md): invites usually target rows from this list.
