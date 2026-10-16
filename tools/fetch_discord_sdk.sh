#!/usr/bin/env bash
# --------------------------------------------------------------------
# fetch_discord_sdk.sh
#
# Stage the Discord Social SDK binaries into the `defolddiscordsdk` Defold
# extension so the native SDK can be loaded at build time. Windows users:
# use the twin script `fetch_discord_sdk.bat`.
#
# Usage:
#   tools/fetch_discord_sdk.sh [PATH]
#   tools/fetch_discord_sdk.sh --src PATH --dest PATH [--mobile]
#
# PATH is either:
#   (a) an already extracted SDK folder (default: ./discord_social_sdk in
#       the repository root), or
#   (b) a .zip archive of the SDK, unpacked to a temporary directory first.
#
# Options:
#   --src PATH    SDK source: folder or .zip; overrides the positional arg
#   --dest PATH   extension destination folder (default: ./defolddiscordsdk)
#   --mobile      also stage arm64-android (.aar) and arm64-ios (.xcframework)
#                 artifacts when the archive carries them (future phases;
#                 default off)
#   -h, --help    this help text
#
# Exit codes:
#   0  at least one core Discord partner SDK binary was staged
#   1  staging finished but no core partner binary is present (see table)
#   2  usage error (bad arguments, missing source, unsafe destination)
#
# The script is idempotent: rerunning simply refreshes the staged files.
# --------------------------------------------------------------------

set -u

PROGRAM="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

SRC=""
DEST=""
MOBILE=0

usage() {
	cat <<EOF
Usage: ${PROGRAM} [PATH] [--src PATH] [--dest PATH] [--mobile]

Stage Discord Social SDK binaries into the defolddiscordsdk extension.

  PATH         extracted SDK folder (default: ${REPO_ROOT}/discord_social_sdk)
               or a .zip archive, unpacked to a temporary directory first.

  --src PATH   SDK source (folder or .zip); overrides the positional argument
  --dest PATH  destination extension folder (default: ${REPO_ROOT}/defolddiscordsdk)
  --mobile     also stage arm64-android and arm64-ios artifacts when present
  -h, --help   show this help

Exits 0 only when at least one core Discord partner SDK binary was staged.
EOF
}

# ------------------------------------------------------------------
# Argument parsing
# ------------------------------------------------------------------
while [ $# -gt 0 ]; do
	case "$1" in
		--src)
			if [ $# -lt 2 ]; then echo "error: --src needs a value" >&2; usage >&2; exit 2; fi
			SRC="$2"; shift 2 ;;
		--dest)
			if [ $# -lt 2 ]; then echo "error: --dest needs a value" >&2; usage >&2; exit 2; fi
			DEST="$2"; shift 2 ;;
		--mobile)
			MOBILE=1; shift ;;
		-h|--help)
			usage; exit 0 ;;
		--)
			shift; break ;;
		-*)
			echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
		*)
			if [ -z "$SRC" ]; then
				SRC="$1"; shift
			else
				echo "error: unexpected argument: $1" >&2; usage >&2; exit 2
			fi ;;
	esac
done
[ -z "$SRC" ] && SRC="$REPO_ROOT/discord_social_sdk"
[ -z "$DEST" ] && DEST="$REPO_ROOT/defolddiscordsdk"

# ------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------
print_row() {
	# $1 platform, $2 destination file (relative to res/), $3 status, $4 source
	printf '  %-13s %-38s %-8s %s\n' "$1" "$2" "$3" "$4"
}

# Resolves the SDK root: accepts the plain layout (bin/ and lib/ at the top)
# or one nested folder (zips that wrap everything in discord_social_sdk/).
find_sdk_root() {
	local base="$1" candidate
	for candidate in "$base" "$base"/*/; do
		if [ -d "$candidate/bin" ] || [ -d "$candidate/lib" ]; then
			printf '%s' "${candidate%/}"
			return 0
		fi
	done
	return 1
}

# ------------------------------------------------------------------
# Validate source
# ------------------------------------------------------------------
EXTRACT_DIR=""

cleanup() {
	if [ -n "$EXTRACT_DIR" ] && [ -d "$EXTRACT_DIR" ]; then
		rm -rf "$EXTRACT_DIR"
	fi
}
trap cleanup EXIT

SRC_EXT="${SRC##*.}"
if [ -f "$SRC" ] && [ "$SRC_EXT" = "zip" -o "$SRC_EXT" = "ZIP" ]; then
	if ! command -v unzip >/dev/null 2>&1; then
		echo "error: unzip is required to read .zip archives" >&2
		exit 2
	fi
	EXTRACT_DIR="$(mktemp -d 2>/dev/null || mktemp -d /tmp/defolddiscordsdk.XXXXXX)"
	echo "Unpacking $SRC -> $EXTRACT_DIR"
	if ! unzip -q "$SRC" -d "$EXTRACT_DIR"; then
		echo "error: failed to unpack $SRC" >&2
		exit 2
	fi
	SRC="$EXTRACT_DIR"
elif [ -d "$SRC" ]; then
	SRC="$(cd "$SRC" 2>/dev/null && pwd)" || { echo "error: cannot read source folder: $SRC" >&2; exit 2; }
else
	echo "error: source is neither an extracted SDK folder nor a .zip: $SRC" >&2
	usage >&2
	exit 2
fi

if ! SDK_ROOT="$(find_sdk_root "$SRC")"; then
	echo "error: no SDK layout (bin/ or lib/ folders) under: $SRC" >&2
	echo "       point --src at the extracted Discord Social SDK archive." >&2
	exit 2
fi

# ------------------------------------------------------------------
# Validate destination
# ------------------------------------------------------------------
case "$DEST" in
	/*) ;;
	*) DEST="$REPO_ROOT/$DEST" ;;
esac
if [ "$DEST" = "$REPO_ROOT" ] || [ "$DEST" = "$REPO_ROOT/tools" ] || [ "$DEST" = "$SCRIPT_DIR" ]; then
	echo "error: unsafe destination '$DEST'; pass --dest pointing at the extension folder" >&2
	exit 2
fi
if [ -d "$(pwd)" ] && [ "$(pwd -P)" = "$DEST" ]; then
	echo "error: refuse to run from inside the destination folder ($DEST)" >&2
	echo "       run from the repository root instead." >&2
	exit 2
fi

mkdir -p "$DEST/res" || { echo "error: cannot create $DEST/res" >&2; exit 2; }

echo "SDK root:  $SDK_ROOT"
echo "Staging into: $DEST/res"
echo ""
echo "Staged artifacts (destination relative to $DEST/res):"
print_row "platform" "file" "status" "source"
print_row "-------" "----" "------" "------"

CORE_COUNTED=0
MISSING_COUNTED=0
FAILED_COUNTED=0

# Stages the first existing candidate for a list of file names.
# $1 destination subdir, $2 space separated candidate names (in preference
# order), $3.. candidate source dirs relative to SDK_ROOT.
stage_artifact() {
	local dest_sub="$1" names="$2" dir rel name src target
	shift 2
	for name in $names; do
		for rel in "$@"; do
			src="$SDK_ROOT/$rel/$name"
			if [ -f "$src" ]; then
				mkdir -p "$DEST/res/$dest_sub"
				if cp -f "$src" "$DEST/res/$dest_sub/$name"; then
					print_row "$dest_sub" "$name" "staged" "$rel/$name"
					case "$name" in
						*partner_sdk*) CORE_COUNTED=$((CORE_COUNTED+1)) ;;
					esac
					return 0
				else
					print_row "$dest_sub" "$name" "FAILED" "$rel/$name"
					FAILED_COUNTED=$((FAILED_COUNTED+1))
					return 1
				fi
			elif [ -d "$src" ]; then
				target="$DEST/res/$dest_sub/$name"
				rm -rf "$target"
				mkdir -p "$DEST/res/$dest_sub"
				if cp -R "$src" "$target"; then
					print_row "$dest_sub" "$name/" "staged" "$rel/$name"
					case "$name" in
						*partner_sdk*) CORE_COUNTED=$((CORE_COUNTED+1)) ;;
					esac
					return 0
				else
					print_row "$dest_sub" "$name/" "FAILED" "$rel/$name"
					FAILED_COUNTED=$((FAILED_COUNTED+1))
					return 1
				fi
			fi
		done
	done
	print_row "$dest_sub" "$names" "missing" "-"
	MISSING_COUNTED=$((MISSING_COUNTED+1))
	return 1
}

# Stages all *.kef krisp model files from the first candidate dir that has any.
stage_kefs() {
	local dest_sub="$1" dir rel name f
	shift
	local staged=0
	for rel in "$@"; do
		dir="$SDK_ROOT/$rel"
		for f in "$dir"/*.kef; do
			if [ -f "$f" ]; then
				mkdir -p "$DEST/res/$dest_sub"
				name="$(basename "$f")"
				if cp -f "$f" "$DEST/res/$dest_sub/$name"; then
					print_row "$dest_sub" "$name" "staged" "$rel/$name"
					staged=1
				else
					print_row "$dest_sub" "$name" "FAILED" "$rel/$name"
					FAILED_COUNTED=$((FAILED_COUNTED+1))
				fi
			fi
		done
		if [ "$staged" -eq 1 ]; then
			return 0
		fi
	done
	print_row "$dest_sub" "*.kef" "missing" "-"
	MISSING_COUNTED=$((MISSING_COUNTED+1))
	return 1
}

MAC_HINT=0

# ------------------------------------------------------------------
# Desktop platforms
# ------------------------------------------------------------------

# Windows x86_64: dlls and krisp models live in bin/release (bin/debug fallback).
stage_artifact "x86_64-win32" "discord_partner_sdk.dll" "bin/release" "bin/debug" "bin/release/arm64" "bin/debug/arm64"
stage_artifact "x86_64-win32" "discord_krisp.dll"       "bin/release" "bin/debug"
stage_kefs     "x86_64-win32" "bin/release" "bin/debug"

# macOS: prefer the loose dylibs at the archive lib root (release, then debug).
stage_artifact "x86_64-osx" "libdiscord_partner_sdk.dylib" "lib/release" "lib/debug" "bin/release/arm64" "bin/debug/arm64" || MAC_HINT=1
stage_artifact "x86_64-osx" "libdiscord_krisp.dylib"      "lib/release" "lib/debug"
stage_kefs     "x86_64-osx" "lib/release" "lib/debug"

# Linux: partner library only. No krisp library ships for Linux in the
# current archive; handle the absence gracefully (nothing staged, no error).
stage_artifact "x86_64-linux" "libdiscord_partner_sdk.so" "lib/release" "lib/debug"

# ------------------------------------------------------------------
# Mobile (optional, behind --mobile)
# ------------------------------------------------------------------
if [ "$MOBILE" = "1" ]; then
	stage_artifact "arm64-android" "discord_partner_sdk.aar" "lib/release" "lib/debug"
	stage_artifact "arm64-ios"     "discord_partner_sdk.xcframework" "lib/release" "lib/debug"
fi

# ------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------
echo ""
if [ "$MAC_HINT" -eq 1 ]; then
	echo "note: no loose macOS dylib was found in this archive."
	echo "      If your download only ships macOS slices inside an XCFramework,"
	echo "      copy the Mach-O binary yourself, for example:"
	echo "        <sdk>/discord_partner_sdk.xcframework/macos-*/discord_partner_sdk.framework/discord_partner_sdk"
	echo "        -> $DEST/res/x86_64-osx/libdiscord_partner_sdk.dylib"
	echo "      Staging continues with whatever files do exist."
	echo ""
fi
echo "Summary: $CORE_COUNTED core partner staging hit(s), $MISSING_COUNTED missing, $FAILED_COUNTED failed."

# Decide the exit code from what actually landed on disk.
CORE_STAGED=0
if [ -f "$DEST/res/x86_64-win32/discord_partner_sdk.dll" ]; then
	CORE_STAGED=1
fi
if [ -f "$DEST/res/x86_64-osx/libdiscord_partner_sdk.dylib" ]; then
	CORE_STAGED=1
fi
if [ -f "$DEST/res/x86_64-linux/libdiscord_partner_sdk.so" ]; then
	CORE_STAGED=1
fi
if [ "$MOBILE" = "1" ]; then
	if [ -f "$DEST/res/arm64-android/discord_partner_sdk.aar" ]; then
		CORE_STAGED=1
	fi
	if [ -d "$DEST/res/arm64-ios/discord_partner_sdk.xcframework" ]; then
		CORE_STAGED=1
	fi
fi

if [ "$CORE_STAGED" -eq 1 ]; then
	echo "The Discord partner SDK core is staged. Binaries stay untracked (see .gitignore)."
	exit 0
fi

echo "No core Discord partner SDK binary staged."
echo "Check that $SRC really is the extracted Discord Social SDK archive"
echo "(needs bin/ and/or lib/ directories)."
exit 1
