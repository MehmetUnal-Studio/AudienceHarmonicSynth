#!/usr/bin/env bash
#
# check_generated_data.sh — verify Source/ElementSpectralData.cpp is in sync with
# its generator (tools/generate_element_spectral_data.py) and the input data/*.txt.
#
# The generator hardcodes its output to <repo>/Source/ElementSpectralData.{h,cpp}.
# To avoid clobbering the committed files, this script runs the generator against a
# TEMPORARY repo root (with data/ symlinked in) and diffs the freshly generated
# .cpp against the committed one. It NEVER writes to the real Source/ directory.
#
# Exit status:
#   0  committed file matches generator output
#   1  drift detected (committed file is stale — re-run the generator and commit)
#   2  the generator could not be run (missing python3 / inputs)
#
# Usage:  tools/check_generated_data.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

GENERATOR="$REPO_ROOT/tools/generate_element_spectral_data.py"
COMMITTED="$REPO_ROOT/Source/ElementSpectralData.cpp"

if ! command -v python3 >/dev/null 2>&1; then
  echo "check_generated_data: python3 not found on PATH" >&2
  exit 2
fi
if [[ ! -f "$GENERATOR" ]]; then
  echo "check_generated_data: generator not found at $GENERATOR" >&2
  exit 2
fi
if [[ ! -f "$COMMITTED" ]]; then
  echo "check_generated_data: committed file not found at $COMMITTED" >&2
  exit 2
fi
if [[ ! -d "$REPO_ROOT/data" ]]; then
  echo "check_generated_data: input data directory not found at $REPO_ROOT/data" >&2
  exit 2
fi

# Build a throwaway repo layout the generator expects: <tmp>/tools, <tmp>/data, <tmp>/Source.
# The generator derives its root as parents[1] of the script, so place the script under tmp/tools.
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/specgen.XXXXXX")"
cleanup() { rm -rf "$TMP_ROOT"; }
trap cleanup EXIT

mkdir -p "$TMP_ROOT/tools" "$TMP_ROOT/Source"
ln -s "$REPO_ROOT/data" "$TMP_ROOT/data"
cp "$GENERATOR" "$TMP_ROOT/tools/"

# Regenerate into the temp tree (writes <tmp>/Source/ElementSpectralData.{h,cpp}).
if ! python3 "$TMP_ROOT/tools/$(basename "$GENERATOR")" >/dev/null; then
  echo "check_generated_data: generator failed to run" >&2
  exit 2
fi

GENERATED="$TMP_ROOT/Source/ElementSpectralData.cpp"

if diff -u "$COMMITTED" "$GENERATED"; then
  echo "check_generated_data: OK — Source/ElementSpectralData.cpp is in sync with the generator."
  exit 0
else
  echo "" >&2
  echo "check_generated_data: DRIFT — Source/ElementSpectralData.cpp does not match generator output." >&2
  echo "  Re-run: python3 tools/generate_element_spectral_data.py   then commit the result." >&2
  exit 1
fi
