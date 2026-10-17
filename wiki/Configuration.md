# Configuration

Configuration has two layers: static keys in the `[defolddiscordsdk]` section of `game.project`, and per-call options passed to `discordsdk.initialize()`. Options set at initialize override the game.project defaults for that run.

## game.project section

The section is registered with the editor via the extension's `ext.properties` file, so in Defold it renders as a dedicated **Defold Discord SDK** category (Runtime group) in the `game.project` settings pane, with input labels and help tooltips per field. Key order and defaults:

```project
[defolddiscordsdk]
application_id =
lib_path =
steam_app_id =
default_redirect =
scopes = presence
auto_register_launch = 1
```

| Key | Default | Description |
| --- | --- | --- |
| `application_id` | empty | The numeric id of your Discord application. Required for any presence or social call to reach Discord. Game-provided ids are validated at login; a wrong id yields `presence_result` failures and `error` events. |
| `lib_path` | empty | Run-time fallback folder where the extension searches for the SDK binaries. Only needed for the advanced install workflow, see [Installation.md](Installation.md). |
| `steam_app_id` | empty | Steam application id. When set, the extension registers the Steam launcher variant so invite deep links start the game through Steam on machines where the game was bought on Steam. |
| `default_redirect` | empty | Overrides the OAuth redirect used by `authorize()`. Leave empty while using the default flow; set it when you host a redirect page or when a platform requires an explicit target, see [OAuth-and-Tokens.md](OAuth-and-Tokens.md). |
| `scopes` | `presence` | Scope preset name. `presence` is the minimal set for rich presence, friends, and account linking. `communication` enables the full social layer; it is a superset, so it is the only value you need for lobbies, voice, and chat. |
| `auto_register_launch` | `1` | When `1`, the extension registers the launch command during `initialize()` so Discord can start your game when a player opens an invite. Set `0` to register later and manually. |

A typical full-mode section:

```project
[defolddiscordsdk]
application_id = 1349146942634065960
scopes = communication
steam_app_id = 480
```

## initialize() options

The same knobs are available per call. Fields are optional; anything omitted falls back to the game.project section.

```lua
local discordsdk = require "discordsdk"

local ok = discordsdk.initialize({
    application_id = "1349146942634065960", -- falls back to game.project
    scopes = discordsdk.scopes_communication,  -- or discordsdk.scopes_presence, or a custom string array
    auto_register_launch = true,            -- default true
    steam_app_id = "480",
    default_redirect = "https://mygame.example/oauth",
    enabled = true,                         -- default true; false runs a fully inert module
})
```

| Option | Type | Default | Notes |
| --- | --- | --- | --- |
| `application_id` | string | game.project value | Numeric application id as a string. |
| `scopes` | table or string | `presence` | A preset table (`discordsdk.scopes_presence`, `discordsdk.scopes_communication`) or a custom array of OAuth scope strings. |
| `auto_register_launch` | boolean | `true` | Registers the launch command so Discord can start the game for players who click an invite. |
| `steam_app_id` | string | game.project value | Enables Steam launcher registration. |
| `default_redirect` | string | game.project value | OAuth redirect override. |
| `enabled` | boolean | `true` | Set `false` to disable the integration at run time, for example behind your own settings menu. Disabled mode behaves like a missing SDK but keeps returning consistent values. |

`initialize()` returns `true` when the SDK library is available and startup completed. Call it once, before any other module call. Calls made before `initialize()` are ignored safely.

## Launch registration detail

`auto_register_launch` covers the common case: Discord learns how to start the game so that a player who clicks an invite while the game is closed gets launched straight into the session. Registration is local to the machine; call it every run. Two manual overrides exist:

```lua
discordsdk.register_launch_command("mygame://") -- custom protocol launch value
discordsdk.register_steam("480")                -- Steam application id
```

See [Game-Invites-and-Joining.md](Game-Invites-and-Joining.md).

## Error modes

| Situation | Behavior |
| --- | --- |
| SDK library missing | `initialize()` returns `false`, one clear log line, every call returns `false`, `nil`, or empty tables, game runs normally |
| `enabled = false` | Same as a missing library but deterministic; `version()` still answers |
| Invalid or missing `application_id` | Initialization succeeds locally; presence updates and social calls fail, surfaced through `presence_result` with `ok = false` and the `error` event |
| Calls before `initialize()` | Ignored, return values follow the unavailable contract |
| Rate limited presence | Updates are queued and deduplicated inside the extension, five per twenty seconds; results are reported one at a time through `presence_result` |
| Rate limited lobby operations | Discord enforces application-wide development limits, reported through the `error` event, see [Lobbies.md](Lobbies.md) |

## See also

- [Installation.md](Installation.md) for how `lib_path` is resolved.
- [API-Reference.md](API-Reference.md) for the exact contracts.
- [Troubleshooting.md](Troubleshooting.md) for symptom based diagnosis.
