#!/bin/sh
# Build one app package warning-free: out/pkg/<id>.gzp, checked by glaze-sdk.
# usage (from the glaze-apps root): .claude/skills/glaze-build/scripts/build_app.sh <id>
# Prints only errors/warnings (full log: out/build-<id>.log). Exit 0 = clean build.
set -u
id=${1:?usage: build_app.sh <id>}
[ -d "packages/$id" ] || { echo "build_app: no packages/$id (run from the glaze-apps root)" >&2; exit 2; }
sdk=${GLAZE_SDK:-../glaze-os/out/sdk/glaze-sdk}
[ -f "$sdk/share/app.mk" ] || { echo "build_app: no SDK at $sdk; run 'make sdk' (or ./build.sh sdk) in ../glaze-os" >&2; exit 2; }
mkdir -p out
log=out/build-$id.log
if command -v mipsel-linux-gnu-gcc >/dev/null 2>&1; then
    make "out/pkg/$id.gzp" >"$log" 2>&1
else
    ./build.sh "out/pkg/$id.gzp" >"$log" 2>&1
fi
status=$?
problems=$(grep -E 'error|warning:|undefined reference|No rule|Error [0-9]|FAIL' "$log" | head -40)
if [ $status -ne 0 ]; then
    echo "BUILD FAILED ($log):"
    echo "${problems:-$(tail -20 "$log")}"
    exit 1
fi
if grep -q 'warning:' "$log"; then
    echo "BUILD HAS WARNINGS (not allowed; fix them):"
    echo "$problems"
    exit 1
fi
echo "OK: out/pkg/$id.gzp ($(wc -c <"out/pkg/$id.gzp" | tr -d ' ') bytes, glaze-sdk check passed)"
