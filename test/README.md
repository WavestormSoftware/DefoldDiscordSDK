# test/dmsdk - minimal Defold SDK header stub

This directory is a tiny, hand-written stand-in for the Defold SDK headers
(what a real Defold install provides as `<dmsdk/sdk.h>` and friends). It is
laid out as `test/dmsdk/dmsdk/sdk.h` so that `-I test/dmsdk` makes the
extension's real `#include <dmsdk/sdk.h>` lines resolve. It exists for
exactly one purpose: so that plain `g++` can type-check this repository's
own C++ sources on a CI runner without downloading or installing Defold.

    g++ -fsyntax-only -std=c++17 -I defolddiscordsdk/include -I test/dmsdk \
        $(find defolddiscordsdk/src -name '*.cpp')

These checks are run automatically by `scripts/ci/syntax.sh` and the `syntax`
step of `.github/workflows/ci.yml`.

## What it is not

- This is NOT the real dmsdk. It contains no engine code and no Discord code.
- The declarations are written from scratch against the public API surface the
  extension actually uses (a subset of `lua.h`/`lauxlib.h` entry points,
  `dmLog*`, and the `dmScript`/`dmConfigFile`/`dmExtension` namespaces).
- It is license-clean: it was authored for this repository and carries no
  copied material from the Defold engine sources.

## Safety to ship

Because it consists solely of declarations we wrote ourselves (there is no
standard-library-privative or engine-derived content), the stub is safe to
commit and to redistribute under the repository's MIT license. If any content
that looks like it originates from the actual Defold engine sources ever ends
up in this directory, it must be removed before committing.

The stub always under-approximates the real SDK: a green syntax check means
the sources are plausible against our reading of the API, not that they link
with the engine. Real coverage is provided by the Defold cloud build server;
see `docs/CI.md`.
