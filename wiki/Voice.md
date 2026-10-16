# Voice

Lobby voice calls run through Discord's voice infrastructure without the Discord app needing to be open on the talker's machine: the SDK streams individual user audio itself. Voice requires the `communication` scope preset and a lobby the user belongs to; see [Lobbies.md](Lobbies.md).

## Starting a call

```lua
local discordsdk = require "discordsdk"

function open_session(self, lobby_id)
    discordsdk.start_voice(lobby_id)   -- joins the call for this lobby
end

function close_session(self)
    discordsdk.stop_voice()            -- leave the active call
end
```

| Function | Behavior |
| --- | --- |
| `start_voice(lobby_id)` | Joins the lobby's voice call; participants arrive through `voice_participant_changed` |
| `stop_voice()` | Leaves the active call when the user is in one |
| `stop_all_voice()` | Leaves every active call across all lobbies, for full shutdown paths |

Keep call sizes at around twenty-five participants or fewer. Larger text lobby memberships are fine; audio calls of that size are not, and Discord's guidance is explicit about it.

## Participants

```lua
discordsdk.set_callback("voice_participant_changed", function(payload)
    -- payload = { lobby_id, users = { user, ... } }
    voice_bar.render(payload.lobby_id, payload.users)
end)
```

The payload carries the full current set of participants, so the callback replaces rendering state instead of being diffed against it. Voice state of a user is public inside the call context only; no per-user speaking metrics are promised at this layer.

## Settings

```lua
discordsdk.set_muted(true)
discordsdk.set_deaf(false)
discordsdk.set_noise_cancellation(true)
discordsdk.set_noise_suppression(true)
discordsdk.set_echo_cancellation(true)
discordsdk.set_agc(true)
discordsdk.set_input_volume(80)
discordsdk.set_output_volume(100)
```

| Function | Meaning |
| --- | --- |
| `set_muted(b)` | Self microphone mute; remote players stop hearing you |
| `set_deaf(b)` | Deafened state; you stop hearing others, implied mute carries |
| `set_noise_cancellation(b)` | krisp machine learning cancellation, see below |
| `set_noise_suppression(b)` | SDK built-in suppression, available on every platform |
| `set_echo_cancellation(b)` | Acoustic echo removal when speakers are on open air |
| `set_agc(b)` | Automatic gain control on the input path |
| `set_input_volume(n)` | Microphone volume, percent 0 to 100 |
| `set_output_volume(n)` | Playback volume, percent 0 to 100 |

All control functions return `boolean`. Settings changes the SDK makes on the player's behalf, or that another surface changes, arrive through `voice_settings_updated`:

```lua
discordsdk.set_callback("voice_settings_updated", function(s)
    -- s = { muted, deaf, noise_cancellation, noise_suppression, echo_cancellation, agc }
    settings_menu.sync(s)
end)
```

Persist your own choices alongside, with `sys.save`, since the SDK does not own your settings menu state.

## Devices

```lua
local inputs  = discordsdk.get_input_devices()   -- { { id, name }, ... }
local outputs = discordsdk.get_output_devices()

discordsdk.set_input_device(inputs[1].id)
discordsdk.set_output_device(outputs[1].id)
```

Both getters return tables of `{ id, name }` entries and empty tables when voice is not connected. The setters accept id strings from those tables and return `false` for unknown ids. Rerender device pickers when the game gains focus, device lists move constantly.

## krisp noise cancellation

Discord's strongest filter is krisp, a machine learning model trained for speech against background noise. It is optional and is distributed as separate voice libraries plus `.kef` model files.

- The fetch tool stages krisp assets when the downloaded SDK archive contains them; see [Installation.md](Installation.md).
- With krisp staged, `set_noise_cancellation(true)` enables it; `voice_settings_updated` reports the resulting state.
- Without krisp assets the call still runs: `set_noise_cancellation` is a no-op on that machine and the SDK built-in suppression remains the floor.
- Availability on Linux is limited; treat `set_noise_cancellation` as best effort and verify through `voice_settings_updated` rather than assuming.

Also worth wiring: the SDK supports locally muting blocked players inside voice calls, matching your relationship-based moderation, see [Friends-and-Relationships.md](Friends-and-Relationships.md).

## A settings panel pattern

```lua
local discordsdk = require "discordsdk"

local prefs = {}

function prefs.apply()
    discordsdk.set_noise_suppression(true)
    discordsdk.set_echo_cancellation(true)
    discordsdk.set_agc(true)
end

function prefs.toggle_mute()
    return not discordsdk.set_muted(not prefs.muted) and prefs.muted
end

return prefs
```

## See also

- [Lobbies.md](Lobbies.md): where calls live.
- [Installation.md](Installation.md): staging krisp files.
- [API-Reference.md](API-Reference.md): exact signatures and the settings payload.
