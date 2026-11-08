#!/bin/bash
# Atrapa nmcli do testów panelu Sieć bez karty Wi-Fi (np. w Xvfb/kontenerze).
# Udaje kartę wlan0 i kilka sieci w zasięgu; resztę przekazuje do prawdziwego nmcli.
#   LISAOS_NMCLI=tools/atrapa-nmcli.sh lisaos-alice siec
# Stan: $ATRAPA_DIR (domyślnie /tmp/atrapa-nmcli): wifi (on/off), polaczona, zapisane
D=${ATRAPA_DIR:-/tmp/atrapa-nmcli}
mkdir -p "$D"; [ -f "$D/wifi" ] || echo on > "$D/wifi"; [ -f "$D/wwan" ] || echo on > "$D/wwan"
touch "$D/polaczona" "$D/zapisane" "$D/hotspot"
[ -s "$D/zapisane" ] || echo "Dom:tajne123" > "$D/zapisane"
REAL=$(command -v -p nmcli || echo /usr/bin/nmcli)
args="$*"
wifi=$(cat "$D/wifi"); con=$(cat "$D/polaczona")
siec() {  # in-use:ssid:signal:security:bssid:device
  cat <<'L'
Dom:82:WPA2:AA\:BB\:CC\:00\:00\:01
Kawiarnia Pod Kotem:64::AA\:BB\:CC\:00\:00\:02
FRITZ!Box 7530 ZX:47:WPA2 WPA3:AA\:BB\:CC\:00\:00\:03
Sąsiad/5G:28:WPA2:AA\:BB\:CC\:00\:00\:04
eduroam:55:WPA2 802.1X:AA\:BB\:CC\:00\:00\:05
Biuro:18:WPA3:AA\:BB\:CC\:00\:00\:06
L
}
saved() { cut -d: -f1 "$D/zapisane"; }
case "$args" in
  "-t -f WIFI,WWAN radio")
    w=disabled; [ "$wifi" = on ] && w=enabled; m=disabled; [ "$(cat "$D/wwan")" = on ] && m=enabled
    echo "$w:$m" ;;
  "radio all off") echo off > "$D/wifi"; echo off > "$D/wwan"; : > "$D/polaczona"; : > "$D/hotspot" ;;
  "radio all on") echo on > "$D/wifi"; echo on > "$D/wwan" ;;
  "radio wifi on") echo on > "$D/wifi" ;;
  "radio wifi off") echo off > "$D/wifi"; : > "$D/polaczona" ;;
  "-t -f DEVICE,TYPE,STATE,CONNECTION device")
    "$REAL" "$@"
    if [ "$wifi" != on ]; then echo "wlan0:wifi:unavailable:"
    elif [ -n "$con" ]; then echo "wlan0:wifi:connected:$con"
    else echo "wlan0:wifi:disconnected:"; fi ;;
  "-t -f NAME,UUID,TYPE,STATE connection show")
    "$REAL" "$@"
    saved | while read -r n; do st=""; [ "$n" = "$con" ] && st=activated; echo "$n:atrapa-$n:802-11-wireless:$st"; done ;;
  "-g 802-11-wireless.mode,802-11-wireless.ssid connection show id "*)
    n=${args#-g 802-11-wireless.mode,802-11-wireless.ssid connection show id }
    if [ "$n" = Hotspot ] && [ -s "$D/hotspot" ]; then echo ap; cat "$D/hotspot"
    elif grep -qF "$n:" "$D/zapisane"; then echo infrastructure; echo "$n"
    else exec "$REAL" "$@"; fi ;;
  "device wifi hotspot ssid "*)
    h=$(printf '%s\n' "$args" | sed -E 's/^device wifi hotspot ssid (.*) password .*$/\1/')
    sleep 1; echo "$h" > "$D/hotspot"; echo Hotspot > "$D/polaczona"
    echo "Device 'wlan0' successfully activated." ;;
  *"device wifi list"*)
    [ "$wifi" = on ] || exit 0
    siec | while IFS= read -r l; do
      s=${l%%:*}; u=" "; [ "$s" = "$con" ] && u="*"
      echo "$u:$(printf '%s' "$s" | sed 's/:/\\:/g'):${l#*:}:wlan0"
    done ;;
  "device wifi rescan") exit 0 ;;
  *"device wifi connect"*)
    shift 5 2>/dev/null; [ "$1" = "45" ] || true
    # argumenty: --wait 45 device wifi connect SSID [password PW]
    set -- $args; ssid=""; pw=""
    ssid=$(printf '%s\n' "$args" | sed -E 's/^--wait 45 device wifi connect (.*)$/\1/; s/ password .*$//; s/ hidden yes$//')
    pw=$(printf '%s\n' "$args" | sed -nE 's/.* password (.*)$/\1/p')
    sleep 1
    sec=$(siec | grep -F "$ssid:" | head -1 | cut -d: -f3)
    known=$(grep -F "$ssid:" "$D/zapisane" | head -1 | cut -d: -f2-)
    if [ -z "$sec" ] && ! siec | grep -qF "$ssid:"; then echo "Error: No network with SSID '$ssid' found."; exit 10; fi
    if [ -n "$sec" ]; then
      if [ -n "$pw" ]; then
        if [ "$pw" != "haslo1234" ] && [ "$pw" != "$known" ]; then
          echo "Error: Connection activation failed: Secrets were required, but not provided."; exit 4; fi
        grep -vF "$ssid:" "$D/zapisane" > "$D/z.tmp"; echo "$ssid:$pw" >> "$D/z.tmp"; mv "$D/z.tmp" "$D/zapisane"
      elif [ -z "$known" ]; then
        echo "Error: Connection activation failed: Secrets were required, but not provided."; exit 4
      fi
    fi
    echo "$ssid" > "$D/polaczona"
    echo "Device 'wlan0' successfully activated." ;;
  "device disconnect wlan0") : > "$D/polaczona"; : > "$D/hotspot"; echo "Device 'wlan0' successfully disconnected." ;;
  "device connect wlan0") [ -n "$(saved | head -1)" ] && saved | head -1 > "$D/polaczona" ;;
  *"device show wlan0")
    echo "GENERAL.HWADDR:5C\:E0\:C5\:12\:34\:56"; echo "GENERAL.CONNECTION:${con:---}"
    echo "CAPABILITIES.SPEED:unknown"
    if [ -n "$con" ]; then echo "IP4.ADDRESS[1]:192.168.1.23/24"; echo "IP4.GATEWAY:192.168.1.1"
      echo "IP4.DNS[1]:192.168.1.1"; echo "IP6.ADDRESS[1]:fe80::5ee0:c5ff:fe12:3456/64"; fi ;;
  "connection delete id "*)
    n=${args#connection delete id }
    if grep -qF "$n:" "$D/zapisane"; then grep -vF "$n:" "$D/zapisane" > "$D/z.tmp"; mv "$D/z.tmp" "$D/zapisane"
      [ "$con" = "$n" ] && : > "$D/polaczona"; echo "Connection '$n' successfully deleted."
    else exec "$REAL" "$@"; fi ;;
  "-g ipv4.method connection show id "*)
    n=${args#-g ipv4.method connection show id }
    grep -qF "$n:" "$D/zapisane" && echo auto || exec "$REAL" "$@" ;;
  *) exec "$REAL" "$@" ;;
esac
