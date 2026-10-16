#!/usr/bin/env python3
# License hygiene audit.
# Verifies that proprietary Discord Social SDK payloads never become tracked
# content: binary blobs must stay out of the tree (except inside the local,
# git-ignored discord_social_sdk/ working directory), the extension res
# directories may only contain plain-text placeholder files, and whenever a
# res tree holds staged binaries locally the ignore rules must cover it.
# The repo may not have git initialized yet (CI runs on GitHub, where it
# does), so instead of asking git we assert the intended end state directly.
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

EXCLUDED_DIRS = {".git", ".internal", "build", "node_modules"}
SDK_LOCAL_DIR = "discord_social_sdk"  # fetched binary SDK lives here, never tracked
EXTENSION_DIRS = ("defolddiscordsdk",)  # extension folder only

BINARY_SUFFIXES = {".dll", ".so", ".dylib", ".aar", ".kef", ".exe", ".lib", ".pdb"}


def is_text_file(path):
    """A file counts as text if it decodes as utf-8 and contains no NUL bytes."""
    data = path.read_bytes()
    if b"\x00" in data:
        return False
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return False
    return True


def all_entries():
    """Yield (rel_path, absolute_path) for every non-excluded filesystem entry."""
    for path in sorted(REPO.rglob("*")):
        rel = path.relative_to(REPO)
        parts = rel.parts
        if not parts:
            continue
        if set(parts) & EXCLUDED_DIRS:
            continue
        if parts[0] == SDK_LOCAL_DIR:
            continue  # explicitly allowed local-only location
        yield rel, path


def check_no_binaries(problems, binaries):
    for rel, path in all_entries():
        if path.is_dir():
            continue
        # res trees have their own dedicated check (staging there is allowed
        # when ignore rules cover it); do not double-report.
        if rel.parts[0] in EXTENSION_DIRS and "res" in rel.parts:
            continue
        if path.suffix.lower() in BINARY_SUFFIXES:
            problems.append(
                f"{rel}: proprietary-looking binary may not be committed "
                "(stage Discord SDK payloads only under discord_social_sdk/ "
                "or a locally git-ignored res folder)"
            )
            binaries += 1
    return binaries


def iter_res_dirs():
    """Extension res directories, pre- and post-rename layout."""
    for ext in EXTENSION_DIRS:
        res = REPO / ext / "res"
        if res.is_dir():
            yield res


def gitignore_covers_res(ext):
    """True if .gitignore ignores ext/res/** as fetched content."""
    gitignore = REPO / ".gitignore"
    if not gitignore.exists():
        return False
    text = gitignore.read_text(encoding="utf-8", errors="replace")
    pattern = re.escape(ext) + r"/res/\*\*"
    return re.search(r"(?m)^\s*/?" + pattern + r"/?(\s|#|$)", text) is not None


def check_res_trees(problems):
    """Flag binaries and non placeholder files inside extension res trees.

    Binaries in a res tree are acceptable only while the ignore rules cover
    the res tree (fetched content stays untracked); otherwise they will be
    committed at the next git init/add, which is exactly what this audit
    exists to prevent. Returns the flagged binary list.
    """
    flagged = []
    for res in iter_res_dirs():
        rel_res = res.relative_to(REPO)
        ext = rel_res.parts[0]
        covered = gitignore_covers_res(ext)
        staged_binaries = []
        for path in sorted(res.rglob("*")):
            rel = path.relative_to(REPO)
            if path.is_dir():
                continue
            if path.suffix.lower() in BINARY_SUFFIXES or path.suffix.lower() == ".xcframework":
                if covered:
                    continue  # locally staged only; ignore rules keep it untracked
                staged_binaries.append(rel)
                continue
            if path.name not in ("README.md", ".keep") and not path.name.lower().endswith(".txt"):
                problems.append(
                    f"{rel}: unexpected file type in res tree; only README.md, "
                    "*.txt and .keep placeholders may be tracked"
                )
                continue
            if not is_text_file(path):
                problems.append(f"{rel}: res placeholder file is not plain text")
        if staged_binaries:
            flagged.extend(staged_binaries)
            problems.extend(
                f"{rel}: Discord-proprietary binary in res tree is NOT covered by .gitignore "
                f"and would be committed; add '{ext}/res/**' ignore rules (keep the "
                "README.md/*.txt/.keep exceptions) before the first git commit"
                for rel in staged_binaries
            )
    return flagged


def check_gitignore(problems):
    gitignore = REPO / ".gitignore"
    if not gitignore.exists():
        problems.append(".gitignore: file missing; cannot confirm discord_social_sdk/ is ignored")
        return
    text = gitignore.read_text(encoding="utf-8", errors="replace")
    if not re.search(r"(?m)^\s*/?" + re.escape(SDK_LOCAL_DIR) + r"/?(\s|$)", text):
        problems.append(
            f".gitignore: no ignore rule for {SDK_LOCAL_DIR}/ "
            "(the fetched binary SDK must never be tracked)"
        )
    ext = next(iter(EXTENSION_DIRS), None)
    if (REPO / ext / "res").is_dir() and not re.search(
        r"(?m)^\s*/?" + re.escape(ext) + r"/res\b", text
    ):
        problems.append(
            f".gitignore: no ignore rule for {ext}/res (fetched binaries are staged there; "
            "the ignore pattern must follow the current extension folder name)"
        )


def check_dont_copied_notices(problems):
    for rel, path in all_entries():
        if path.is_dir():
            continue
        if re.search(r"license.?notices", path.name, re.IGNORECASE):
            problems.append(
                f"{rel}: Discord SDK notice file found outside {SDK_LOCAL_DIR}/"
            )


def check_stub_cleanliness(problems):
    for rel, path in all_entries():
        if path.is_dir() or rel.parts[0] != "test" or path.suffix not in {".h", ".hpp", ".c", ".cpp"}:
            continue
        data = path.read_bytes()
        if re.search(rb"Defold Foundation", data, re.IGNORECASE):
            problems.append(
                f"{rel}: stub header looks copied from Defold engine sources "
                "(mentions 'Defold Foundation'); stubs must be self-written only, "
                "see test/README.md"
            )


def main():
    problems = []
    binaries = 0
    binaries = check_no_binaries(problems, binaries)
    staged = check_res_trees(problems)
    check_gitignore(problems)
    check_dont_copied_notices(problems)
    check_stub_cleanliness(problems)

    notes = []
    if not list((REPO / SDK_LOCAL_DIR).glob("**/[Ll]icense-Notices.txt")):
        notes.append(f"no License-Notices.txt found under {SDK_LOCAL_DIR}/ (SDK not fetched locally; fine)")

    print(f"license-audit: {binaries} binary artifact(s) outside allowed locations, "
          f"{len(staged)} staged binary/binary group(s) in res trees, {len(problems)} problem(s)")
    for note in notes:
        print(f"note: {note}")
    if problems:
        for p in problems:
            print(f"  {p}")
        return 1
    print("license-audit: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
