# WEP on the GL-AR300M16 — Test Build of wpad

Part of [Lab #10](../../README.md). Commands that start with `router/` run from the `lab10/` folder.

> [!WARNING]
> **For testing only.** WEP is broken encryption: anyone in range can recover the key in minutes.
> Restore WPA2 when the test is over (section 4).

## Table of Contents
- [1. Why the Stock Router Cannot Do WEP](#1-why-the-stock-router-cannot-do-wep)
- [2. Build](#2-build)
- [3. Try It on the Router](#3-try-it-on-the-router)
- [4. Undo](#4-undo)
- [5. Files](#5-files)

---

## <a id="1-why-the-stock-router-cannot-do-wep"></a>1. Why the Stock Router Cannot Do WEP

hostapd, the program that runs the access point, is part of the `wpad-openssl` package. On GL.iNet
firmware 4.3.27 to 4.3.30 it is version `2022-01-16-cff80b4f-18.2`, on OpenWrt 22.03.4
(`r20123-38ccc47687`, `ath79/nand`). It was compiled without WEP. Given a WEP config, it refuses to
start and the radio stays down:

```
hostapd: Line 34: unknown configuration item 'wep_key0'
hostapd: Line 35: unknown configuration item 'wep_default_key'
netifd: Device setup failed: HOSTAPD_START_FAILED
```

WEP is a build option, `CONFIG_WPA_ENABLE_WEP`, and it is off by default in OpenWrt 22.03. OpenWrt's
own `wpad-openssl` packages are built without it too, so installing them does not help. The
Wi-Fi driver (ath9k) supports WEP, and so does OpenWrt's config script, which writes the
`wep_key0` lines.

[`build.sh`](build.sh) builds the same package from OpenWrt's SDK for that release, with WEP on.
It stops when the built snapshot is not GL's (`2022-01-16-cff80b4f`) or when WEP did not get compiled in. The release number differs: the SDK gives `1.2`, GL's firmware says `18.2`.

## <a id="2-build"></a>2. Build

The SDK runs on x86_64 Linux only, and it needs Python's `distutils`. This laptop is x86_64, but
Fedora 44 has no `distutils`, so `build.sh` stops at its check here. Two ways:

**GitHub Actions (no setup).** [`build-wpad-wep.yml`](../../../.github/workflows/build-wpad-wep.yml)
runs on every push to `main` that changes `build.sh` or the workflow, or by hand: *Actions →
build-wpad-wep → Run workflow*. It takes about 5 minutes. The result is the release
`wpad-wep-22.03.4` of this repository, which downloads without a GitHub login. On this laptop:

```bash
mkdir -p ~/wpad-wep && cd ~/wpad-wep
base=https://github.com/<owner>/labsNetworking/releases/download/wpad-wep-22.03.4
curl -fL -O "$base/wpad" -O "$base/SHA256SUMS"
sha256sum -c SHA256SUMS --ignore-missing
```

Replace `<owner>` with the GitHub account in `git remote get-url origin`.

**Local build (not set up).** `build.sh` needs Ubuntu 22.04, or an Ubuntu 22.04 container or VM,
with the packages listed at the top of the script. A local build writes `out/` next to the script,
and that folder is gitignored.

## <a id="3-try-it-on-the-router"></a>3. Try It on the Router

[`try-on-router.sh`](try-on-router.sh) runs on this laptop. It copies `wpad` to the router's RAM
(`/tmp`) and mounts it over `/usr/sbin/wpad`. Nothing is written to flash, and a reboot brings
GL's wpad back. It first checks that the router runs the version the build is for. Wi-Fi drops
for a few seconds while hostapd restarts.

The script logs in with SSH keys only (`BatchMode`). Set up a key once, and type the router's
password once when `ssh-copy-id` asks for it:

```bash
ssh-keygen -t ed25519          # only if ~/.ssh has no key yet
ssh-copy-id root@your-router-ip
```

```bash
router/wifi-wep-setup/try-on-router.sh install ~/wpad-wep/wpad
```

Then switch the access point to WEP with [`../wifi-wep-test.sh`](../wifi-wep-test.sh). It backs up
`/etc/config/wireless` first, and it fails when hostapd does not actually broadcast:

```bash
scp router/wifi-wep-test.sh root@your-router-ip:/tmp/
ssh root@your-router-ip 'SSID=Test sh /tmp/wifi-wep-test.sh <13 ASCII characters or 26 hex digits>'
```

The script's last line gives the command that restores the WPA2 settings.

Installing the `.ipk` with `opkg` would keep WEP across reboots, but it writes to flash and replaces
GL's `wpad-openssl`. Its `Depends` also pins `hostapd-common` to the same `1.2` version, which this
build does not produce. Not needed for a test.

## <a id="4-undo"></a>4. Undo

Restore WPA2 first, with the `cp ... && wifi reload` line `wifi-wep-test.sh` printed. Then:

```bash
router/wifi-wep-setup/try-on-router.sh undo
```

or reboot the router.

## <a id="5-files"></a>5. Files

| File | Runs on | What it does |
| :--- | :--- | :--- |
| [`build.sh`](build.sh) | x86_64 Linux, Ubuntu 22.04 | Downloads and checks the SDK, builds `wpad-openssl` with WEP, checks version and WEP, writes `out/` |
| [`try-on-router.sh`](try-on-router.sh) | This laptop | `install <wpad>`: runs it from the router's RAM. `undo`: back to GL's wpad |
| [`../wifi-wep-test.sh`](../wifi-wep-test.sh) | The router | Switches the access point to WEP and checks that hostapd broadcasts it |
| [`build-wpad-wep.yml`](../../../.github/workflows/build-wpad-wep.yml) | GitHub Actions | Runs `build.sh` and publishes the release `wpad-wep-22.03.4` |
