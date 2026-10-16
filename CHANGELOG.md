# Changelog

All notable changes to DefoldDiscordSDK are documented here. The format is
based on Keep a Changelog and the project uses semantic versioning.

## [1.0.0] - 2026-10-17

Initial public release.

### Added

- Defold native extension binding of the modern Discord Social SDK through
  the runtime-loaded `discord_partner_sdk` C ABI (no C++20 requirement,
  Defold cloud builds safe).
- Rich presence with the full current field set: state, details, large and
  small assets, party size, confidential party, timestamps, join, spectate
  and match secrets, field URLs, status display type and supported
  platforms. Updates are queued and rate limited to five per twenty
  seconds.
- Discord account linking (browser OAuth authorization, access and refresh
  token management, token persistence support) and full social layer mode.
- Unified friends list of Discord friends plus in-game game friends, with
  live relationship and presence events.
- Game invites, including join and spectate secret handling for both
  Discord-launched and in-game flows.
- Client-side lobbies with metadata, search, member management, text chat
  and chat history.
- Lobby voice chat with optional krisp noise cancellation support.
- Local tooling to fetch the Discord Social SDK binaries per platform
  (`tools/fetch_discord_sdk.sh` and `.bat`); binaries are never committed.
- Example Defold project exercising the entire Lua surface
  (`example/example.script`), with key bindings for presence, clearing,
  friends, authorization, lobbies and invites.
- Extensive documentation: README, 16-page wiki and NOTICE covering
  licensing.
- Continuous integration: Lua parsing, C API consistency against the
  vendored header, documentation quality, license audit and a real
  cross-platform compile through Defold's official build server
  (x86_64-linux, x86_64-win32, arm64-ios, armv7-android).
- MIT-licensed extension code; the Discord Social SDK itself remains under
  Discord's own license terms and is fetched separately.

[1.0.0]: https://github.com/WavestormSoftware/DefoldDiscordSDK/releases/tag/v1.0.0
