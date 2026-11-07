#!/bin/sh
# Run a scripted emulator test of one app and report what happened.
# usage (from the glaze-apps root):
#   .claude/skills/glaze-ui-test/scripts/ui_test.sh <id> [script] [seconds]
# script defaults to tests/ui/t_<id>.txt; seconds defaults to the script's last
# event + 8 s (the guest clock lags the host by a few seconds). Builds the package,
# puts only it on a fresh card out/<id>-cf.img, boots it with the script, then
# prints the screenshot path, any faults, and the app's printf lines.
# FS_FROM=out/shots/t_<id>/glaze.bin reuses a previous run's flash (resume tests;
# the card is still inserted).
set -u
id=${1:?usage: ui_test.sh <id> [script] [seconds]}
script=${2:-tests/ui/t_$id.txt}
secs=${3:-}
here=$(dirname "$0")
glaze=${GLAZE_DIR:-../glaze-os}
[ -f "$script" ] || { echo "ui_test: no $script; write it first (see glaze-ui-test)" >&2; exit 2; }
last=$(awk '/^[0-9]/ {t=$1; if ($2=="drag") t+=$7} END {print int(t/1000)+1}' "$script")
min=$((last + 8))
if [ -z "$secs" ]; then secs=$min
elif [ "$secs" -lt "$min" ]; then echo "ui_test: warning: ${secs}s may end before the last event (${last}s); use >= ${min}" >&2; fi
"$here/../../glaze-build/scripts/build_app.sh" "$id" || exit 1
rm -f "out/$id-cf.img"
"$glaze/tools/mkcfimg.sh" "out/$id-cf.img" 16 16 "out/pkg/$id.gzp" >/dev/null || { echo "ui_test: mkcfimg failed" >&2; exit 1; }
echo "running $script for ${secs}s (rebuilds the OS image first; ~40 s+)..."
png=$(CF="out/$id-cf.img" "$glaze/tests/run.sh" "$script" "$secs" | tail -1)
name=$(basename "$script" .txt)
log=$glaze/out/shots/$name/stdout.log
[ -f "$log" ] || log=out/shots/$name/stdout.log
echo "SCREENSHOT: $png   (open it and check it)"
echo "LAUNCH:"
if grep -q "sb: launch /Applications/$id.app" "$log"; then echo "  $id launched"
else echo "  $id NEVER LAUNCHED. Launches seen:"; grep 'sb: launch' "$log" | sed 's/^/    /'
     echo "  Check the install taps (see glaze-ui-test) or a stale OS image (rm ../glaze-os/out/fs.img)."; fi
echo "FAULTS:"
faults=$(grep -E '(^|[^a-z])fault|PANIC|exception' "$log" | head -10)
echo "${faults:-  none}"
echo "APP OUTPUT (lines starting '$id:', last 25):"
grep "^$id:" "$log" | tail -25
