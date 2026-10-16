# FAQ

Questions that come up before and during an integration. Short answers first, links after.

**Do players need a Discord account?**

For presence mode, no login happens at all: the game hands its activity to the running Discord desktop client or the Discord app on Android. On Android the app must be installed and signed in; on desktop the client must run. For the social layer, players link their account once through the browser page; players without a Discord interest can usually be provisioned as Game Friends, which is how the unified list keeps working for them, see [Friends-and-Relationships.md](Friends-and-Relationships.md).

**Does this replace discordrich?**

Yes. The old extension rides on the discontinued discord-rpc client library; this one uses the modern Social SDK and covers presence plus the features that came after it. Read the two-mode description on [Home.md](Home.md).

**Does the game ship Discord binaries?**

No. Binaries come from your own per-application download in the Discord Developer Portal, staged by the fetch tool or resolved via `lib_path`, see [Installation.md](Installation.md).

**Why are there no binaries in the repository?**

Discord's SDK terms govern the binaries and downloads are per application, so committing them would be both a redistribution and a version lock. See [Installation.md](Installation.md).

**Is Defold's cloud build supported?**

Yes. The extension binds Discord's C ABI and loads the library at run time, so no SDK headers are required for the build and nothing proprietary reaches the build service, see [Getting-Started.md](Getting-Started.md).

**Can users without a linked account see the game and receive invites?**

They see the presence as viewers. Sending and accepting invites requires the linked path and the right scopes; presence-only games work without linking on desktop, see [Rich-Presence.md](Rich-Presence.md).

**What does an invite do while the game is closed?**

The registered launch command, or Steam registration, lets Discord start the game and hand over the join secret; the game reads it with `get_launch_args()`, see [Game-Invites-and-Joining.md](Game-Invites-and-Joining.md).

**How big can lobbies and voice go?**

Text lobbies hold up to one thousand members and each user holds up to one hundred lobbies per game. Voice calls should stay near twenty-five participants. Development-mode rate limits, one hundred create-or-join and message operations per two hours per application, apply until production limits are granted, see [Lobbies.md](Lobbies.md).

**Is voice team-only?**

No. Any lobby the user joins can carry a call, including lobbies Discord mediates across platforms. See [Voice.md](Voice.md).

**Does voice require the Discord app open?**

No. The SDK streams individual user voice itself once a lobby call is up. The desktop client is only needed earlier, for presence-only mode and for invite handling outside the game, see [Voice.md](Voice.md).

**What about consoles?**

Documented as future. Device-code OAuth is a possible later path; the current recommendation in these docs is not to design shipping readiness around it yet, see [Platform-Support.md](Platform-Support.md).

**Are tokens safe to store with `sys.save`?**

The persistence pattern lives on [OAuth-and-Tokens.md](OAuth-and-Tokens.md). Treat access and refresh tokens as credentials: persist only with your normal user-data safeguards, never log or transmit them beyond their purpose.

**Why do presence updates feel slow during testing?**

Discord takes about a minute to propagate an activity through its surfaces, and the extension queues at most five updates per twenty seconds with deduplication. Both behaviors are intentional, see [Testing-Guide.md](Testing-Guide.md).

**Why is there no iOS or web presence?**

Rich presence via RPC is not supported on iOS or web clients; the extension documents this inline and ships stubs for those platforms, see [Platform-Support.md](Platform-Support.md).

**Does krisp noise cancellation need extra files?**

Yes, optional voice libraries and `.kef` model files; the fetch tool stages them when the archive has them. Without them `set_noise_cancellation` is a no-op and the built-in suppression stays, see [Voice.md](Voice.md) and [Installation.md](Installation.md).

**Does the game need to do anything before presence shows up?**

A correctly configured `application_id`, a staged SDK, and initialize. Presence alone asks for nothing more; the quickstart on the front page works verbatim, see [Getting-Started.md](Getting-Started.md).

**Where do I ask new questions?**

The troubleshooting matrix covers the common failure modes, see [Troubleshooting.md](Troubleshooting.md). For Discord SDK side behavior, Discord's own documentation and developer support are the authorities.
