# Discord example

A single game object demo that exercises the whole `discordsdk` Lua module and logs
every event and action to the console. No GUI assets are needed; open the
Defold console (or run the bundled build with the console attached) and watch
the output.

## Prerequisites

1. **Discord application id** - create an application at
   https://discord.com/developers/applications and put its id in
   `game.project` under `[defolddiscordsdk]` as `application_id`.
   Most features (rich presence, invites, lobbies) require it.
2. **SDK binaries** - the Discord Social SDK binaries are not committed to
   this repository. Stage them into `defolddiscordsdk/res/` with one of:
   - `tools/fetch_discord_sdk.sh` (Linux and macOS)
   - `tools\fetch_discord_sdk.bat` (Windows)

   Both scripts default to the extracted SDK folder `discord_social_sdk/` in
   the repository root; they also accept a path to a folder or a `.zip`
   archive, plus `--src` and `--dest` overrides. Add `--mobile` to stage
   Android (`.aar`) and iOS (`xcframework`) artifacts of a future phase.
3. **Discord client** - run the Discord desktop app while testing so the SDK
   can connect; presence updates, invites and join requests surface there.

## Key press list

The demo binds the keyboard number row (actions defined in
`/input/game.input_binding`):

| Key | Action   | What it does |
|-----|----------|--------------|
| `1` | `key_p`  | Sends a rich presence for a fictional e-sports match (`Rocket Arena`, ranked grand final, timestamps start now / end in 15 minutes, large and small assets, party 1 of 4, join secret, two buttons). |
| `2` | `key_c`  | Clears the rich presence. |
| `3` | `key_f`  | Prints the current user, the friend count and details of the first friend. Also remembers the first friend id for key 6. |
| `4` | `key_a`  | Prints the authorization state; when not authenticated calls `discordsdk.authorize()`. |
| `5` | `key_l`  | Creates or joins the demo lobby with the fixed secret `defold-demo-lobby`, then sends the lobby message `hello from defold`. Pressing `5` again leaves the lobby. |
| `6` | `key_i`  | Sends an activity invite with a short message to the first friend (or to your own id when no friend is known yet). Never hardcodes other users' ids. |

If pressing a number does nothing, the matching `key_trigger` block is missing
from `/input/game.input_binding` - the example keeps running either way and the
boot log lists what each key would have done.

## Event callbacks exercised

The `discordsdk.initialize` call registers and logs all of these:
`ready`, `status_changed`, `error`, `authorized`, `activity_invite_created`,
`activity_join`, `activity_join_request` (replied via
`discordsdk.send_join_request_reply`), `lobby_created`, `lobby_updated`,
`lobby_deleted`, `lobby_member_added`, `lobby_member_removed`,
`lobby_member_updated`, `message_created`, `relationship_created`,
`relationship_deleted`, `presence_result`, `user_updated`, `token_expired` and
`voice_participant_changed`.

## Notes

- Without staged binaries the extension still exposes every `discordsdk.*`
  function; each call then returns `false`. The example reports this as
  `[demo] '...' not available - skipped (see tools/fetch_discord_sdk)` instead
  of crashing.
- On shutdown (`final`) the demo leaves the lobby and clears presence so no
  stale activity lingers in the Discord client.
