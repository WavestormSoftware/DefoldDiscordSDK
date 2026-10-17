#!/usr/bin/env python3
"""Extract the changelog section for a version and append Defold usage notes.

Usage: release_notes.py <version> [<tag>] [<repository>]

The version is looked up in CHANGELOG.md under a "## [version]" heading.
The tag (default: the version) is used for the exact links in the footer.
"""

import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2

    version = sys.argv[1].lstrip("v")
    tag = sys.argv[2] if len(sys.argv) > 2 else version
    repo = sys.argv[3] if len(sys.argv) > 3 else "WavestormSoftware/DefoldDiscordSDK"

    changelog = Path(__file__).resolve().parent.parent.parent / "CHANGELOG.md"
    text = changelog.read_text(encoding="utf-8")

    pattern = re.compile(r"^## \[" + re.escape(version) + r"\]", re.MULTILINE)
    match = pattern.search(text)
    if match is None:
        print(
            "release_notes: no '## [%s]' section found in CHANGELOG.md" % version,
            file=sys.stderr,
        )
        return 1

    start = match.start()
    next_heading = re.search(r"^## \[", text[match.end():], re.MULTILINE)
    if next_heading is not None:
        section = text[start : match.end() + next_heading.start()]
    else:
        section = text[start:]

    # Drop heading links and footnote link definitions from the section body.
    section = re.sub(r"^\[[^\]]+\]:\s.*$", "", section, flags=re.MULTILINE).strip()

    footer = """

## Using this release in Defold

Add the release as a project dependency in `game.project`:

```
[project]
dependencies = https://github.com/{repo}/archive/refs/tags/{tag}.zip
```

The equivalent release asset is
`https://github.com/{repo}/releases/download/{tag}/{name}-{version}.zip` and
refers to the same tracked content. Integrity checksums are provided in
the release assets.

The Discord Social SDK binaries are not part of the zip. Fetch them with
`tools/fetch_discord_sdk.sh` or `.bat` (see the README's "Installing the SDK
binaries" section) and stage them into `defolddiscordsdk/res/<platform>/`, or
point the `[defolddiscordsdk].lib_path` setting at your copy.
""".strip("\n")

    body = section + "\n\n" + footer.format(
        repo=repo, tag=tag, version=version, name="DefoldDiscordSDK"
    )
    sys.stdout.write(body)
    return 0


if __name__ == "__main__":
    sys.exit(main())
