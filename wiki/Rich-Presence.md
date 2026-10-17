# Rich Presence

Rich presence is the activity Discord shows on the user's profile, in the member list, and in the friends list: what the player is doing, for how long, where, and with how many others. It is also the carrier for game invites: party information, join secrets, and platform routing all travel inside the presence payload.

Rich presence works in two modes:

- **Presence mode, no login.** The extension talks by direct RPC to a running Discord desktop client, or to the Discord app on Android from SDK 1.10 onward. No account linking is needed.
- **Social mode.** Once authorized, the same payload updates through the authenticated connection instead of the local client.

Presence through RPC is not supported on iOS or web clients, and Discord does not support it on consoles; see [Platform-Support.md](Platform-Support.md).

## How Discord renders the payload

```
Playing My Game                    <- game name, from the application or the name field
Exploring the Forgotten Vale       <- details
In a group (2 of 5)                <- state and party
```

- Line one is the game name. Discord normally uses your application's registered name; the `name` field overrides it when you need a different display string.
- Line two is `details`, the primary description of what the player is doing.
- Line three combines `state`, a secondary user status, with the party badge `current of max`, which only renders when `party` is set.

## update_presence fields

```lua
discordsdk.update_presence({
    name = "My Game: Chapter Two",       -- optional override of the app display name
    type = discordsdk.TYPE_PLAYING,         -- one of the TYPE_* constants, default playing
    details = "Score 2400",
    details_url = "https://example.com/leaderboard",
    state = "Rank Diamond",
    state_url = "https://example.com/rankings",
    timestamps = {
        start_ts = os.time() - 540,      -- counts up: elapsed time
        end_ts = os.time() + 900,        -- counts down: remaining time
    },
    assets = {
        large_image = "world-map",       -- asset key from the portal or an external URL
        large_text = "World map",
        large_url = "https://example.com/maps/world",
        small_image = "hero-avatar",
        small_text = "Level 40",
        small_url = "https://example.com/heroes",
        invite_cover_image = "invite-cover", -- banner shown on invite cards
    },
    party = {
        id = "ab12cd34",
        size = 2,
        max = 5,
        privacy = nil,                   -- optional party privacy flag as exposed by the SDK
    },
    secrets = {
        join = "joinsecret-8231",        -- powers the join button, invites, and deep links
        spectate = "spectate-8231",      -- powers spectate invites when used
        match = nil,                     -- optional match secret
    },
    buttons = {                          -- at most two
        { label = "Get the game", url = "https://example.com/store" },
        { label = "Community",    url = "https://discord.gg/example" },
    },
    status_display_type = discordsdk.DISPLAY_STATE, -- status text shows the state field
    supported_platforms = discordsdk.PLATFORM_DESKTOP, -- where join actions are offered
    instance = true,                     -- marks the session as a distinct instance
})
```

Return value: `true` when the update was accepted into the queue, `false` when the SDK is unavailable or the payload was rejected early. Delivery to Discord is asynchronous; each acknowledged update produces a `presence_result` event whose payload has `ok` and, on failure, an `error` string.

The extension rate limits presence for you: at most five updates per twenty seconds, queued and deduplicated. Sending the same payload twice in a row is a no-op, so it is safe to refresh presence every frame with a dirty flag.

## Field semantics

| Field | Notes |
| --- | --- |
| `name` | Display name override on line one. Useful when the registered application name is too long or needs characters it cannot contain. |
| `type` | `discordsdk.TYPE_PLAYING` (0), `discordsdk.TYPE_STREAMING` (1), `discordsdk.TYPE_LISTENING` (2), `discordsdk.TYPE_WATCHING` (3), `discordsdk.TYPE_CUSTOM` (4), `discordsdk.TYPE_COMPETING` (5). Discord is known to route streaming and custom types only for specific flows; prefer playing. |
| `details`, `details_url` | Primary description and its click target. URLs must be plain https links. |
| `state`, `state_url` | Secondary status and its click target. |
| `timestamps` | Unix seconds. `start_ts` only counts up and shows elapsed time; `end_ts` counts down and shows remaining time. Both may be set; when `end_ts` is present the timer shows remaining. Set `start_ts` to the session start and update on session change, not per frame. |
| `assets.large_image`, `assets.large_text` | Main artwork key or external URL, plus hover text. Asset keys are stored lowercased by the portal; reference them exactly as stored. |
| `assets.large_url`, `assets.small_url` | Click targets for the artworks. |
| `assets.small_image`, `assets.small_text` | Corner artwork, typically a class or mode badge, plus hover text. |
| `assets.invite_cover_image` | Banner shown on the invitation card when this presence is used to invite someone. Falls back to the static invite image configured in the portal when unset. Accepts an asset key or an external URL for per-mode dynamic covers. |
| `party` | `id` groups concurrent sessions so Discord can tell parties apart; `size` is the current player count, `max` the maximum. Only real party information belongs here; invented numbers break the join experience. |
| `secrets.join`, `secrets.spectate` | Opaque tokens; invite acceptance and the join button hand them to your game through `activity_join` and `get_launch_args()`. Round-trip them from your session manager; never hardcode. |
| `buttons` | Up to two buttons, each `{ label, url }`. Buttons are only visible to other users, never to the owner of the presence. Labels should be action phrased. |
| `status_display_type` | `discordsdk.DISPLAY_NAME` shows the game name as the user's status text, `discordsdk.DISPLAY_STATE` shows `state`, `discordsdk.DISPLAY_DETAILS` shows `details`. Default is name. |
| `supported_platforms` | Bitmask of `discordsdk.PLATFORM_*`. Controls where Discord offers the join button: the desktop flags (`PLATFORM_DESKTOP` and the `PLATFORM_WINDOWS`/`PLATFORM_MACOS`/`PLATFORM_LINUX` aliases, which the SDK collapses onto one desktop bit) require the desktop flag on the viewer and in the target presence, and `PLATFORM_ANDROID`/`PLATFORM_IOS` route the mobile deep links. |
| `instance` | Legacy flag marking the session as one instance of the game. Keep `true` for standard sessions. |

## Assets in the portal

Images referenced by key must be uploaded to your application, under Rich Presence, Art Assets. Up to three hundred assets are supported, and every key is lowercased on save. Keep names canonical and stable.

- Size: 1024 by 1024 or larger square, spartan backgrounds read best at small sizes.
- External URLs are allowed for both artworks and the invite cover; they must still meet the same dimensions and provide modern formats such as webp, and, unlike uploaded keys, animated formats too.
- The invite cover deserves its own asset per game mode or map rather than a logo, since the banner is what a player sees in the invitation.

## clear_presence

```lua
discordsdk.clear_presence()
```

Removes the activity; the profile line for your game disappears within the propagation delay. Call it on shutdown and on logout screens. Clearing does not send a queued update and does not count against the rate limit the same way as churn.

## Presence while testing

Rank-limited updates and the one minute of Discord-side propagation make presence look stuck during testing. Use the checklist in [Testing-Guide.md](Testing-Guide.md) and the rate limit notes in [Troubleshooting.md](Troubleshooting.md).

## See also

- [Game-Invites-and-Joining.md](Game-Invites-and-Joining.md): the consume side of secrets and deep links.
- [API-Reference.md](API-Reference.md): `update_presence`, `clear_presence`, `presence_result`.
- [Platform-Support.md](Platform-Support.md): where presence through RPC is supported.
