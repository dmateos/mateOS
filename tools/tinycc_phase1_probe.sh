#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SRC_DIR="${1:-/tmp/tinycc}"
OUT_FILE="${ROOT_DIR}/userland/tinycc/PHASE1_GAP.md"

if [ ! -d "$SRC_DIR" ] || [ ! -f "$SRC_DIR/tcc.c" ]; then
    echo "tinycc source not found at: $SRC_DIR" >&2
    echo "usage: $0 [/path/to/tinycc]" >&2
    exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

echo "[tinycc-phase1] source: $SRC_DIR"

if [ ! -f "$SRC_DIR/tcc" ] || [ ! -f "$SRC_DIR/libtcc.a" ]; then
    echo "[tinycc-phase1] building host tinycc..."
    (
        cd "$SRC_DIR"
        ./configure >/dev/null
        make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null
    )
fi

echo "[tinycc-phase1] building mateOS userland symbol providers..."
make -C "${ROOT_DIR}/userland" libc.o syscalls.o >/dev/null

echo "[tinycc-phase1] collecting symbol sets..."
find "$SRC_DIR" -maxdepth 2 -type f -name '*.o' >"$TMP_DIR/tcc_objs.list"
if [ ! -s "$TMP_DIR/tcc_objs.list" ]; then
    echo "no tinycc objects found under: $SRC_DIR" >&2
    exit 1
fi

xargs nm -g --defined-only <"$TMP_DIR/tcc_objs.list" \
    | awk 'NF >= 3 && $2 != "U" { print $3 }' \
    | sed 's/@.*$//' \
    | LC_ALL=C sort -u >"$TMP_DIR/tcc_defined.txt"

xargs nm -u <"$TMP_DIR/tcc_objs.list" \
    | awk 'NF >= 2 { print $NF }' \
    | sed 's/@.*$//' \
    | LC_ALL=C sort -u >"$TMP_DIR/tcc_undefined_raw.txt"

comm -23 "$TMP_DIR/tcc_undefined_raw.txt" "$TMP_DIR/tcc_defined.txt" >"$TMP_DIR/tcc_external.txt"

nm -g --defined-only "${ROOT_DIR}/userland/libc.o" "${ROOT_DIR}/userland/syscalls.o" \
    | awk 'NF >= 3 && $2 != "U" { print $3 }' \
    | LC_ALL=C sort -u >"$TMP_DIR/mate_exports.txt"

comm -12 "$TMP_DIR/tcc_external.txt" "$TMP_DIR/mate_exports.txt" >"$TMP_DIR/already_present.txt"
comm -23 "$TMP_DIR/tcc_external.txt" "$TMP_DIR/mate_exports.txt" >"$TMP_DIR/missing_symbols.txt"

echo "[tinycc-phase1] collecting header gaps..."
find "$SRC_DIR" -maxdepth 2 -type f \( -name '*.c' -o -name '*.h' \) >"$TMP_DIR/tcc_sources.list"
xargs rg -No '#include <[^>]+>' <"$TMP_DIR/tcc_sources.list" \
    | sed -E 's/.*<([^>]+)>.*/\1/' \
    | LC_ALL=C sort -u >"$TMP_DIR/tcc_headers.txt"

{
    (
        cd "${ROOT_DIR}/userland/include"
        find . -type f -name '*.h' | sed 's#^\./##'
    ) || true
    (
        cd "${ROOT_DIR}/userland/smallerc/include"
        find . -maxdepth 1 -type f -name '*.h' | sed 's#^\./##'
    ) || true
} | LC_ALL=C sort -u >"$TMP_DIR/mate_headers.txt"

comm -23 "$TMP_DIR/tcc_headers.txt" "$TMP_DIR/mate_headers.txt" >"$TMP_DIR/missing_headers.txt"
comm -12 "$TMP_DIR/tcc_headers.txt" "$TMP_DIR/mate_headers.txt" >"$TMP_DIR/present_headers.txt"

mkdir -p "$(dirname "$OUT_FILE")"
{
    echo "# TinyCC Phase 1 Gap Report"
    echo
    echo "- Source: \`$SRC_DIR\`"
    echo "- Generated: $(date -u '+%Y-%m-%d %H:%M:%SZ')"
    echo
    echo "## Summary"
    echo
    echo "- TinyCC external symbols: $(wc -l <"$TMP_DIR/tcc_external.txt" | tr -d ' ')"
    echo "- Already provided by mateOS userland (libc.o + syscalls.o): $(wc -l <"$TMP_DIR/already_present.txt" | tr -d ' ')"
    echo "- Missing symbols to implement/shim: $(wc -l <"$TMP_DIR/missing_symbols.txt" | tr -d ' ')"
    echo "- TinyCC requested libc headers: $(wc -l <"$TMP_DIR/tcc_headers.txt" | tr -d ' ')"
    echo "- Missing headers vs userland/include + userland/smallerc/include: $(wc -l <"$TMP_DIR/missing_headers.txt" | tr -d ' ')"
    echo
    echo "## Missing Headers"
    echo
    echo '```'
    cat "$TMP_DIR/missing_headers.txt"
    echo '```'
    echo
    echo "## Missing Symbols"
    echo
    echo '```'
    cat "$TMP_DIR/missing_symbols.txt"
    echo '```'
    echo
    echo "## Already Present Symbols"
    echo
    echo '```'
    cat "$TMP_DIR/already_present.txt"
    echo '```'
} >"$OUT_FILE"

echo "[tinycc-phase1] wrote: $OUT_FILE"
