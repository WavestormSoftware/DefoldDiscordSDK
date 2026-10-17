# OAuth and Tokens

Account linking is what upgrades `DefoldDiscordSDK` from presence-only to the full social layer. The player picks once, in a browser page, which Discord identity to share with your game; afterwards the SDK connection carries friends, invites, lobbies, chat, and voice with no further interaction.

## Scopes

Scopes decide what the link grants. Preset tables are provided:

```lua
discordsdk.scopes_presence       = { "openid", "sdk.social_layer_presence" }
discordsdk.scopes_communication  = { "openid", "sdk.social_layer" }
```

- Presence preset: account linking, provisional accounts, the friends list, rich presence. Choose it when the game has no own chat or lobbies.
- Communication preset: everything above plus messaging, lobbies, linked channels, and voice. It is a superset, so most games ship it and skip presence as a separate run mode.

Pass either preset to `initialize({ scopes = ... })` or set `scopes = communication` in `game.project`. Avoid asking for extra scopes; unneeded ones drop user trust and conversion.

## The authorize flow

```lua
local discordsdk = require "discordsdk"

function init(self)
    discordsdk.initialize({
        application_id = "1349146942634065960",
        scopes = discordsdk.scopes_communication,
    })
    register_callbacks()
end

-- on a Connect button:
function connect(self)
    discordsdk.authorize()
end

-- results:
discordsdk.set_callback("authorized", function()
    local me = discordsdk.get_current_user()
    print("linked as " .. tostring(me.display_name))
end)

discordsdk.set_callback("error", function(e)
    -- e = { message, code } -- show a friendly retry message here
end)
```

`authorize()` opens the system browser at Discord, the player approves, and the browser hands the result back to the installed extension flow. The call returns `true` when the flow could start, not when linking finished; completion is exactly the `authorized` event.

Redirect handling: the SDK's default redirect resolves through the Discord application configuration. Set `default_redirect` in `[defolddiscordsdk]` or `initialize()` when you host your own redirect page or need an explicit target for your build environment. A mismatch between what the portal has registered and what the game requests produces a browser-side 401 or redirect mismatch page and an `error` event. Keep the portal list and the option aligned.

## Public client, confidential client

Discord classifies OAuth clients as public or confidential.

- Public client: no client secret shipped; the SDK handles the browser handshake. Required for some SDK entries. Most desktop and mobile games operate here.
- Confidential client: a server holds the secret and server-side alternatives cover the sensitive flows. Recommended for production past a security review, per Discord's own guidance.

Review with your security process before release; nothing in the extension stores a client secret for you, and it should not.

## Token persistence

Tokens survive across runs only if you save them. A small store around `sys.save`:

```lua
local discordsdk = require "discordsdk"
local PATH = "discord_tokens"

local function load(path)
    if sys.exists(path) then
        return sys.load(path)
    end
    return { access = nil, refresh = nil }
end

function restore()
    local t = load(PATH)
    if t.access and t.refresh then
        discordsdk.set_token(t.access, t.refresh)
        discordsdk.connect()
        return true
    end
    return false
end

function store()
    -- call this after authorized: get_token() hands out the stored pair
    local t = discordsdk.get_token()
    if t ~= nil then
        sys.save(PATH, { access = t.access, refresh = t.refresh, scopes = t.scopes, expires_at = t.expires_at })
    end
end
```

Notes:

- `set_token(access, refresh)` restores a previous link without a browser round trip; follow it with `connect()`.
- After a successful `refresh_token()`, or at any time after `authorized`, call `discordsdk.get_token()` and update your store with the returned `{ access, refresh, scopes, expires_at }` table; `nil` means nothing is stored yet.
- `token_expired` means the access token failed: try `refresh_token()`, and on repeated failure fall back to `authorize()`.
- `revoke_token()` invalidates the grant at Discord; use it for an account disconnect feature. `clear_token()` only forgets the local copy.

Treat both tokens as credentials: never log them, never print them, do not copy them into analytics or crash reports. Wipe the save when a player chooses to unlink while also calling `revoke_token()`.

## is_authenticated

```lua
if discordsdk.is_authenticated() then
    -- skip the browser path and connect
end
```

Convenience gate for the restore path. It answers locally about a loaded token pair, it does not confirm with Discord that the grant is still valid, so stale tokens still surface as `token_expired` after connect.

## connect and disconnect

`connect()` opens the SDK's online session after a token restore. It is normally unnecessary because `authorize()` connects by itself; use it in the persistence pattern above. `disconnect()` closes the session; presence mode keeps working through the local client, but social calls go dormant.

## Consoles, noted for later

Device-code authorization, where the player types a short code at a Discord page, is a possible path for consoles later. It is future work in this extension and the practical recommendation for shipped games today is to prioritize the desktop and mobile store flow first rather than designing around console linking.

## See also

- [API-Reference.md](API-Reference.md): the token functions and events.
- [Configuration.md](Configuration.md): `default_redirect` and `scopes` keys.
- [Troubleshooting.md](Troubleshooting.md): 401 and redirect mismatch cases.
