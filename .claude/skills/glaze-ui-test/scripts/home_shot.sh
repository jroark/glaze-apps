#!/bin/sh
# Screenshot the unlocked home screen of the current OS image (no card).
# Use it to find where a newly installed app's icon will land, and whether
# Files' root has an Applications row above Card (it does when the image
# already has an installed app, e.g. Maps on the home screen).
# usage (from the glaze-apps root): .claude/skills/glaze-ui-test/scripts/home_shot.sh
set -eu
glaze=${GLAZE_DIR:-../glaze-os}
mkdir -p out
printf '# Home-screen probe\n3000 drag 40 280 235 280 500\n' > out/t_home.txt
png=$("$glaze/tests/run.sh" out/t_home.txt 14 | tail -1)
echo "SCREENSHOT: $png   (open it: icons sit on a 60 px grid, centres x=30,90,150,210; y=48,120,192 in script coordinates)"
