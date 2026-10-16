Discord Social SDK runtime staging.

The extension does not link the SDK; at startup the loader
(defolddiscordsdk/src/discord_loader.cpp) searches the game binary directory,
this res/<arch>/ subfolder, and [defolddiscordsdk] lib_path from game.project for
the partner sdk binary:

  x86_64-win32/  discord_partner_sdk.dll (+ discord_krisp.dll, krisp .kef models)
  x86_64-osx/    libdiscord_partner_sdk.dylib (+ libdiscord_krisp.dylib)
  x86_64-linux/  libdiscord_partner_sdk.so (krisp optional)

Copy the binaries for your target here (or set lib_path) and they are picked
up automatically; leave them out and the extension runs in stub mode and logs
one line explaining the state.
