#!/bin/bash -e
# Build the PiPedal UI and stage it on the Pi for the Supr console-face
# panels, then refresh ~/supr-deploy.sh on the Pi to match the new bundle.
#
# Run from anywhere:      bash path/to/pipedal/vite/restage-supr-ui.sh
# Afterwards, on the Pi:  ~/supr-deploy.sh   (needs sudo)
#
# Set PI to your own account and host, e.g.
#   PI=pi@mypedal.local bash restage-supr-ui.sh
# Key auth is used when it is available (ssh-copy-id), which is much faster;
# otherwise the script falls back to password ssh and prompts per connection.

cd "$(dirname "$0")"

echo "== building =="
npm run build
rm -f dist/assets/*.js.gz
for f in dist/assets/*.js; do gzip -c "$f" > "$f.gz"; done

MAIN_GZ=$(ls dist/assets/main-*.js.gz | head -1)
MAIN_JS=${MAIN_GZ%.gz}
BASE=$(basename "$MAIN_JS")
echo "== bundle: $BASE =="

PI="${PI:-pi@raspberrypi.local}"

if ssh -o BatchMode=yes -o ConnectTimeout=8 $PI true 2>/dev/null; then
    echo "== key auth available: scp direct =="
    ssh $PI 'mkdir -p ~/supr-ui-staged/assets'
    scp "$MAIN_GZ" dist/index.html $PI:~/supr-ui-staged/
    ssh $PI "mv ~/supr-ui-staged/$(basename "$MAIN_GZ") ~/supr-ui-staged/assets/ && gunzip -kf ~/supr-ui-staged/assets/$(basename "$MAIN_GZ")"
else
    echo "== no ssh key: falling back to password ssh (you'll be prompted) =="
    ssh -o PubkeyAuthentication=no $PI 'mkdir -p ~/supr-ui-staged/assets && cat > /tmp/supr-main.js.gz.b64' < <(base64 -i "$MAIN_GZ")
    ssh -o PubkeyAuthentication=no $PI "base64 -d /tmp/supr-main.js.gz.b64 > ~/supr-ui-staged/assets/$(basename "$MAIN_GZ") && gunzip -kf ~/supr-ui-staged/assets/$(basename "$MAIN_GZ")"
    ssh -o PubkeyAuthentication=no $PI 'cat > ~/supr-ui-staged/index.html' < dist/index.html
fi

echo "== refreshing ~/supr-deploy.sh on the Pi =="
ssh ${SSH_OPTS:-} $PI "printf '%s\n' \
'#!/bin/bash -e' \
'# Supr pedals + console-panel UI deploy' \
'cd ~/SuprPedals && sudo make install' \
'sudo cp ~/supr-ui-staged/index.html /etc/pipedal/react/index.html' \
'sudo cp ~/supr-ui-staged/assets/${BASE} ~/supr-ui-staged/assets/${BASE}.gz /etc/pipedal/react/assets/' \
'sudo systemctl restart pipedald' \
'echo Done - hard-refresh the browser' > ~/supr-deploy.sh && chmod +x ~/supr-deploy.sh"

echo
echo "Staged. Now run on the Pi:   ~/supr-deploy.sh"
