# Platform Support

`DefoldDiscordSDK` targets desktop first and degrades deliberately everywhere else. The matrix below is the current contract; changes are called out in the wiki and the changelog.

## Matrix

| Platform | Build | Presence without login | Social layer after linking | Notes |
| --- | --- | --- | --- | --- |
| Windows x86_64 | Full, first class | Yes, direct RPC to the desktop client | Yes | Primary test target |
| macOS x86_64 and arm64 | Full, universal binary | Yes | Yes | |
| Linux x86_64 | Full, first class | Yes | Yes | krisp noise cancellation assets may be limited, see [Voice.md](Voice.md) |
| Android | Stubs today; SDK supports the feature set, mobile integration is a documented later phase | Later; presence reaches the Discord app on Android from SDK 1.10 onward | Later | The extension compiles with stubs so projects ship unchanged |
| iOS | Stubs today; later phase | Not supported: presence via RPC does not work on iOS | Later | Documented limitation, not a bug |
| HTML5 | No-op | No | No | Every call returns `false`, `nil`, or empty tables |
| Consoles | Future | No | Future | Device-code OAuth is a possible later path, currently discouraged and documented as future |

Desktop and Android RPC presence needs a running or installed Discord client; it does not require the player to link an account.

## What stub mode means

On platforms the extension does not integrate yet, the native part compiles to stubs and the Lua module stays valid code:

- `initialize()` returns `false` for the SDK-dependent path.
- Every call follows the unavailable contract, `false`, `nil`, empty tables.
- The game runs. No crashes, no null dereferences in Lua because the payloads are plain tables.

This exists so that projects can add the dependency once and build for all platforms while later phases land.

## Presence via RPC is not supported on iOS or web

Restated explicitly because it surprises teams: Discord's rich presence via direct RPC client protocol is supported on desktop and, from SDK 1.10, on Android, and it is not supported on iOS or web clients. Consoles are absent as well. When your game must show presence from those surfaces, the authenticated social path of an SDK version that supports them is the route, not the presence RPC path.

## Deploy notes per desktop

- Windows: the staged library sits under `defolddiscordsdk/res/x86_64-win32/`.
- macOS: `defolddiscordsdk/res/x86_64-osx/` carries a universal build covering x86_64 and arm64.
- Linux: `defolddiscordsdk/res/x86_64-linux/`; the SDK binaries ship independently of glibc assumptions you should verify with your toolchain.

All three folders are filled by the fetch tool described in [Installation.md](Installation.md), or resolved through `lib_path`.

## Browser and storage side notes

- The OAuth flow opens the system browser; nothing in-game renders the Discord page.
- Deep links for invites on mobile follow the SDK's universal link and app link guidance, relevant again once mobile lands.
- Steam launcher registration applies to desktop stores; `register_steam` is per machine.

## HTML5 details

Nothing is loaded, nothing is polled. The module reports the unavailable contract. `version()` still answers with the extension version, which keeps logs and about boxes informative on web builds.

## See also

- [Installation.md](Installation.md): binaries per platform folder.
- [Configuration.md](Configuration.md): `enabled` toggling per platform.
- [FAQ.md](FAQ.md): common platform questions.
