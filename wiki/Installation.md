# Installation

The Discord Social SDK binaries are proprietary and are not committed to this repository. Each game copies them from its own per-application download in the Discord Developer Portal. This page documents exactly how to obtain them and both ways to make them available at run time.

## Why binaries are separate

Discord licenses the Social SDK under its own terms. Committing the libraries would be redistribution and would tie this repository to a version snapshot. The extension therefore links nothing at build time: the C++ code binds Discord's C ABI and loads the library at run time, which is also why the extension builds fine on Defold's cloud build service without the SDK.

## Downloading the SDK

1. Open the [Discord Developer Portal](https://discord.com/developers/applications) and select your application.
2. On the application's Getting Started page, enable the Social SDK.
3. Open the Downloads page that appears under the application.
4. Choose the latest listed version.
5. Download the C++ variant. Keep the archive or extract it somewhere discoverable.

The download is per application: an application id other than your own cannot use it for production features.

## Workflow one: the fetch tool

The fetch tool stages binaries, and optional krisp voice assets, into the extension's resource folders. Run it from the repository root.

Linux and macOS:

```bash
tools/fetch_discord_sdk.sh /path/to/sdk-archive-or-folder
```

Windows:

```bat
tools\fetch_discord_sdk.bat C:\path\to\sdk-archive-or-folder
```

Auto-detect: when the SDK was extracted to a folder named `discord_social_sdk` next to the repository root, the tool finds it by itself and the argument can be omitted.

What it does:

- Detects the platform subfolders inside the SDK download.
- Copies the desktop libraries into `defolddiscordsdk/res/x86_64-win32/`, `defolddiscordsdk/res/x86_64-osx/`, and `defolddiscordsdk/res/x86_64-linux/`.
- When the archive contains the optional krisp voice libraries and `.kef` noise cancellation models, stages those as well.

The tool is idempotent: re-running it after a download replaces what was staged. See `defolddiscordsdk/res/README.md` for the exact output layout. The staged files stay local and uncommitted.

## Workflow two: `lib_path` (advanced)

Instead of staging inside the extension, place the binaries anywhere the game can resolve at run time, for example next to the bundled executable, and point the extension at the folder:

```project
[defolddiscordsdk]
application_id = 1349146942634065960
lib_path = ./discord_sdk
```

Notes:

- `lib_path` is a run-time fallback searched when the library is not otherwise found. It is applied when the bundle loads.
- This workflow is useful for self-hosted builds, CI caches, and games that ship the SDK next to the executable for legal tracking.
- Relative paths resolve from the bundle working directory; absolute paths are recommended.

Shipping the binaries next to the game executable has the same effect; `lib_path` simply makes the search explicit.

## What happens without the binaries

The extension always keeps the game running. When the SDK library is missing, the module logs a clear line stating the library was not loaded, and every call returns `false`, `nil`, or an empty table as documented in the [API-Reference.md](API-Reference.md). To find out early, use `get_status()` and `is_ready()`.

## Checklist

- [ ] SDK archive downloaded from the portal, C++ variant, latest version.
- [ ] Fetch tool executed, or `lib_path` configured.
- [ ] `res/README.md` reviewed after staging.
- [ ] `is_ready()` returns `true` at run time.

## See also

- [Configuration.md](Configuration.md) for the `lib_path` key and related options.
- [Updating-the-SDK.md](Updating-the-SDK.md) for replacing an installed version.
- [Platform-Support.md](Platform-Support.md) for per platform binary availability, including krisp on Linux.
