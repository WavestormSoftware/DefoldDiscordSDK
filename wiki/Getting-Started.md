# Getting Started

This page takes a Defold project from zero to a visible Discord presence and, optionally, to the full social layer. It assumes you have a Discord account with an application registered at the [Discord Developer Portal](https://discord.com/developers/applications).

## What you need

- A Defold project (Defold 1 or newer).
- A Discord application with its numeric application id.
- The Discord desktop client installed and running on the test machine for presence mode.
- The Discord Social SDK binaries, staged by the fetch tool. See [Installation.md](Installation.md).

## Step one: add the extension

Either add this repository as a Defold dependency, Project, then Dependencies, then the GitHub URL https://github.com/WavestormSoftware/DefoldDiscordSDK, or copy the `defolddiscordsdk` extension folder into your project. Adding a dependency means Defold's build service compiles it; since the extension binds Discord's C ABI and loads the library at run time, no SDK headers are needed for that build.

## Step two: stage the SDK

Run the fetch tool once per machine, pointing it at the downloaded SDK archive or its extracted folder:

```bash
tools/fetch_discord_sdk.sh /path/to/discord-social-sdk-archive
```

Windows:

```bat
tools\fetch_discord_sdk.bat C:\path\to\discord-social-sdk-archive
```

The tool puts the right binaries into `defolddiscordsdk/res/<platform>/`. Without them nothing breaks: the extension logs that the library is missing and the game keeps running with the module returning empty results.

## Step three: configure the application id

Add the section to `game.project` with your own application id:

```project
[defolddiscordsdk]
application_id = 1349146942634065960
```

## Step four: set and clear presence

```lua
local discordsdk = require "discordsdk"

function init(self)
    discordsdk.initialize() -- falls back to the game.project section
    discordsdk.update_presence({
        details = "Sparring in the Training Grounds",
    })
end

function final(self)
    discordsdk.clear_presence()
end
```

Presence appears in the user's profile and friends list about a minute after the update. This is presence mode: no login screen, no scopes, no account linking. It requires that the Discord desktop client (or the Discord app on Android) is running.

## Step five: unlock the social layer

The friends list, invites, lobbies, and voice require the OAuth scope preset named `communication`. Initialize with it, then authorize once:

```lua
discordsdk.initialize({ scopes = discordsdk.scopes_communication })
discordsdk.authorize()
```

A browser window opens, the player approves the link, and the `authorized` event fires. From there the full surface becomes available: `get_friends`, invites, `lobby_join_or_create`, `start_voice`, and more. See [OAuth-and-Tokens.md](OAuth-and-Tokens.md) for the flow, redirect setup, and token persistence.

## Verify

- Node: Defold console shows the extension log lines during `initialize`.
- Discord: your profile shows the presence; the friends list shows the activity for your game.
- Failures are always soft: when the SDK library is missing every call returns `false`, `nil`, or an empty table and one clear log line explains the state.

## See also

- [Installation.md](Installation.md) for SDK download details and both install workflows.
- [Configuration.md](Configuration.md) for every option.
- [Rich-Presence.md](Rich-Presence.md) for the full presence payload.
- [API-Reference.md](API-Reference.md) for the complete surface.
