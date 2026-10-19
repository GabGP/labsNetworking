#!/bin/sh
# Runs a WEP-enabled wpad on the GL-AR300M16 until it reboots, or puts GL.iNet's back.
#
#   try-on-router.sh install <wpad>   copy <wpad> to the router's RAM and mount it over
#                                     /usr/sbin/wpad; nothing is written to flash
#   try-on-router.sh undo             unmount it, so GL.iNet's wpad runs again
#
# Runs on the laptop (or any machine with SSH to the router). A reboot also undoes install.
# The Wi-Fi settings are not touched: run ../wifi-wep-test.sh on the router afterwards.
set -eu

ROUTER=${ROUTER:-root@your-router-ip}
SSH_OPTIONS=${SSH_OPTIONS:--o BatchMode=yes -o ConnectTimeout=10}
# What GL.iNet 4.3.x ships; the router must run exactly this version.
EXPECTED_VERSION=2022-01-16-cff80b4f-18.2

die() { echo "try-on-router.sh: $*" >&2; exit 1; }
on_router() { ssh $SSH_OPTIONS "$ROUTER" "$@"; }

# Runs on the router with MODE=install or MODE=undo. hostapd runs from /usr/sbin/wpad, so it
# is stopped before that file is unmounted or replaced, and always started again: a failed
# swap leaves the router on whichever wpad is in place, with Wi-Fi back up. Then it waits for
# netifd: radios up, or given up on (hostapd refused the config).
swap_wpad='
failed=
/etc/init.d/wpad stop
sleep 1
if grep -q " /usr/sbin/wpad " /proc/mounts && ! umount /usr/sbin/wpad; then
    echo "error: could not unmount /usr/sbin/wpad" >&2
    failed=1
fi
if [ -z "$failed" ]; then
    if [ "$MODE" = install ]; then
        mv /tmp/wpad-wep.new /tmp/wpad-wep && mount -o bind /tmp/wpad-wep /usr/sbin/wpad || failed=1
    else
        rm -f /tmp/wpad-wep
    fi
fi
/etc/init.d/wpad start
sleep 2
wifi up
i=0
while [ $i -lt 20 ]; do
    sleep 1
    i=$((i + 1))
    status=$(ubus call network.wireless status)
    echo "$status" | grep -q "\"retry_setup_failed\": true" && break
    echo "$status" | grep -q "\"up\": false" || break
done
echo "after ${i} s:"
ubus call network.wireless status | grep -E "\"up\"|retry_setup_failed"
for object in $(ubus list "hostapd.*"); do
    echo "$object: $(ubus call "$object" get_status | jsonfilter -e @.status -e @.ssid | tr "\n" " ")"
done
grep -q " /usr/sbin/wpad " /proc/mounts && echo "running: the wpad in RAM" || echo "running: GL.iNet wpad"
[ -z "$failed" ]
'

case ${1:-} in
    install)
        [ $# -eq 2 ] || die "usage: $0 install <wpad>"
        [ -f "$2" ] || die "$2: no such file"
        command -v strings >/dev/null && { strings "$2" | grep -qx wep_default_key || die "$2 has no WEP support"; }
        router_version=$(on_router "opkg info wpad-openssl" | sed -n 's/^Version: //p')
        [ "$router_version" = "$EXPECTED_VERSION" ] ||
            die "the router runs wpad-openssl '$router_version'; this build is for $EXPECTED_VERSION"
        on_router "cat >/tmp/wpad-wep.new && chmod 755 /tmp/wpad-wep.new" <"$2"
        on_router "MODE=install; $swap_wpad" || die "install failed; see above"
        echo "Undo: $0 undo (or reboot the router)."
        ;;
    undo)
        on_router "MODE=undo; $swap_wpad" || die "undo failed; see above"
        ;;
    *) die "usage: $0 install <wpad> | undo" ;;
esac
