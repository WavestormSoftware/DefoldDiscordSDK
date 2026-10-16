# CI

The repository's continuous integration lives in
`.github/workflows/ci.yml` and `scripts/ci/`. It is designed to run entirely
on generic GitHub runners with no Defold install, plus one opt-in job that
uses the real Defold build server.

## Jobs

One `build` job runs the following checks on `ubuntu-latest`:

- `syntax` (`scripts/ci/syntax.sh`): every C++ translation unit under
  `defolddiscordsdk/src` is type-checked with
  `g++ -fsyntax-only -std=c++17 -I defolddiscordsdk/include -I test/dmsdk`.
  No engine, no linking: this proves the sources are consistent C++ against
  the documented API surface, nothing more.
- `capi-consistency` (`scripts/ci/capi_check.sh`): every `X(...)` entry in
  `defolddiscordsdk/src/discordsdk_capi.h` must name a function that is
  also declared in the vendored `defolddiscordsdk/include/cdiscourd.h`.
  This catches binding/header drift before it ever reaches the build server.
- `lua` (`scripts/ci/lua.sh`): the example script is parse-checked
  (`luac5.1 -p`, with `luac`/`luajit` fallbacks).
- `docs-quality` (`scripts/ci/docs_check.py`): all markdown files must be
  pure ASCII (no emojis, no typographic replacements), relative links must
  resolve to real files, code fences must be balanced, and `game.project`
  must only use whitelisted section keys for the extension, with
  `[library] include_dirs` naming the extension folder.
- `license-audit` (`scripts/ci/license_audit.py`): proves the proprietary
  Discord Social SDK payload cannot sneak into a commit. No binaries
  (`.dll`, `.so`, `.dylib`, `.aar`, `.kef`, `.exe`, `.lib`, `.pdb`,
  `.xcframework`) may exist anywhere outside the git-ignored
  `discord_social_sdk/` working directory, `defolddiscordsdk/res/` may only
  contain text placeholders (`README.md`, `*.txt`, `.keep`), and stub headers
  must not contain engine-copied content.

## Why test/dmsdk is safe to ship

The syntax check needs a `dmsdk/sdk.h` to include. Real SDK headers come from
a Defold install; instead of shipping engine-derived files, the repository
carries a minimal stub in `test/dmsdk/`. It declares only the small API
surface the extension uses, written from scratch for this repository. It
contains no engine code, no standard library re-derivations beyond plain
standard headers, and no copied license text, which means committing it is
clean: it is our own MIT-covered work product. The CI license audit greps the
stub for engine-copied markers as a tripwire. It intentionally under-specifies
the API; a green syntax run is a plausibility gate, not an engine link check.

## Real native builds (opt-in)

The `native-build` workflow job performs a genuine build through Defold's
hosted native-extension build service (Extender) using the
`dapetcu21/setup-defold@v3.0.2` action, which downloads `bob.jar`
(verified: the action exists on the GitHub Marketplace). It compiles the
extension as a library artifact for several platforms:

```
java -jar "$BOB" resolve
java -jar "$BOB" --build-server https://build.defold.com \
    --build-artifacts library --platform <platform> build
```

This job is triggered by `workflow_dispatch` and a weekly schedule, not on
every push, to keep per-push runtime small and to be a polite citizen of the
free public build server (the Defold docs allow unrestricted use, so if you
want push coverage, add `push:`/`pull_request:` to the job's trigger list).
If you prefer bob to default to stable automatically, you can drop
`--build-server` (stable channel bob defaults to `https://build.defold.com`).

For fully self-hosted coverage, run a local Extender server per
https://defold.com/manuals/extender-local-setup/ and pass its URL instead.
