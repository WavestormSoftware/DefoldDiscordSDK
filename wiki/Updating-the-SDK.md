# Updating the SDK

The Discord Social SDK ships binaries that Discord replaces periodically. Because the binaries are per application downloads rather than repository artifacts, updating is a deliberate, local action. This page is the full loop from portal to verified runtime.

## When to update

- Discord lists a newer version on your application's Downloads page.
- A fix or feature you need is called out in the SDK changelog, for example a platform addition like Android presence.
- A new major SDK series requires re-staging before Defold builds pick it up.

Re-running for a trivially newer patch version is cheap but does replace runtime files; update on purpose, not reflex.

## Steps

1. Open the [Discord Developer Portal](https://discord.com/developers/applications) and select your application.
2. On the Getting Started page of the application, the Social SDK section, open the Downloads page.
3. Choose the latest version and download the C++ variant.
4. Run the fetch tool from the repository root:

   ```bash
   # linux or macos
   tools/fetch_discord_sdk.sh /path/to/new-sdk-download

   # windows
   tools\fetch_discord_sdk.bat C:\path\to\new-sdk-download
   ```

   The tool replaces the previously staged content in `defolddiscordsdk/res/x86_64-win32/`, `defolddiscordsdk/res/x86_64-osx/`, and `defolddiscordsdk/res/x86_64-linux/`, and stages optional krisp voice libraries and `.kef` models when present. Or extract the SDK to a folder named `discord_social_sdk` beside the repository root and run the tool without an argument, which is the auto-detect path.
5. Review `defolddiscordsdk/res/README.md` for the staged layout and confirm nothing unexpected appeared.
6. Rebuild the game and enter the version at runtime:

   ```lua
   local discordsdk = require "discordsdk"
   print(discordsdk.version())   -- extension and SDK lines, one string
   print(discordsdk.SDK_VERSION) -- SDK revision actually loaded
   ```

7. Rerun a compact smoke as in [Testing-Guide.md](Testing-Guide.md); a presence set plus, when linked, a lobby create-and-leave is typically enough to spot a regression.

## Spot-checking the staged binaries

When a build mysteriously still behaves old, verify what is actually staged. The extension loads the library from these folders at run time, so a stale file there wins over the new download:

```bash
# linux: look for the shared object and its exported C ABI surface
nm -D defolddiscordsdk/res/x86_64-linux/*.so | grep -i discord

# macos
otool -L defolddiscordsdk/res/x86_64-osx/*.dylib

# windows (from a developer prompt)
dumpbin /exports defolddiscordsdk\res\x86_64-win32\*.dll
```

Confirm the file timestamps moved after the update and that the exported symbols still match the C ABI the extension binds; the extension defines these calls itself, so ABI stability across Discord releases is your update gate.

If you use the advanced `lib_path` workflow instead of staged folders, re-check those paths too; see [Installation.md](Installation.md).

## Rolling back

Keep the previous SDK archive around until the smoke suite confirms the new one. Rolling back means re-running the fetch tool with the earlier archive; nothing else changes, since the extension source is version agnostic.

## Version reporting facts

- `discordsdk.SDK_VERSION` is discovered at load time and reports `nil` when no library is present.
- `discordsdk.VERSION` is constant and describes the extension, not the SDK.
- Log lines from `initialize` mention which library file was actually loaded, the fastest way to catch a stale or double copy.

## See also

- [Installation.md](Installation.md): both workflows in full.
- [Platform-Support.md](Platform-Support.md): per platform binary folders.
- [Troubleshooting.md](Troubleshooting.md): after a botched update.
