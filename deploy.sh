#!/bin/bash
# deploy.sh — Build et déploie mod-pi-touch sur le Pi, redémarre le service.
set -e

PI=pistomp@192.168.0.19
BUILD_DIR=/home/pistomp/mod-pi-touch

echo "==> Sync sources..."
rsync -av --exclude=build/ --exclude=build_dbg/ \
    /home/pilal/dev/mod-pitouch/mod-pi-touch/ "$PI:$BUILD_DIR/"

echo "==> Build on Pi..."
ssh "$PI" "cd $BUILD_DIR && cmake --build build -j4"

echo "==> Install sudoers rules (WiFi + power + JACK)..."
ssh "$PI" "echo 'pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli dev wifi rescan *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli dev wifi connect *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli dev wifi hotspot *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli con up *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli con down *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli con delete *
pistomp ALL=(ALL) NOPASSWD: /bin/systemctl poweroff
pistomp ALL=(ALL) NOPASSWD: /bin/systemctl reboot
pistomp ALL=(ALL) NOPASSWD: /bin/cp /tmp/jackdrc_new /etc/jackdrc
pistomp ALL=(ALL) NOPASSWD: /bin/systemctl restart jack' | sudo tee /etc/sudoers.d/mod-pi-touch-wifi > /dev/null && sudo chmod 0440 /etc/sudoers.d/mod-pi-touch-wifi"

echo "==> Deploy..."
ssh "$PI" "sudo systemctl stop mod-pi-touch; sudo cp $BUILD_DIR/build/mod-pi-touch /usr/local/bin/mod-pi-touch; sudo systemctl start mod-pi-touch"

echo "==> Status:"
ssh "$PI" "systemctl status mod-pi-touch --no-pager | head -8"
