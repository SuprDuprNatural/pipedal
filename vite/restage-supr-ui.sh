#!/usr/bin/env bash
set -euo pipefail
# Stage the entire fresh UI. Installation/restart and live-board preservation
# belong to the coordinated deployment; this script does not restart audio.
# PI=pi@device.local bash vite/restage-supr-ui.sh
# For a verified hostname alias: export RSYNC_RSH='ssh -o HostKeyAlias=pi4'
cd "$(dirname "$0")"
npm run build
compgen -G 'dist/assets/main-*.js' >/dev/null || { echo 'No main bundle' >&2; exit 1; }
for file in dist/assets/*.js; do gzip -c "$file" > "$file.gz"; done
PI="${PI:-pi@raspberrypi.local}"
rsync -az --delete dist/ "$PI:supr-ui-staged/"
echo 'Complete UI staged in ~/supr-ui-staged. Back up the live board before installing and restarting.'
