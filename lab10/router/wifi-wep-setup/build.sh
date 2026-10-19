#!/usr/bin/env bash
# Builds wpad-openssl with WEP for the GL.iNet GL-AR300M16 router.
#
# GL.iNet firmware 4.3.27 to 4.3.30 is OpenWrt 22.03.4 (r20123, ath79/nand) with
# wpad-openssl 2022-01-16-cff80b4f-18.2, built without WEP. Neither GL's nor OpenWrt's package
# turns on CONFIG_WPA_ENABLE_WEP, so hostapd refuses 'wep_key0'. This builds the same package
# from OpenWrt's own SDK for that release, with WEP on. Details: README.md next to this file.
#
# Runs on x86_64 Linux: Ubuntu 22.04 (GitHub Actions container, or an Ubuntu 22.04 container).
# Ubuntu 24.04 and Fedora 44 do not work: OpenWrt 22.03 needs Python's distutils, which Python
# 3.12 and later no longer have.
#   sudo apt-get install build-essential gawk gettext git libncurses-dev python3-distutils \
#       rsync unzip wget xz-utils zstd file binutils
#   ./build.sh                 # results in ./out (OUT_DIR), SDK cached in WORK_DIR
set -euo pipefail

SDK_NAME=openwrt-sdk-22.03.4-ath79-nand_gcc-11.2.0_musl.Linux-x86_64
SDK_URL=https://downloads.openwrt.org/releases/22.03.4/targets/ath79/nand/$SDK_NAME.tar.xz
# From https://downloads.openwrt.org/releases/22.03.4/targets/ath79/nand/sha256sums
SDK_SHA256=25eb5618f05facf173f8890df2d84b95f0e0e4ca5ed8dbde0b4af3c953defe2e
# What GL.iNet 4.3.x ships; the router's other packages expect exactly this version.
EXPECTED_VERSION=2022-01-16-cff80b4f-18.2

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
WORK_DIR=${WORK_DIR:-$HOME/.cache/wpad-wep}
OUT_DIR=${OUT_DIR:-$SCRIPT_DIR/out}
JOBS=${JOBS:-$(nproc)}

die() { echo "build.sh: $*" >&2; exit 1; }

# ---- preflight ---------------------------------------------------------------------------
[ "$(uname -m)" = x86_64 ] || die "OpenWrt's SDK runs on x86_64 only (this is $(uname -m))"
case $WORK_DIR in
    /mnt/*) die "WORK_DIR=$WORK_DIR is on the Windows drive; the build needs a case-sensitive Linux filesystem (use the default under \$HOME)" ;;
esac
python3 -c 'import distutils' 2>/dev/null ||
    die "Python's distutils is missing; use Ubuntu 22.04 and install python3-distutils"
for tool in gawk make gcc g++ git rsync unzip wget xz zstd file strings; do
    command -v "$tool" >/dev/null || die "$tool not found; install the packages listed at the top of this script"
done

# ---- SDK ---------------------------------------------------------------------------------
mkdir -p "$WORK_DIR" "$OUT_DIR"
SDK_ARCHIVE=$WORK_DIR/$SDK_NAME.tar.xz
SDK_DIR=$WORK_DIR/$SDK_NAME
if [ ! -d "$SDK_DIR" ]; then
    [ -f "$SDK_ARCHIVE" ] || wget -nv -O "$SDK_ARCHIVE" "$SDK_URL"
    echo "$SDK_SHA256  $SDK_ARCHIVE" | sha256sum -c - || { rm -f "$SDK_ARCHIVE"; die "SDK checksum mismatch"; }
    tar -xJf "$SDK_ARCHIVE" -C "$WORK_DIR"
fi
cd "$SDK_DIR"

# hostapd and its libraries (openssl, ubus, ubox, libnl-tiny) are all in the base feed, which
# the SDK pins to the v22.03.4 tag.
./scripts/feeds update base
./scripts/feeds install hostapd

# Only wpad-openssl, with WEP. Without the ALL_* lines the SDK builds every hostapd variant.
cat >.config <<'EOF'
# CONFIG_ALL is not set
# CONFIG_ALL_NONSHARED is not set
# CONFIG_ALL_KMODS is not set
# CONFIG_SIGNED_PACKAGES is not set
CONFIG_PACKAGE_wpad-openssl=m
CONFIG_WPA_ENABLE_WEP=y
EOF
make defconfig >/dev/null
grep -qx 'CONFIG_WPA_ENABLE_WEP=y' .config || die "make defconfig dropped CONFIG_WPA_ENABLE_WEP"
grep -qx 'CONFIG_PACKAGE_wpad-openssl=m' .config || die "make defconfig dropped wpad-openssl"

make -j"$JOBS" package/hostapd/clean >/dev/null
make -j"$JOBS" package/hostapd/compile || make -j1 V=s package/hostapd/compile

# ---- check and collect -------------------------------------------------------------------
IPK=$(find bin/packages -name 'wpad-openssl_*.ipk' | head -1)
[ -n "$IPK" ] || die "no wpad-openssl package was built"
EXTRACT_DIR=$(mktemp -d)
trap 'rm -rf "$EXTRACT_DIR"' EXIT
tar -xzf "$IPK" -C "$EXTRACT_DIR"
tar -xzf "$EXTRACT_DIR/control.tar.gz" -C "$EXTRACT_DIR"
tar -xzf "$EXTRACT_DIR/data.tar.gz" -C "$EXTRACT_DIR"

version=$(sed -n 's/^Version: //p' "$EXTRACT_DIR/control")
[ "$version" = "$EXPECTED_VERSION" ] || die "built $version, but GL.iNet 4.3.x ships $EXPECTED_VERSION"
# 'wep_default_key' is a hostapd config item that only exists when WEP is compiled in.
strings "$EXTRACT_DIR/usr/sbin/wpad" | grep -qx wep_default_key || die "the built wpad has no WEP support"

rm -f "$OUT_DIR"/wpad-openssl_*.ipk "$OUT_DIR/wpad" "$OUT_DIR/SHA256SUMS"
cp "$IPK" "$OUT_DIR/"
cp "$EXTRACT_DIR/usr/sbin/wpad" "$OUT_DIR/wpad"
(cd "$OUT_DIR" && sha256sum wpad wpad-openssl_*.ipk >SHA256SUMS)
echo "Built wpad-openssl $version with WEP:"
cat "$OUT_DIR/SHA256SUMS"
