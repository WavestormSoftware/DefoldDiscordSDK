# Getting Started

This page takes a Defold project from zero to a visible Discord presence and, optionally, to the full social layer. It assumes you have a Discord account with an application registered at the [Discord Developer Portal](https://discord.com/developers/applications).

## What you need

- A Defold project (Defold 1 or newer).
- A Discord application with its numeric application id.
- The Discord desktop client installed and running on the test machine for presence mode.
- The Discord Social SDK binaries, staged by the fetch tool. See [Installation.md](Installation.md).

## Step one: add the extension

There are two ways.

**Dependency on a release (simplest).** Add one of these URLs to the project dependencies (in `game.project` under `[project]`, or via Project, then Dependencies in the editor):

```project
[project]
dependencies = https://github.com/WavestormSoftware/DefoldDiscordSDK/archive/refs/tags/1.0.0.zip
```

The tag archive URL (above) and the release asset URL `https://github.com/WavestormSoftware/DefoldDiscordSDK/releases/download/1.0.0/DefoldDiscordSDK-1.0.0.zip` are built automatically by the release workflow, contain the tag's tracked source code (`game.project` plus the `defolddiscordsdk` extension folder), and are pinned to that exact version. Always point at a specific tagged version so your build stays reproducible; when you adopt a newer release, bump the tag in the URL. Defold compiles the extension sources through its cloud build service, so the zip needs no precompiled content. A bare repository URL like `https://github.com/WavestormSoftware/DefoldDiscordSDK` also works but tracks the default branch instead of a released version.

**Local copy.** Copy the `defolddiscordsdk` extension folder into your project. This is the right choice when you want to modify the extension, and it also lets you stage the SDK binaries inside `res/` without any extra plumbing (see [Installation.md](Installation.md)). Adding a dependency means Defold's build service compiles it; since the extension binds Discord's C ABI and loads the library at run time, no SDK headers are needed for that build.

## Step two: stage the SDK

Run the fetch tool once per machine, pointing it at the downloaded SDK archive or its extracted folder:

```bash
tools/fetch_discord_sdk.sh /path/to/discord-social-sdk-archive
```

Windows:

```bat
tools\fetch_discord_sdk.bat C:\path\to\discord-social-sdk-archive
```

The tool puts the right binaries into `defolddiscordsdk/res/<platform>/`. Without them the extension logs a loud boot-time error telling you to add the SDK and every `discordsdk.*` call returns false; the game itself keeps running.

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
