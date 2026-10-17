#!/usr/bin/env python3
"""Sanity checks for the packed Defold library zip.

Usage: check_library_zip.py <zip-file>

Verifies the archive layout that Defold dependency loading needs:
  - game.project at the root, declaring include_dirs
  - the extension folder with ext.manifest, include and source files
"""

import sys
import zipfile


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2

    path = sys.argv[1]
    try:
        archive = zipfile.ZipFile(path)
    except zipfile.BadZipFile:
        print("check_library_zip: %s is not a readable zip" % path, file=sys.stderr)
        return 1

    names = archive.namelist()
    required = [
        "game.project",
        "defolddiscordsdk/ext.manifest",
        "defolddiscordsdk/ext.properties",
        "defolddiscordsdk/include/cdiscourd.h",
        "defolddiscordsdk/src/extension.cpp",
        "defolddiscordsdk/src/discordsdk_capi.cpp",
    ]
    missing = [name for name in required if name not in names]

    sources = [n for n in names if n.startswith("defolddiscordsdk/src/") and n != "defolddiscordsdk/src/"]
    binaries = [n for n in names if n.endswith((".dll", ".so", ".dylib", ".kef", ".aar"))]

    problems = []
    if missing:
        problems.append("missing required entries: %s" % ", ".join(missing))
    if len(sources) < 4:
        problems.append("too few extension source files found (%d)" % len(sources))
    if binaries:
        problems.append("proprietary SDK binaries must not be packaged: %s" % ", ".join(binaries[:4]))

    settings = archive.read("game.project").decode("utf-8", "replace")
    if "include_dirs = defolddiscordsdk" not in settings:
        problems.append("game.project does not declare include_dirs = defolddiscordsdk")

    if problems:
        for problem in problems:
            print("check_library_zip: %s" % problem, file=sys.stderr)
        return 1

    print(
        "check_library_zip: OK (%d entries, %d extension sources, no binaries)" % (len(names), len(sources))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
