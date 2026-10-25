# Changelog

All notable changes to DefoldDiscordSDK are documented here. The format is
based on Keep a Changelog and the project uses semantic versioning.

## [1.0.5] - 2026-10-25

### Added

- `authorize({ browser = true })`: a loopback browser login that does not
  depend on a Discord client. The extension builds the PKCE + state
  request itself, opens the system browser (`xdg-open`/`open`/
  `ShellExecute`), accepts the redirect on `127.0.0.1:<ephemeral port>`
  (loopback only, forged `state` is refused and the listener keeps
  waiting), then exchanges the code through the same `GetToken` path as
  the SDK flow. Times out after 5 minutes.
- `abort_authorize()` and `is_authorizing()`.

### Fixed

- Linking did nothing when a third party client (Vesktop/arRPC) owned the
  `discord-ipc-0` socket: the SDK sent the authorization to that client,
  which never answers, and `authorize()` stayed pending forever. Every
  later call was then ignored as "already running". The pending attempt
  can now be cancelled or replaced by the browser flow.
- Cancelling a pending SDK authorization no longer surfaces as an
  `authorize aborted` error.
- `version()` and `VERSION` reported 1.0.3 in the 1.0.4 release.

## [1.0.4] - 2026-10-18

### Fixed

- Passing any options table to `discordsdk.initialize()` could abort the
  game at boot with heap corruption (`free(): invalid size`): the compiled
  option parsing pushed a stack-local scratch string into the allocator
  during the assignments that moved the parsed values into the client
  options. Parsing now copies option values straight into their destination
  (`ParseScopesOption`/`ParseStringOption` take an output parameter, the
  game.project fallbacks use `assign`) so no string temporary outlives the
  Lua calls, and the abort is gone. Verified against a repro of the 1.0.3
  crash with a full variant matrix: no-args unchanged, `{}`, string scopes,
  scopes arrays with mixed value types, `enabled = false`, string
  coercions, and error paths all boot cleanly.
- Two latent option-parser bugs fixed while diagnosing: `ParseBoolOption`
  unconditionally wrote to its null `changed` pointer on both call sites,
  and `ParseStringOption` reported the inverse of the intended `present`
  flag (neither was observable through the public surface before, because
  `initialize()` passed no flag).

## [1.0.3] - 2026-10-17

### Added

- `discordsdk.get_token()` returns the stored token pair as
  `{ access, refresh, scopes, expires_at }` (or `nil`), completing the
  documented OAuth persistence pattern: `get_token()` + `sys.save` after
  `authorized`, `set_token()` + `connect()` on the next run. Tokens stay
  local memory inside the extension until the game chooses to persist them.

### Fixed

- The example script called APIs that do not exist (`create_or_join_lobby`,
  `auth_state`) and passed wrong argument shapes to `send_invite` and
  `send_join_request_reply`. It now binds the real surface directly
  (`lobby_join_or_create`, `is_authenticated`, `discordsdk.INVITE_JOIN`,
  `discordsdk.REPLY_YES`) and the lobby demo drives the chat message from
  `lobby_created` the way the events actually arrive.
- Wiki message payloads now match the real `message` table field names
  (`id`, `author_id`, `author_username`, `created_at`, `from_game`), and
  `message_deleted` documents the `channel_id` field it carries.
- The OAuth page's persistence example stores real values via
  `get_token()` instead of a stub that read from nothing.

## [1.0.2] - 2026-10-17

### Changed

- Repository review pass. Removed dead token plumbing from the launch
  argument parser, corrected comment drift (loader symbol-visibility wording,
  callback data comment, event payload contract), and made the first
  stub-mode warning name the call that triggered it.
- The application finalize step now releases the Discord Social SDK library
  handle; per-script reloads still keep the loaded library, unchanged.
- The wiki's `supported_platforms` wording now says plainly that the SDK has
  no per-OS desktop flags: `PLATFORM_WINDOWS`, `PLATFORM_MACOS` and
  `PLATFORM_LINUX` are aliases of the single desktop bit.
- The README credits Discord's Social SDK and the Defold Foundation, and the
  vendored header attribution is unchanged (Discord's own licensing).
- LICENSE.md and all source file headers now name Wavestorm Software as the
  copyright holder.

## [1.0.1] - 2026-10-17

### Fixed

- Engine link failure on consuming projects: two SDK log and join callback
  error paths referenced the `Discord_Free` allocator symbol directly, which
  the Defold engine link cannot resolve because the Discord Social SDK is
  only loaded at run time. Both paths now release (or consciously drop) the
  SDK-owned string through the resolved function table, and a new CI gate
  inspects compiled archives to prevent any direct ABI reference from
  shipping again.

### Removed

- Dead helpers `PushDevices` and `FirePresenceResult` that produced
  unused-function warnings during extension builds.

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
- Automated tag-driven releases: pushing a semver tag runs all gates and
  publishes a GitHub release containing the tag's source as a Defold-ready
  library zip with checksums and changelog notes, ready to paste into
  game.project dependencies; Defold compiles the sources itself.
- MIT-licensed extension code; the Discord Social SDK itself remains under
  Discord's own license terms and is fetched separately.

[1.0.4]: https://github.com/WavestormSoftware/DefoldDiscordSDK/releases/tag/v1.0.4
[1.0.3]: https://github.com/WavestormSoftware/DefoldDiscordSDK/releases/tag/v1.0.3
[1.0.2]: https://github.com/WavestormSoftware/DefoldDiscordSDK/releases/tag/v1.0.2
[1.0.1]: https://github.com/WavestormSoftware/DefoldDiscordSDK/releases/tag/v1.0.1
[1.0.0]: https://github.com/WavestormSoftware/DefoldDiscordSDK/releases/tag/v1.0.0
