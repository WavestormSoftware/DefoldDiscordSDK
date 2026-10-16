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
extension as a library artifact for x86_64-linux, x86_64-win32, arm64-ios
and armv7-android.

Bob uploads only the folders listed via `--ne-build-dir` as extension
source for `--build-artifacts=library` jobs, and it synthesizes an
upload-side `ext.manifest` itself. Pointing `--ne-build-dir` straight at
`defolddiscordsdk/` aborts with "Duplicate file in upload zip: ext.manifest"
because the walk would add our real manifest as a second copy. The job
therefore stages a manifest-free copy of the sources first. The invocation
is roughly:

```
rm -rf build/ne-upload
mkdir -p build/ne-upload
cp -r defolddiscordsdk/src build/ne-upload/src
cp -r defolddiscordsdk/include build/ne-upload/include

java -jar "$BOB" resolve
java -jar "$BOB" --build-server https://build.defold.com \
    --ne-build-dir build/ne-upload \
    --build-artifacts library --platform <platform> build
```

All four platforms are verified to produce `libdefault.a` this way. Build
logs are uploaded as artifacts on failure for debugging.

The job is triggered by `workflow_dispatch` and a weekly schedule, not on
every push, to keep per-push runtime small and to be a polite citizen of the
free public build server; Defold allows unrestricted use, so widening the
trigger is a one-line change. For self-hosted coverage, run a local Extender
server (see the "Setup local build server" manual at defold.com) and pass
its URL via `--build-server`.
