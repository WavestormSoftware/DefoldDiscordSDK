# Testing Guide

Discord integrations fail quietly in ways unit tests cannot see: presence shows up a minute late, invites vanish when tested against your own account, lobby caps reset on a two hour window. This page describes the setup and the habits that surface real problems early.

## The three-client setup

Install the stable Discord client, Discord Canary, and Discord PTB side by side; they do not conflict. Sign each one into a different account. You can then observe delivery between accounts on one machine:

| Client | Role |
| --- | --- |
| Stable | The account your build aims at, typically the receiver of invites |
| Canary | Second account for join, chat, and voice counterparty |
| PTB | Third account for group flows and spectate |

Run your game logged into whichever client you want to exercise; the SDK talks to whichever Discord client session is available on the machine, and installing the variants is the supported way to run more than one.

## Two accounts minimum

A user never sees their own invite card and cannot test joining their own presence. Every invite, join, or lobby chat test therefore needs a second account, and preferably a second client on the same machine or a second machine. Buttons on presence are likewise invisible to their owner; a colleague on another client confirms them.

For the old discord-rpc based extensions, testing two game instances on one machine relied on flipping the `DISCORD_INSTANCE_ID` environment variable so each RPC session matched its own client. The Social SDK flow uses installed client variants instead: run Discord stable and Canary, run the game once per pairing, and let presence spread. For repeatable in-game multiplicity the `instance` presence flag remains the marker for instance aware sessions.

## Presence checks

- Presence appears on profiles and member lists with about a minute of Discord-side propagation; do not treat an instant check as failure.
- The extension rate limits to five updates per twenty seconds, queued, and deduplicates identical payloads, so a test loop that spams updates is still safe; watch the `presence_result` events to confirm actual deliveries.
- Confirm the viewer has Game Activity enabled for your application; a client setting toggles it off for all games and looks like a bug in yours.

## Invite and join checks

1. Host sets presence with `party` and `secrets.join` and `supported_platforms`, then sends an invite programmatically.
2. Receiver on Canary accepts in your custom UI, through `activity_invite_created` and `accept_invite`, and lands through `activity_join`.
3. Repeat with the receiver clicking Join directly on the host's presence, covering the client-side deep link.
4. Close the receiver's game, click again, and verify the deep link starts the game and `get_launch_args()` returns the secret.
5. Cover `send_join_request` and `send_join_request_reply`, which needs the ask-to-join state rather than a plain invite.
6. Cover spectate separately; `INVITE_SPECTATE` and the spectate secret follow the same wiring.

## Lobby and voice checks

| Case | How |
| --- | --- |
| Create then join | Host `lobby_join_or_create(secret)`, guest with the same secret sees membership events |
| Metadata update | Re-call `lobby_join_or_create` as a member with new metadata |
| Chat | Two clients type; both see `message_created`, sender included |
| History | Join later, fetch `lobby_get_messages(L, 50)` and verify ordering oldest to newest |
| Voice | `start_voice`, both see `voice_participant_changed`; toggle mute and verify on the other end |
| Teardown | `lobby_leave` on both; verify removal events |
| Idle expiry | Leave everything connected off and let the lobby idle out, default five minutes |
| Stale secrets | Roll a thirty day old secret; joins then fail as expected |

Rate limit discipline: development applications get one hundred lobby create-or-join operations and one hundred lobby messages per two hours, per application, not per user. A test suite that rebuilds lobbies in a loop can spend the entire window; budget your lobby rotation, reuse secrets mid-test, and keep one tester per feature until the window resets. Linked channel operations are tighter still, twenty per two hours.

## What to log in CI

- `version()` at startup, so failures trace to an SDK revision.
- Every `presence_result` and `error` payload, with time stamps.
- Lobby ids, hashed secrets, and membership transitions, never raw tokens.
- `is_ready()` transitions around network drops; the reconnect path is where real tears appear.

## Quick smoke sequence

```lua
-- expect: prints on a healthy machine with a linked Discord client
local discordsdk = require "discordsdk"

function assert_smoke()
    assert(discordsdk.initialize({ application_id = "1349146942634065960" }))
    -- presence path:
    assert(discordsdk.update_presence({ details = "smoke" }))
    -- social path only on a linked session:
    local list = discordsdk.get_friends() or {}
    print("smoke: friends welcomed", #list)
end
```

## See also

- [Troubleshooting.md](Troubleshooting.md): when a check fails.
- [Lobbies.md](Lobbies.md): development rate limits in detail.
- [Configuration.md](Configuration.md): making tests deterministic with `enabled`.
