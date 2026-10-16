# NOTICE -- DefoldDiscordSDK

This file describes the license status of everything this repository
distributes or references. See `LICENSE.md` for the license text of the
repository's own code.

1. DefoldDiscordSDK extension code.
   All repository code written for this project (the `defolddiscordsdk`
   extension, the `discordsdk` Lua module, the `example` project, the helper
   tooling under `tools/` and `scripts/`, and the documentation) is licensed
   MIT; see `LICENSE.md`.

2. Vendored Discord C API header (`defolddiscordsdk/include/cdiscourd.h`).
   The file name is preserved from the official Discord Social SDK
   distribution. The header contains only public API type, constant, and
   function declarations, and is included solely so that this repository's
   bindings can compile standalone. It remains Copyright Discord, is included
   de minimis in this form, and the official source is the SDK download
   available from your own Discord developer account.

3. Discord Social SDK binaries.
   The runtime libraries (`discord_partner_sdk.*`), the krisp audio
   components, and the `.kef` machine-learning models are proprietary Discord
   software. They are NOT shipped in this repository and are not distributed
   under the MIT license above. Games obtain them by running
   `tools/fetch_discord_sdk.sh` (or `fetch_discord_sdk.bat`), which downloads
   an SDK copy under Discord's own terms as referenced from the Discord
   developer portal, and stages it locally. Those terms govern your use of
   the binaries, not this repository.

4. Defold engine and template.
   The Defold engine is licensed by the Defold Foundation. This extension
   interacts with the engine exclusively through the engine's public `dmsdk`
   headers at extension-build time. No engine source or engine binaries are
   distributed by this repository.

5. Trademarks.
   DefoldDiscordSDK is an independent community project. It is not affiliated
   with, endorsed by, or sponsored by Discord Inc. or the Defold Foundation.
   "Discord", the Discord logo, "Defold", and the Defold logo are trademarks
   of their respective owners.

6. Open source components of the SDK itself.
   The open source code compiled into the Discord Social SDK is enumerated in
   the SDK archive's `License-Notices.txt`, which ships with every SDK copy
   you download. That inventory is intentionally not duplicated here; keep the
   file together with the staged binaries.
