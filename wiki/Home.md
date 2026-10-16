# Home

`DefoldDiscordSDK` is a native [Defold](https://defold.com) extension that fully integrates the modern Discord Social SDK. It replaces the old, deprecated `discordrich` extension that was based on the discontinued discord-rpc client. The Lua module table is the global `discordsdk`; the extension folder is `defolddiscordsdk` and the extension library name is `DefoldDiscordSDK`.

DefoldDiscordSDK is built and maintained by [Wavestorm Software](https://wavestorm.net/). Software. Games. Websites.

| | |
| --- | --- |
| Lua module | `local discordsdk = require "discordsdk"` |
| Extension name | `defolddiscordsdk` |
| Extension library name | `DefoldDiscordSDK` |
| Language | Native extension, C++ binding Discord's C ABI, library loaded at run time |
| Modes | Presence only without login; full social layer after OAuth account linking |
| Platforms | Windows x86_64, macOS x86_64 and arm64, Linux x86_64; Android and iOS stubs; HTML5 no-op |
| License | MIT for the extension; Discord SDK binaries are not in the repository and stay under Discord's terms |
| Game config | `[defolddiscordsdk]` section in `game.project` |

Two modes, one dependency:

- **Presence mode.** No login and no OAuth. Presence is pushed by direct RPC to a running Discord desktop client, or to the Discord app on Android. Works even for users who never link an account.
- **Social mode.** After one `discordsdk.authorize()` the game gains the unified friends list, game invites, lobbies with text chat and history, and lobby voice chat with optional krisp noise cancellation.

## Page index

| Page | Content |
| --- | --- |
| [Getting-Started.md](Getting-Started.md) | From empty Defold project to visible presence, minimal steps |
| [Installation.md](Installation.md) | Downloading the SDK, the fetch tool, and the advanced `lib_path` workflow |
| [Configuration.md](Configuration.md) | Every `game.project` key, `initialize()` options, and error modes |
| [API-Reference.md](API-Reference.md) | Every function, constant, event, and payload table |
| [Rich-Presence.md](Rich-Presence.md) | The full activity payload and how Discord renders it |
| [Game-Invites-and-Joining.md](Game-Invites-and-Joining.md) | Invite lifecycle, ask to join, deep links, join handling |
| [Lobbies.md](Lobbies.md) | Client-side lobbies, metadata, text chat, history, rate limits |
| [Friends-and-Relationships.md](Friends-and-Relationships.md) | Unified friends list, events, avatars |
| [Voice.md](Voice.md) | Lobby voice calls, audio settings, devices, krisp |
| [OAuth-and-Tokens.md](OAuth-and-Tokens.md) | Account linking, redirect setup, token storage |
| [Platform-Support.md](Platform-Support.md) | Per platform matrix and current limitations |
| [Testing-Guide.md](Testing-Guide.md) | Two accounts, three clients, rate limit aware testing |
| [Updating-the-SDK.md](Updating-the-SDK.md) | How to bump the SDK version safely |
| [Troubleshooting.md](Troubleshooting.md) | Symptom, cause, and fix table |
| [FAQ.md](FAQ.md) | Questions that come up before every integration |

## Suggested reading order

New integration: [Getting-Started.md](Getting-Started.md), [Installation.md](Installation.md), [Configuration.md](Configuration.md).

Building the social layer: [OAuth-and-Tokens.md](OAuth-and-Tokens.md), [Friends-and-Relationships.md](Friends-and-Relationships.md), [Game-Invites-and-Joining.md](Game-Invites-and-Joining.md), [Lobbies.md](Lobbies.md), [Voice.md](Voice.md).

Debugging: [Troubleshooting.md](Troubleshooting.md), [Testing-Guide.md](Testing-Guide.md), [API-Reference.md](API-Reference.md).

## Notices

DefoldDiscordSDK is built and maintained by [Wavestorm Software](https://wavestorm.net/). DefoldDiscordSDK is not affiliated with, endorsed by, or connected to Discord Inc. or the Defold Foundation. Discord and Defold are trademarks of their respective owners. The Discord Social SDK binaries are fetched separately under Discord's terms; this repository ships no Discord binaries.
