#!/bin/sh
# Drive deploy/.adds/hanzi/run.sh against a fake Kobo.
#
# This script has put the real device into a blank screen three times, each
# time through an edit that looked right: running the nickel binary bare,
# deleting the block that stops Nickel, a guard that never fired. Nothing
# ran it before it reached the device. Now every check does.
#
# The fakes: pidof/killall/reboot/udevadm on PATH, a nickel that records how
# it was started, and a /proc holding Nickel's environment.

SRC="$(cd "$(dirname "$0")/.." && pwd)/deploy/.adds/hanzi/run.sh"
fail=0

# Only under dash. BusyBox's sh -- the Kobo's own -- runs its built-in
# pidof, killall, reboot and ifconfig ahead of anything on PATH, so the
# fakes below are bypassed and the real commands run on this machine. It
# happened once; as a normal user they failed, but never again.
if [ -n "$BB_ASH_VERSION" ]; then
    echo "    refusing: this is BusyBox sh -- run with dash (sh tests/run_sh_test.sh)"
    exit 1
fi
RUNSH=$(command -v dash || true)
if [ -z "$RUNSH" ]; then
    echo "    refusing: needs dash, whose commands come from PATH (the fakes)"
    exit 1
fi

run_case() {   # name, fake-nickel behaviour, extra env...
    name="$1"; behave="$2"; shift 2
    T="$(mktemp -d)"
    R="$T/root"; B="$T/bin"; D="$T/app"; S="$T/state"
    mkdir -p "$R/proc/4242" "$R/proc/77" "$R/usr/local/Kobo" "$R/etc/init.d" \
             "$R/tmp" "$B" "$D" "$S"
    cp "$SRC" "$D/run.sh"
    [ -n "$BIGLOG" ] && head -c 600000 /dev/zero | tr '\000' x > "$D/hanzi.log"

    # Nickel's environment at launch, NUL-separated as /proc gives it.
    printf 'NICKEL_HOME=/mnt/onboard/.kobo\000LANG=en_US.UTF-8\000WIFI_MODULE=wlan_drv_gen4m\000INTERFACE=wlan0\000SECRET_JUNK=1\000' \
        > "$R/proc/4242/environ"
    printf 'PLATFORM=mt8113t-ntx\000PRODUCT=monza\000' > "$R/proc/77/environ"
    # On MediaTek the Wi-Fi module is loaded whether Wi-Fi is on or not.
    echo "wlan_drv_gen4m 1000 0 - Live 0x0" > "$R/proc/modules"
    mkdir -p "$R/dev" "$R/etc"
    echo 1 > "$R/dev/wmtWifi"
    echo "nameserver 192.168.1.1" > "$R/etc/resolv.conf"
    [ -n "$WIFI" ] && touch "$S/wifi_up"
    touch "$S/nickel_up"                       # Nickel is up at launch

    cat > "$B/pidof" <<P
#!/bin/sh
case "\$*" in
  *nickel*) [ -e "$S/nickel_up" ] && { echo 4242; exit 0; }; exit 1 ;;
  *udevd*)  echo 77; exit 0 ;;
esac
exit 1
P
    cat > "$B/killall" <<P
#!/bin/sh
echo "KILLALL \$*" >> "$S/calls"
case "\$*" in *nickel*) rm -f "$S/nickel_up" ;; esac
P
    printf '#!/bin/sh\necho REBOOT >> "%s/calls"\n' "$S" > "$B/reboot"
    cat > "$B/ifconfig" <<P
#!/bin/sh
case "\$*" in
  *down*) echo "IFCONFIG \$*" >> "$S/calls"; rm -f "$S/wifi_up" ;;
  *) [ -e "$S/wifi_up" ] && echo "wlan0  UP BROADCAST RUNNING MULTICAST"
     [ -e "$S/wifi_up" ] || echo "wlan0  BROADCAST MULTICAST" ;;
esac
P
    printf '#!/bin/sh\necho "WPA_CLI $*" >> "%s/calls"\n' "$S" > "$B/wpa_cli"
    printf '#!/bin/sh\necho "UDEVADM $*" >> "%s/calls"\n' "$S" > "$B/udevadm"
    printf '#!/bin/sh\necho ANIMATOR >> "%s/calls"\n' "$S" > "$R/etc/init.d/on-animator.sh"
    printf '#!/bin/sh\necho HINDENBURG >> "%s/calls"\n' "$S" > "$R/usr/local/Kobo/hindenburg"
    cat > "$R/usr/local/Kobo/nickel" <<P
#!/bin/sh
echo "NICKEL \$*" >> "$S/calls"
env > "$S/nickel_env"
case "$behave" in
  ok)       touch "$S/nickel_up" ;;
  never)    : ;;
  dies)     touch "$S/nickel_up"; ( sleep 1.5; rm -f "$S/nickel_up" ) & ;;
esac
P
    cat > "$D/hanzi" <<P
#!/bin/sh
echo "APP RAN" >> "$S/calls"
exit \${FAKE_APP_EXIT:-0}
P
    chmod +x "$B"/* "$R/etc/init.d/on-animator.sh" "$R/usr/local/Kobo/"* "$D/hanzi"

    env PATH="$B:$PATH" HANZI_TEST_ROOT="$R" HANZI_TEST_SETTLE=2 "$@" \
        "$RUNSH" "$D/run.sh" >/dev/null 2>&1

    C="$S/calls"; touch "$C"
    starts=$(grep -c '^NICKEL ' "$C"); reboots=$(grep -c '^REBOOT' "$C")
    echo "    $name: nickel started $starts time(s), rebooted $reboots time(s)"
    CASE_T="$T"; CASE_C="$C"; CASE_S="$S"; CASE_R="$R"; CASE_D="$D"
}

bad() { echo "      FAIL: $*"; fail=1; }

# 1. The normal exit: Nickel comes back, the way rcS starts it.
run_case "exit, nickel comes back" ok
grep -q "KILLALL -q -TERM nickel" "$CASE_C" || bad "nickel was not stopped at launch"
[ "$(grep -n 'KILLALL -q -TERM nickel' "$CASE_C" | cut -d: -f1)" -lt \
  "$(grep -n 'APP RAN' "$CASE_C" | cut -d: -f1)" ] || bad "nickel was stopped after the app started"
[ "$starts" = 1 ] || bad "nickel started $starts times, not once"
[ "$reboots" = 0 ] || bad "rebooted although nickel came back"
grep -q "^NICKEL -platform kobo -skipFontLoad" "$CASE_C" || bad "nickel started without -platform kobo"
grep -q "^HINDENBURG" "$CASE_C" || bad "hindenburg not started"
grep -q "^ANIMATOR" "$CASE_C" || bad "on-animator.sh not run"
grep -q "^UDEVADM trigger" "$CASE_C" || bad "udev not triggered"
[ -p "$CASE_R/tmp/nickel-hardware-status" ] || bad "the hardware-status FIFO was not made"
E="$CASE_S/nickel_env"
grep -q "^NICKEL_HOME=/mnt/onboard/.kobo$" "$E" || bad "NICKEL_HOME not carried over"
grep -q "^LANG=en_US.UTF-8$" "$E" || bad "LANG not carried over"
grep -q "^PRODUCT=monza$" "$E" || bad "PRODUCT (from udevd) not carried over"
grep -q "^LD_LIBRARY_PATH=/usr/local/Kobo$" "$E" || bad "LD_LIBRARY_PATH not set"
grep -q "^SECRET_JUNK" "$E" && bad "carried over a variable it should not have"
grep -q "nickel is back after" "$CASE_D/hanzi.log" || bad "log does not say nickel is back"

# 2. The watchdog killed the app: same ending.
run_case "watchdog exit" ok FAKE_APP_EXIT=3
[ "$starts" = 1 ] && [ "$reboots" = 0 ] || bad "a watchdog exit did not bring nickel back"

# 3. Nickel never starts: reboot, once.
run_case "nickel never starts" never
[ "$reboots" = 1 ] || bad "did not reboot once"

# 4. Nickel starts, then quits -- what bare ./nickel did. Must not be believed.
run_case "nickel starts then dies" dies
[ "$reboots" = 1 ] || bad "believed a nickel that died"
grep -q "started and then exited" "$CASE_D/hanzi.log" || bad "log does not say it died"

# 5. Wi-Fi on at launch: turned off, the MediaTek way, and Nickel still comes
#    back -- no reboot. The app never uses the network.
WIFI=1 run_case "wifi on at launch" ok
grep -q "^WPA_CLI -i wlan0 terminate" "$CASE_C" || bad "wpa_supplicant not stopped"
grep -q "^IFCONFIG wlan0 down" "$CASE_C" || bad "interface not taken down"
[ "$(cat "$CASE_R/dev/wmtWifi")" = 0 ] || bad "radio not powered off through /dev/wmtWifi"
grep -q "nameserver 192.168.1.1" "$CASE_R/etc/resolv.conf" || bad "resolv.conf lost"
[ "$(grep -n 'IFCONFIG wlan0 down' "$CASE_C" | head -1 | cut -d: -f1)" -lt \
  "$(grep -n 'APP RAN' "$CASE_C" | cut -d: -f1)" ] || bad "wifi turned off only after the app ran"
[ "$starts" = 1 ] && [ "$reboots" = 0 ] || bad "wifi on at launch still rebooted on exit"

# 7. The module is always loaded on MediaTek. That alone must not reboot:
#    it did, on every exit, in the version before this one.
run_case "wifi off, module loaded" ok
grep -q "^IFCONFIG" "$CASE_C" && bad "touched an interface that was already down"
[ "$starts" = 1 ] && [ "$reboots" = 0 ] || bad "a loaded module with wifi off rebooted"

# 6. Asked to reboot.
run_case "HANZI_REBOOT_ON_EXIT" ok HANZI_REBOOT_ON_EXIT=1
[ "$starts" = 0 ] && [ "$reboots" = 1 ] || bad "HANZI_REBOOT_ON_EXIT not honoured"

# 8. A log grown past half a megabyte is moved aside, not appended to forever.
BIGLOG=1 run_case "log past its cap" ok
[ "$(wc -c < "$CASE_D/hanzi.log")" -lt 100000 ] || bad "the big log was appended to"
[ "$(wc -c < "$CASE_D/hanzi-prev.log")" -ge 600000 ] || bad "the big log was not kept as hanzi-prev.log"
[ "$starts" = 1 ] && [ "$reboots" = 0 ] || bad "rotating the log changed the exit"

[ "$fail" = 0 ] && echo "    -> run.sh stops nickel, and brings it back or reboots"
exit "$fail"
