# Troubleshooting

A table of observed symptoms, causes, and fixes. Work top to bottom when unsure; the first entries cover the paths most integrations trip on.

## Matrix

| Symptom | Cause | Fix |
| --- | --- | --- |
| Log reports the SDK library was not loaded; calls return `false` | Binaries not staged and no `lib_path` set | Run the fetch tool per [Installation.md](Installation.md), or set `[defolddiscordsdk] lib_path` in the editor category Defold Discord SDK. The game keeps running either way; rebuild and check `is_ready()`. |
| Presence is set, nothing shows in Discord | Discord desktop client not running, or Game Activity disabled for your app in the client settings | Start the client; enable the app under Game Activity. The extension cannot raise the client for the user. |
| Presence set on one device only, another device shows nothing | Rich presence via RPC is desktop and Android-only; not supported on iOS or web | Use the authenticated social path for those surfaces or ignore; see [Platform-Support.md](Platform-Support.md). |
| Presence updates seem stuck during testing | Discord propagation of about a minute, plus the extension's five-per-twenty-seconds queue | Wait for propagation, watch `presence_result` events. Identical payloads are deduplicated, so change a field before re-sending. |
| `authorize()` fails with HTTP 401 | Redirect mismatch, or the application is not enabled for the Social SDK | Re-check the portal redirect list against `default_redirect`, re-enable the SDK on the Getting Started page per [Installation.md](Installation.md). |
| `authorize()` returns `true` but no browser or Discord prompt ever appears | Another program owns the `discord-ipc-0` socket (Vesktop, arRPC, some Flatpak bridges) and silently ignores the authorization request; the SDK waits forever | Offer the browser flow: `discordsdk.authorize({ browser = true })` (it replaces the hung attempt). |
| Browser reports redirect mismatch on authorize | Portal registered redirect and requested one differ | Align `default_redirect` with what the portal has registered. |
| Player sees no friends after linking | Presence scopes only, so `get_friends` etc. are unavailable | initialize with `scopes = communication` or `discordsdk.scopes_communication`, authorize again, see [Configuration.md](Configuration.md). |
| Join button never appears on presence | Presence lacks required invite data | The presence needs a non-empty `party.id`, `party.size` with `party.max`, `secrets.join`, and a non-empty `supported_platforms` mask. Verify with a second account; the owner never sees their own join button. Checklist: `party.id`, sizes, join secret, platforms. |
| Join works in game but not from Discord client | Launch registration missing on the receiver machine | Keep `auto_register_launch = 1`, or call `register_launch_command` or `register_steam` each run; registration is per machine and per run. |
| `get_launch_args()` returns nils after a deep link join | Game was launched through a path without the secret, or you called too late | Call `get_launch_args()` early in startup; verify the invite was a join, not spectate, and deep link versus normal launch, plus the `activity_join` callback for the in-game accept path. |
| Lobby creates fail partway through testing | Development-mode application-wide rate limit | One hundred create-or-join and one hundred messages per two hours per application. Budget your suite, see [Lobbies.md](Lobbies.md). |
| Guests cannot join an old lobby even though it still exists | Client-created lobby join secrets expire thirty days after creation | Rotate secrets on long-lived lobbies and re-invite through the fresh one. |
| A lobby vanished overnight | Idle lobbies are deleted automatically | Default idle timeout is about five minutes with no connected members; keep a member connected or expect deletion, see [Lobbies.md](Lobbies.md). |
| Voice says participating but nobody hears each other | Device too quiet, or crisper settings expected | Verify `get_input_devices` picks the intended device, volumes at non-zero, and `voice_settings_updated`. krisp is a no-op where its assets are missing; built-in suppression still applies, see [Voice.md](Voice.md). |
| Lobby voice crowds and degrades | Call above the recommended size | Keep calls at around twenty-five participants or fewer; text scales further than voice. |
| Client-side lobbies rejected although created | Mixed server-side and client-side lobby flows | Create-or-join and lobby_leave apply only to client-created lobbies; server-created ones are a different model, pick one per lobby type. |
| Presence fails with `presence_result` ok false, code from Discord | Invalid application id or Discord rejects the activity | Verify the `application_id` in `[defolddiscordsdk]` against the portal, inspect the `error` event payload for the message. |
| Everything returns `false`, `nil`, empty tables even after fixes | `enabled = false`, or the calls precede `initialize()` | Call `initialize()` once at startup and check the `enabled` option. `version()` still answers either way, use it to confirm module integrity. |

## Reading failure signals

The extension reports failures in three channels, in order of usefulness:

1. `presence_result` payloads, exact per update `{ ok, error }`.
2. `error` events, `error` with `message`, `code`, `detail`, for the broader call failures.
3. Log lines at `initialize`, stating which library file was loaded or what blocked it.

Capture all three in tests; the mono-channel debugging habit misses half the signal.

## When nothing helps

- Re-run the fetch tool and rebuild; stale staged files account for a share of the reports.
- Verify on a second machine or a second user account that the issue is not local client state.
- Compare `discordsdk.SDK_VERSION` against the release notes of what you downloaded.
- Report SDK side issues to Discord's own support process; extension side, the repo's tracker.

## See also

- [API-Reference.md](API-Reference.md): the unavailable contract deltas.
- [Testing-Guide.md](Testing-Guide.md): structured reproduction.
- [Configuration.md](Configuration.md): keys behind most of these symptoms.
