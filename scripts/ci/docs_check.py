#!/usr/bin/env python3
# Documentation hygiene for markdown files and game.project.
# 1. Every tracked markdown file is pure ASCII (no emojis, no typographic
#    replacements). Violations are reported as file:line.
# 2. Relative markdown links must resolve to files inside the repository.
# 3. Markdown code fences must be balanced.
# 4. game.project: the extension section may only contain whitelisted keys and
#    [library] include_dirs must point at a real extension folder.
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent

EXCLUDED_DIRS = {".git", ".internal", "discord_social_sdk", "build", "node_modules"}

# game.project section keys we allow for the extension. Anything else is either
# a typo or a setting the extension does not read.
SECTION_NAMES = {"defolddiscordsdk"}
WHITELISTED_KEYS = {
    "application_id",
    "lib_path",
    "steam_app_id",
    "default_redirect",
    "scopes",
    "auto_register_launch",
}

LINK_RE = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")


def iter_markdown():
    for path in sorted(REPO.rglob("*.md")):
        parts = set(path.relative_to(REPO).parts)
        if parts & EXCLUDED_DIRS:
            continue
        yield path


def check_ascii(path, problems):
    data = path.read_bytes()
    for lineno, line in enumerate(data.splitlines(), start=1):
        try:
            line.decode("ascii")
        except UnicodeDecodeError:
            first_bad = next(i for i in line if i > 0x7F)
            problems.append(
                f"{path.relative_to(REPO)}:{lineno}: non-ascii byte 0x{first_bad:02x}"
            )


def check_fences(path, problems):
    in_fence = False
    for lineno, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        stripped = line.lstrip()
        if stripped.startswith("```") or stripped.startswith("~~~"):
            in_fence = not in_fence
            fence_marker = stripped[:3]
            if in_fence and not re.fullmatch(r"`{3,}|~{3,}[a-zA-Z0-9_+-]*", stripped) and fence_marker not in ("```", "~~~"):
                problems.append(f"{path.relative_to(REPO)}:{lineno}: malformed fence opener")
    if in_fence:
        problems.append(f"{path.relative_to(REPO)}: unterminated code fence")


def resolve_target(path, target):
    if re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("//"):
        return None  # absolute URI, not checked here
    target = target.split("#", 1)[0].split("?", 1)[0]
    if not target:
        return None  # pure anchor link
    return path.parent / target


def check_links(path, problems):
    text = path.read_text(encoding="utf-8", errors="replace")
    text_no_code = re.sub(r"```.*?```", "", text, flags=re.DOTALL)
    text_no_code = re.sub(r"`[^`]*`", "", text_no_code)
    for lineno, line in enumerate(text_no_code.splitlines(), start=1):
        for target in LINK_RE.findall(line):
            resolved = resolve_target(path, target)
            if resolved is None:
                continue
            if not resolved.exists():
                problems.append(
                    f"{path.relative_to(REPO)}:{lineno}: relative link target not found: {target}"
                )


def parse_game_project(problems):
    gp = REPO / "game.project"
    if not gp.exists():
        problems.append("game.project: file missing")
        return
    section = None
    include_dirs_ok = False
    for lineno, raw in enumerate(gp.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if raw.lstrip().startswith("[") and "]" in raw:
            name = raw.split("[", 1)[1].split("]", 1)[0].strip()
            section = name.lower()
            continue
        if section is None or "=" not in line:
            continue
        key, _, value = line.partition("=")
        key = key.strip().lower()
        value = value.strip()
        if section in SECTION_NAMES:
            if key not in WHITELISTED_KEYS:
                problems.append(
                    f"game.project:{lineno}: unknown key '{key}' in [{section}]; "
                    f"allowed: {', '.join(sorted(WHITELISTED_KEYS))}"
                )
        if section == "library" and key == "include_dirs":
            candidates = {t for t in value.split() if t}
            if candidates & SECTION_NAMES:
                cfg_name = next(iter(candidates & SECTION_NAMES))
                if (REPO / cfg_name).is_dir():
                    include_dirs_ok = True
                else:
                    problems.append(
                        f"game.project:{lineno}: [library] include_dirs names '{cfg_name}' "
                        "but that folder does not exist"
                    )
            else:
                problems.append(
                    f"game.project:{lineno}: [library] include_dirs must list the extension folder "
                    f"({'or'.join(sorted(SECTION_NAMES))}), got '{value}'"
                )
    if not include_dirs_ok:
        problems.append("game.project: no [library] include_dirs entry naming the extension folder was validated")


def main():
    problems = []
    checked = 0
    for path in iter_markdown():
        checked += 1
        check_ascii(path, problems)
        check_fences(path, problems)
        check_links(path, problems)
    parse_game_project(problems)

    print(f"docs-quality: checked {checked} markdown file(s) plus game.project")
    if problems:
        print(f"docs-quality: {len(problems)} problem(s)")
        for p in problems:
            print(f"  {p}")
        return 1
    print("docs-quality: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
