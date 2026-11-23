#!/bin/sh
# hanzi launcher, started by NickelMenu (or KFMon) from inside Nickel.
#
# Nickel, the Kobo UI, is stopped while the app runs -- the app takes the
# framebuffer, and with Nickel still up the two draw over each other -- and
# started again when the app exits.
#
# Starting Nickel again is not a matter of running its binary. This firmware
# (4.45 on the Libra Colour) has no nickel.sh; at boot it is rcS that starts
# Nickel. What rcS does, and what KOReader's own nickel.sh repeats to come
# back from KOReader, is: give Nickel back its environment, recreate the FIFO
# udev writes hardware events into, start hindenburg, and start nickel with
# `-platform kobo`. Without that flag Qt has no display to draw on -- which is
# why running the binary bare left a blank screen.
#
# If Nickel still does not come back, the device reboots: slower, but it
# always ends at a working Kobo. And if the screen is ever stuck, holding the
# power button reboots it; nothing here survives that.
#
# tests/run_sh_test.sh drives this whole file against a fake device.

# Empty on a Kobo. The test points it at a fake root.
R="${HANZI_TEST_ROOT:-}"
# A launcher's PATH is not ours to rely on. Appended, so the test's fakes win.
PATH="$PATH:/sbin:/bin:/usr/sbin:/usr/bin"

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR" || exit 1
LOG="$DIR/hanzi.log"
NICKEL_ENV="$DIR/.nickel-env"

mkdir -p import export

# .adds is on a FAT filesystem, so the execute bit may not have survived the
# copy from a computer. Set it here rather than failing silently.
chmod +x ./hanzi 2>/dev/null || true

# Minutes without input before the app sleeps by itself (0 = never):
# export HANZI_AUTOSLEEP_MIN=10
# Reboot on exit instead of starting Nickel again:
# export HANZI_REBOOT_ON_EXIT=1
# If the pen draws in the wrong place (see README, Troubleshooting):
# export HANZI_PEN_SWAP=1
# export HANZI_PEN_MIRRORX=1
# export HANZI_PEN_MIRRORY=1
# Raw input logging, a line per pen sample. Slow, and fills the card.
# export HANZI_INPUT_DEBUG=1
# Waveform for ordinary redraws: du4 (default), du, gl16, gc16fast, gc16
# export HANZI_WFM_PART=du4
# Waveform for ink: du4 (default), a2, du, gl16, gc16fast
# export HANZI_WFM_INK=du4

# Every run appends to the log, and nothing else ever trims it: past half a
# megabyte -- a few hundred sessions -- it becomes hanzi-prev.log, replacing
# the one before. The app itself starts the log again past a megabyte
# within one session, so the two together stay under about two megabytes.
if [ -f "$LOG" ] && [ "$(wc -c < "$LOG")" -gt 524288 ]; then
    mv -f "$LOG" "$DIR/hanzi-prev.log"
fi

echo "--- $(date) ---" >> "$LOG"

# ---- stop Nickel ---------------------------------------------------------
#
# First keep what it was started with, to start it with again: rcS exports
# these after a launcher like KFMon is already running, so its children lack
# them. (NickelMenu is spawned by Nickel and inherits them; keeping is harmless.)
# The same list KOReader takes, plus the two it reads from udevd.
NPID="$(pidof -s nickel 2>/dev/null)"
: > "$NICKEL_ENV"
if [ -n "$NPID" ] && [ -r "$R/proc/$NPID/environ" ]; then
    tr '\000' '\n' < "$R/proc/$NPID/environ" \
        | grep -E '^(DBUS_SESSION_BUS_ADDRESS|NICKEL_HOME|WIFI_MODULE|LANG|INTERFACE|PLATFORM|PRODUCT)=' \
        >> "$NICKEL_ENV"
    echo "nickel is running (pid $NPID); kept $(wc -l < "$NICKEL_ENV") of its settings" >> "$LOG"
else
    echo "note: no nickel pid found; stopping it anyway" >> "$LOG"
fi
UPID="$(pidof -s udevd 2>/dev/null)"
if [ -n "$UPID" ] && [ -r "$R/proc/$UPID/environ" ]; then
    tr '\000' '\n' < "$R/proc/$UPID/environ" \
        | grep -E '^(PLATFORM|PRODUCT)=' >> "$NICKEL_ENV"
fi

# Unconditional: killing something that is not running costs nothing, and
# failing to kill something that is leaves both drawing on one screen.
echo "stopping nickel" >> "$LOG"
killall -q -TERM nickel hindenburg sickel fickel strickel fontickel \
    adobehost foxitpdf iink dhcpcd-dbus dhcpcd 2>/dev/null
t=0
while pidof nickel >/dev/null 2>&1; do
    sleep 1
    t=$((t + 1))
    if [ "$t" -ge 15 ]; then
        echo "nickel would not exit; killing" >> "$LOG"
        killall -q -KILL nickel 2>/dev/null
        break
    fi
done
echo "nickel stopped after ${t}s" >> "$LOG"

# ---- Wi-Fi off -------------------------------------------------------------
#
# Nothing here uses the network. Left on, it keeps the device from sleeping --
# suspend fails with EPERM, a driver voting no, and KOReader takes Wi-Fi down
# before every suspend for that reason -- and Nickel does not come back well
# over it. Nickel turns it on again itself if you had it on.
#
# KOReader's sequence for this chip (wlan_drv_gen4m, the MediaTek Kobos):
# stop the DHCP client and wpa_supplicant, take the interface down, and power
# the radio off through /dev/wmtWifi. The module is never unloaded on MTK --
# it stays in /proc/modules with Wi-Fi off, which is why checking for the
# module rebooted on every exit.
wifi_off() {
    ifc="$(grep '^INTERFACE=' "$NICKEL_ENV" | cut -d= -f2)"
    wm="$(grep '^WIFI_MODULE=' "$NICKEL_ENV" | cut -d= -f2)"
    [ -n "$ifc" ] || ifc=wlan0
    ifconfig "$ifc" 2>/dev/null | grep -q UP || return 0

    # A DHCP client letting go of its lease can empty resolv.conf, and Nickel
    # does not always write it again.
    [ -e "$R/etc/resolv.conf" ] && cp -a "$R/etc/resolv.conf" "$R/tmp/resolv.hanzi"
    killall -q -TERM udhcpc default.script dhcpcd 2>/dev/null
    wpa_cli -i "$ifc" terminate >/dev/null 2>&1
    ifconfig "$ifc" down >/dev/null 2>&1
    if [ "$wm" = wlan_drv_gen4m ] && [ -e "$R/dev/wmtWifi" ]; then
        echo 0 > "$R/dev/wmtWifi" 2>/dev/null
    fi
    if [ -e "$R/tmp/resolv.hanzi" ]; then
        cmp -s "$R/tmp/resolv.hanzi" "$R/etc/resolv.conf" \
            || cp -a "$R/tmp/resolv.hanzi" "$R/etc/resolv.conf"
        rm -f "$R/tmp/resolv.hanzi"
    fi
    echo "wifi off (${wm:-unknown module} on $ifc): not used here, and it" \
         "keeps the device from sleeping" >> "$LOG"
}
wifi_off

# ---- start it again ------------------------------------------------------

reboot_now() {
    # Anything half-started would paint over the reboot.
    killall -q -KILL nickel hindenburg 2>/dev/null
    echo "rebooting to bring the interface back" >> "$LOG"
    sync
    reboot
}

# 0 if Nickel is back and staying up, 1 if not.
start_nickel() {
    cd / || return 1

    if [ -r "$NICKEL_ENV" ]; then
        while IFS= read -r kv; do
            case "$kv" in *=*) export "$kv" ;; esac
        done < "$NICKEL_ENV"
    fi
    export LD_LIBRARY_PATH=/usr/local/Kobo
    export QT_GSTREAMER_PLAYBIN_AUDIOSINK=alsasink
    export QT_GSTREAMER_PLAYBIN_AUDIOSINK_DEVICE_PARAMETER=bluealsa:DEV=00:00:00:00:00:00

    # Off at launch, but make sure: Nickel does not come back well over it.
    wifi_off

    # An SD card left mounted makes Nickel show an "unrecognised" popup.
    if [ -e "$R/dev/mmcblk1p1" ]; then
        umount "$R/mnt/sd" 2>/dev/null
    fi

    # The boot animation, and what brings the launcher hooks back.
    if [ -e "$R/etc/init.d/on-animator.sh" ]; then
        "$R/etc/init.d/on-animator.sh" >/dev/null 2>&1 &
    fi

    if [ -e "$R/etc/init.d/z-nickel-hardware-status" ]; then
        echo "starting nickel (firmware 5)" >> "$LOG"
        unset LD_LIBRARY_PATH
        "$R/etc/init.d/z-nickel-hardware-status"
        sync
        "$R/etc/rc.local"
    else
        echo "starting nickel (firmware 4)" >> "$LOG"
        # udev writes hardware events into this, and Nickel reads them.
        rm -f "$R/tmp/nickel-hardware-status"
        mkfifo "$R/tmp/nickel-hardware-status"
        sync
        "$R/usr/local/Kobo/hindenburg" >/dev/null 2>&1 &
        LIBC_FATAL_STDERR_=1 "$R/usr/local/Kobo/nickel" \
            -platform kobo -skipFontLoad >/dev/null 2>&1 &
        [ "$PLATFORM" != "freescale" ] && udevadm trigger >/dev/null 2>&1 &
    fi

    t=0
    while [ "$t" -lt 20 ]; do
        sleep 1
        t=$((t + 1))
        pidof nickel >/dev/null 2>&1 && break
    done
    if ! pidof nickel >/dev/null 2>&1; then
        echo "nickel did not start" >> "$LOG"
        return 1
    fi
    # Started is not enough: Qt without its platform starts and then quits,
    # which is how the last attempt fooled this script. Watch it a while.
    settle="${HANZI_TEST_SETTLE:-6}"
    sleep "$settle"
    if pidof nickel >/dev/null 2>&1; then
        echo "nickel is back after $((t + settle))s" >> "$LOG"
        return 0
    fi
    echo "nickel started and then exited" >> "$LOG"
    return 1
}

# Called from the trap and again at the end, so that the app exiting and
# the app being killed -- by the watchdog, say -- end in the same place.
# Must be safe to run twice: two Nickels on one framebuffer need a reboot.
RESTORED=0
restore_nickel() {
    [ "$RESTORED" = 0 ] || return 0
    RESTORED=1
    if pidof nickel >/dev/null 2>&1; then
        echo "nickel is already running" >> "$LOG"
        return 0
    fi
    if [ "${HANZI_REBOOT_ON_EXIT:-0}" = 1 ]; then
        echo "HANZI_REBOOT_ON_EXIT is set" >> "$LOG"
        reboot_now
        return 0
    fi
    start_nickel || reboot_now
}
trap restore_nickel EXIT INT TERM HUP

./hanzi --content content.db --user user.db --font LXGWWenKaiLite-Regular.ttf >> "$LOG" 2>&1
echo "exit: $?" >> "$LOG"

restore_nickel
