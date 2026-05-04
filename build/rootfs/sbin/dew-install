#!/bin/sh

PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

TARGET_MOUNT="/mnt/dewos-target"

RED="$(printf '\033[31m')"
GREEN="$(printf '\033[32m')"
YELLOW="$(printf '\033[33m')"
BLUE="$(printf '\033[34m')"
RESET="$(printf '\033[0m')"

ok() {
  echo "${GREEN}[ OK ]${RESET} $1"
}

warn() {
  echo "${YELLOW}[WARN]${RESET} $1"
}

fail() {
  echo "${RED}[FAIL]${RESET} $1"
}

info() {
  echo "${BLUE}[INFO]${RESET} $1"
}

clear_screen() {
  clear 2>/dev/null || true
}

line() {
  echo "----------------------------------------"
}

pause() {
  echo
  echo "Press Enter to continue..."
  read x
}

write_udhcpc_script() {
  mkdir -p /etc/udhcpc

  cat > /etc/udhcpc/default.script <<'EOF_INNER'
#!/bin/sh

RESOLV_CONF="/etc/resolv.conf"

case "$1" in
  deconfig)
    ip addr flush dev "$interface" 2>/dev/null || true
    ;;

  renew|bound)
    ip addr flush dev "$interface" 2>/dev/null || true
    ip addr add "$ip/${mask:-24}" dev "$interface" 2>/dev/null || true
    ip link set "$interface" up 2>/dev/null || true

    if [ -n "$router" ]; then
      ip route del default 2>/dev/null || true

      for gw in $router; do
        ip route add default via "$gw" dev "$interface" 2>/dev/null || true
        break
      done
    fi

    if [ -n "$dns" ]; then
      : > "$RESOLV_CONF"

      for ns in $dns; do
        echo "nameserver $ns" >> "$RESOLV_CONF"
      done
    else
      echo "nameserver 10.0.2.3" > "$RESOLV_CONF"
      echo "nameserver 1.1.1.1" >> "$RESOLV_CONF"
      echo "nameserver 8.8.8.8" >> "$RESOLV_CONF"
    fi
    ;;
esac

exit 0
EOF_INNER

  chmod +x /etc/udhcpc/default.script
}

mount_basic_fs() {
  mkdir -p /proc /sys /dev /tmp /run /mnt "$TARGET_MOUNT"

  mountpoint -q /proc || mount -t proc proc /proc 2>/dev/null || true
  mountpoint -q /sys  || mount -t sysfs sysfs /sys 2>/dev/null || true
  mountpoint -q /dev  || mount -t devtmpfs devtmpfs /dev 2>/dev/null || true

  write_udhcpc_script
}

header() {
  clear_screen
  echo "DewOS Installer"
  line
  echo
}

list_disks() {
  echo "Available install disks:"
  echo

  if command -v lsblk >/dev/null 2>&1; then
    lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINTS 2>/dev/null || true
  else
    cat /proc/partitions
  fi

  echo
}

is_valid_disk() {
  dev="$1"

  [ -b "$dev" ] || return 1

  case "$dev" in
    *[0-9]p[0-9]|*[a-z][0-9])
      return 1
      ;;
  esac

  return 0
}

ask_disk() {
  while true; do
    header
    list_disks

    echo "Target disk, example /dev/vda or /dev/sda:"
    read TARGET_DISK

    if is_valid_disk "$TARGET_DISK"; then
      echo
      echo "${RED}WARNING:${RESET} this will erase $TARGET_DISK"
      echo "Type YES to continue:"
      read answer

      if [ "$answer" = "YES" ]; then
        return
      fi
    else
      echo
      fail "Invalid disk: $TARGET_DISK"
      pause
    fi
  done
}

ask_user() {
  header

  while true; do
    echo "Username:"
    read USERNAME

    case "$USERNAME" in
      "")
        fail "Username cannot be empty."
        ;;
      *[!a-z0-9_-]*)
        fail "Use only lowercase letters, numbers, _ and -."
        ;;
      [0-9]*)
        fail "Username cannot start with number."
        ;;
      *)
        break
        ;;
    esac
  done

  echo
  echo "User password:"
  stty -echo
  read USER_PASS
  stty echo
  echo

  echo "Root password:"
  stty -echo
  read ROOT_PASS
  stty echo
  echo
}

ask_hostname_timezone() {
  header

  echo "Hostname [dewos]:"
  read HOSTNAME
  [ -z "$HOSTNAME" ] && HOSTNAME="dewos"

  echo
  echo "Timezone [Europe/Paris]:"
  read TIMEZONE
  [ -z "$TIMEZONE" ] && TIMEZONE="Europe/Paris"
}

has_ipv4() {
  iface="$1"
  ip addr show "$iface" 2>/dev/null | grep -q "inet "
}

has_default_route() {
  ip route 2>/dev/null | grep -q "^default "
}

gateway_works() {
  ping -c 1 10.0.2.2 >/dev/null 2>&1 && return 0
  return 1
}

http_works() {
  TMP="/tmp/dew-net-check.log"
  rm -f "$TMP"

  wget -O /dev/null http://example.com/ >"$TMP" 2>&1 && {
    rm -f "$TMP"
    return 0
  }

  if grep -q "HTTP/1." "$TMP" 2>/dev/null; then
    rm -f "$TMP"
    return 0
  fi

  if grep -q "server returned error" "$TMP" 2>/dev/null; then
    rm -f "$TMP"
    return 0
  fi

  if grep -q "Connecting to .*:80" "$TMP" 2>/dev/null; then
    rm -f "$TMP"
    return 0
  fi

  rm -f "$TMP"
  return 1
}

show_network_debug() {
  iface="$1"

  echo
  echo "Network debug:"
  line

  echo "Interface:"
  ip addr show "$iface" 2>/dev/null || ifconfig "$iface" 2>/dev/null || true

  echo
  echo "Routes:"
  ip route 2>/dev/null || route -n 2>/dev/null || true

  echo
  echo "DNS:"
  cat /etc/resolv.conf 2>/dev/null || true

  echo
}

repair_network() {
  iface="$1"

  warn "Trying to repair network on $iface..."

  ip link set "$iface" up 2>/dev/null || ifconfig "$iface" up 2>/dev/null || true

  if command -v udhcpc >/dev/null 2>&1; then
    udhcpc -i "$iface" -s /etc/udhcpc/default.script -q -n 2>/dev/null || true
  fi

  if ! has_default_route; then
    if ip addr show "$iface" 2>/dev/null | grep -q "10.0.2."; then
      warn "No default route. Adding QEMU route via 10.0.2.2..."
      ip route del default 2>/dev/null || true
      ip route add default via 10.0.2.2 dev "$iface" 2>/dev/null || true
    fi
  fi

  if [ ! -s /etc/resolv.conf ]; then
    warn "resolv.conf is empty. Writing fallback DNS..."
    echo "nameserver 10.0.2.3" > /etc/resolv.conf
    echo "nameserver 1.1.1.1" >> /etc/resolv.conf
    echo "nameserver 8.8.8.8" >> /etc/resolv.conf
  fi

  if ! http_works; then
    warn "HTTP check failed. Rewriting DNS fallback..."
    echo "nameserver 10.0.2.3" > /etc/resolv.conf
    echo "nameserver 1.1.1.1" >> /etc/resolv.conf
    echo "nameserver 8.8.8.8" >> /etc/resolv.conf
  fi
}

check_network_or_repair() {
  iface="$1"

  echo
  info "Checking network on $iface..."

  if ! ip link show "$iface" >/dev/null 2>&1; then
    show_network_debug "$iface"
    fail "Interface $iface does not exist."
    return 1
  fi

  if ! has_ipv4 "$iface"; then
    warn "No IPv4 address on $iface."
    repair_network "$iface"
  fi

  if ! has_ipv4 "$iface"; then
    show_network_debug "$iface"
    fail "Network failed: no IPv4 address on $iface."
    return 1
  fi

  ok "IPv4 address detected."

  if ! has_default_route; then
    warn "No default route."
    repair_network "$iface"
  fi

  if ! has_default_route; then
    show_network_debug "$iface"
    fail "Network failed: no default route."
    return 1
  fi

  ok "Default route detected."

  if gateway_works; then
    ok "Gateway reachable."
  else
    warn "Gateway ping failed. In QEMU this can be weird, continuing."
  fi

  if http_works; then
    ok "HTTP/DNS works."
    return 0
  fi

  warn "HTTP/DNS test failed. Trying repair..."
  repair_network "$iface"

  if http_works; then
    ok "HTTP/DNS works after repair."
    return 0
  fi

  show_network_debug "$iface"
  fail "Internet/Wi-Fi is not working."
  return 1
}

setup_ethernet() {
  echo
  echo "Ethernet interface [eth0]:"
  read NET_IFACE

  if [ -z "$NET_IFACE" ]; then
    NET_IFACE="eth0"
  fi

  echo
  info "Starting DHCP on $NET_IFACE..."

  ip link set "$NET_IFACE" up 2>/dev/null || ifconfig "$NET_IFACE" up 2>/dev/null || true

  if command -v udhcpc >/dev/null 2>&1; then
    udhcpc -i "$NET_IFACE" -s /etc/udhcpc/default.script || true
  else
    fail "No DHCP client found."
    pause
    return
  fi

  if check_network_or_repair "$NET_IFACE"; then
    ok "Ethernet is ready."
  else
    fail "Ethernet/internet is not working."
  fi

  pause
}

setup_wifi() {
  header

  echo "Wi-Fi setup"
  line
  echo

  if ! command -v wpa_supplicant >/dev/null 2>&1; then
    fail "wpa_supplicant not found."
    fail "Wi-Fi cannot work without it."
    pause
    return
  fi

  echo "Available network interfaces:"
  ip link 2>/dev/null || ifconfig -a 2>/dev/null || true
  echo

  echo "Wi-Fi interface, example wlan0 or wlp3s0:"
  read WIFI_IFACE

  if [ -z "$WIFI_IFACE" ]; then
    WIFI_IFACE="wlan0"
  fi

  echo
  info "Trying to unblock Wi-Fi..."
  rfkill unblock wifi 2>/dev/null || true
  rfkill unblock all 2>/dev/null || true

  echo
  info "Bringing interface up..."
  ip link set "$WIFI_IFACE" up 2>/dev/null || ifconfig "$WIFI_IFACE" up 2>/dev/null || true

  echo
  info "Scanning Wi-Fi networks..."
  echo

  if command -v iw >/dev/null 2>&1; then
    iw dev "$WIFI_IFACE" scan 2>/dev/null | grep 'SSID:' | sed 's/^[ 	]*SSID: //' | sort | uniq
  else
    warn "iw not found, cannot scan."
  fi

  echo
  echo "Wi-Fi SSID:"
  read WIFI_SSID

  if [ -z "$WIFI_SSID" ]; then
    fail "SSID cannot be empty."
    pause
    return
  fi

  echo "Wi-Fi password:"
  stty -echo
  read WIFI_PASS
  stty echo
  echo

  mkdir -p /etc/wpa_supplicant /var/run/wpa_supplicant

  if command -v wpa_passphrase >/dev/null 2>&1; then
    wpa_passphrase "$WIFI_SSID" "$WIFI_PASS" > /etc/wpa_supplicant/wpa_supplicant.conf
  else
    cat > /etc/wpa_supplicant/wpa_supplicant.conf <<EOF_INNER
ctrl_interface=/var/run/wpa_supplicant
update_config=1

network={
    ssid="$WIFI_SSID"
    psk="$WIFI_PASS"
}
EOF_INNER
  fi

  chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf

  echo
  info "Starting wpa_supplicant..."

  killall wpa_supplicant 2>/dev/null || true

  if ! wpa_supplicant -B -i "$WIFI_IFACE" -c /etc/wpa_supplicant/wpa_supplicant.conf; then
    fail "wpa_supplicant failed."
    pause
    return
  fi

  sleep 3

  echo
  info "Requesting DHCP..."

  if command -v udhcpc >/dev/null 2>&1; then
    udhcpc -i "$WIFI_IFACE" -s /etc/udhcpc/default.script || true
  else
    fail "No DHCP client found."
    pause
    return
  fi

  NET_IFACE="$WIFI_IFACE"

  if check_network_or_repair "$WIFI_IFACE"; then
    ok "Wi-Fi is ready."
  else
    fail "Wi-Fi/internet is not working."
    echo
    echo "${RED}Possible reasons:${RESET}"
    echo "${RED}  - wrong Wi-Fi interface${RESET}"
    echo "${RED}  - wrong SSID/password${RESET}"
    echo "${RED}  - missing kernel Wi-Fi driver${RESET}"
    echo "${RED}  - missing firmware${RESET}"
    echo "${RED}  - QEMU does not provide real Wi-Fi${RESET}"
  fi

  pause
}

setup_network() {
  header

  echo "Network setup"
  line
  echo
  echo "1) Ethernet DHCP"
  echo "2) Wi-Fi WPA/WPA2"
  echo "3) Skip"
  echo
  echo "Choose [1/2/3]:"
  read choice

  case "$choice" in
    1|"")
      setup_ethernet
      ;;
    2)
      setup_wifi
      ;;
    3)
      NET_IFACE=""
      ;;
    *)
      NET_IFACE=""
      ;;
  esac
}

get_first_partition() {
  disk="$1"

  case "$disk" in
    *nvme*|*mmcblk*|*loop*)
      echo "${disk}p1"
      ;;
    *)
      echo "${disk}1"
      ;;
  esac
}

partition_format_mount() {
  header

  echo "Partitioning $TARGET_DISK..."

  wipefs -a "$TARGET_DISK" 2>/dev/null || true

  printf "o\nn\np\n1\n\n\nw\n" | fdisk "$TARGET_DISK"

  sync
  sleep 2

  partprobe "$TARGET_DISK" 2>/dev/null || true
  sleep 2

  TARGET_PART="$(get_first_partition "$TARGET_DISK")"

  if [ ! -b "$TARGET_PART" ]; then
    echo "Partition not found: $TARGET_PART"
    echo
    echo "Trying to reload partition table..."

    partprobe "$TARGET_DISK" 2>/dev/null || true
    sleep 3

    if [ ! -b "$TARGET_PART" ]; then
      echo
      echo "Still not found: $TARGET_PART"
      echo
      echo "Current devices:"
      lsblk 2>/dev/null || cat /proc/partitions
      pause
      exit 1
    fi
  fi

  echo
  echo "Formatting $TARGET_PART as ext4..."
  mkfs.ext4 -F "$TARGET_PART"

  mkdir -p "$TARGET_MOUNT"

  if mountpoint -q "$TARGET_MOUNT"; then
    umount "$TARGET_MOUNT"
  fi

  echo
  echo "Mounting $TARGET_PART to $TARGET_MOUNT..."
  mount "$TARGET_PART" "$TARGET_MOUNT"
}

copy_system() {
  header

  echo "Copying DewOS live system to target..."

  mkdir -p "$TARGET_MOUNT/bin"
  mkdir -p "$TARGET_MOUNT/sbin"
  mkdir -p "$TARGET_MOUNT/etc"
  mkdir -p "$TARGET_MOUNT/proc"
  mkdir -p "$TARGET_MOUNT/sys"
  mkdir -p "$TARGET_MOUNT/dev"
  mkdir -p "$TARGET_MOUNT/tmp"
  mkdir -p "$TARGET_MOUNT/root"
  mkdir -p "$TARGET_MOUNT/home"
  mkdir -p "$TARGET_MOUNT/mnt"
  mkdir -p "$TARGET_MOUNT/var"
  mkdir -p "$TARGET_MOUNT/run"
  mkdir -p "$TARGET_MOUNT/usr/bin"
  mkdir -p "$TARGET_MOUNT/usr/sbin"

  cp -a /bin/. "$TARGET_MOUNT/bin/"
  cp -a /sbin/. "$TARGET_MOUNT/sbin/"
  cp -a /etc/. "$TARGET_MOUNT/etc/"

  if [ ! -x /init ]; then
    fail "/init not found in live system."
    pause
    exit 1
  fi

  cp /init "$TARGET_MOUNT/init"
  chmod +x "$TARGET_MOUNT/init"

  cp /init "$TARGET_MOUNT/sbin/init"
  chmod +x "$TARGET_MOUNT/sbin/init"

  echo "Checking copied init files..."
  ls -l "$TARGET_MOUNT/init"
  ls -l "$TARGET_MOUNT/sbin/init"

  mkdir -p "$TARGET_MOUNT/home/$USERNAME"
  chmod 755 "$TARGET_MOUNT/home/$USERNAME"
  chmod 700 "$TARGET_MOUNT/root"
  chmod 1777 "$TARGET_MOUNT/tmp"
}

write_config() {
  header

  echo "Writing system config..."

  UUID="$(blkid -s UUID -o value "$TARGET_PART" 2>/dev/null || true)"

  if [ -z "$UUID" ]; then
    warn "Could not get UUID."
    UUID="UNKNOWN"
  fi

  cat > "$TARGET_MOUNT/etc/fstab" <<EOF_INNER
UUID=$UUID / ext4 defaults 0 1
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
devtmpfs /dev devtmpfs defaults 0 0
tmpfs /tmp tmpfs defaults,mode=1777 0 0
EOF_INNER

  echo "DewOS installed" > "$TARGET_MOUNT/etc/dewos-installed"

  echo "$HOSTNAME" > "$TARGET_MOUNT/etc/hostname"

  cat > "$TARGET_MOUNT/etc/hosts" <<EOF_INNER
127.0.0.1 localhost
127.0.1.1 $HOSTNAME
::1 localhost ip6-localhost ip6-loopback
EOF_INNER

  cat > "$TARGET_MOUNT/etc/resolv.conf" <<EOF_INNER
nameserver 10.0.2.3
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF_INNER

  mkdir -p "$TARGET_MOUNT/etc/wpa_supplicant"

  if [ -f /etc/wpa_supplicant/wpa_supplicant.conf ]; then
    cp /etc/wpa_supplicant/wpa_supplicant.conf "$TARGET_MOUNT/etc/wpa_supplicant/wpa_supplicant.conf"
    chmod 600 "$TARGET_MOUNT/etc/wpa_supplicant/wpa_supplicant.conf"
  fi

  echo "$TIMEZONE" > "$TARGET_MOUNT/etc/timezone"

  USER_HASH="$(openssl passwd -6 "$USER_PASS" 2>/dev/null || echo '*')"
  ROOT_HASH="$(openssl passwd -6 "$ROOT_PASS" 2>/dev/null || echo '*')"

  cat > "$TARGET_MOUNT/etc/passwd" <<EOF_INNER
root:x:0:0:root:/root:/bin/sh
$USERNAME:x:1000:1000:$USERNAME:/home/$USERNAME:/bin/sh
EOF_INNER

  cat > "$TARGET_MOUNT/etc/group" <<EOF_INNER
root:x:0:
users:x:100:
$USERNAME:x:1000:
wheel:x:10:$USERNAME
EOF_INNER

  cat > "$TARGET_MOUNT/etc/shadow" <<EOF_INNER
root:$ROOT_HASH:19000:0:99999:7:::
$USERNAME:$USER_HASH:19000:0:99999:7:::
EOF_INNER

  chmod 600 "$TARGET_MOUNT/etc/shadow"

  mkdir -p "$TARGET_MOUNT/etc/udhcpc"

  cat > "$TARGET_MOUNT/etc/udhcpc/default.script" <<'EOF_INNER'
#!/bin/sh

RESOLV_CONF="/etc/resolv.conf"

case "$1" in
  deconfig)
    ip addr flush dev "$interface" 2>/dev/null || true
    ;;

  renew|bound)
    ip addr flush dev "$interface" 2>/dev/null || true
    ip addr add "$ip/${mask:-24}" dev "$interface" 2>/dev/null || true
    ip link set "$interface" up 2>/dev/null || true

    if [ -n "$router" ]; then
      ip route del default 2>/dev/null || true

      for gw in $router; do
        ip route add default via "$gw" dev "$interface" 2>/dev/null || true
        break
      done
    fi

    if [ -n "$dns" ]; then
      : > "$RESOLV_CONF"

      for ns in $dns; do
        echo "nameserver $ns" >> "$RESOLV_CONF"
      done
    else
      echo "nameserver 10.0.2.3" > "$RESOLV_CONF"
      echo "nameserver 1.1.1.1" >> "$RESOLV_CONF"
      echo "nameserver 8.8.8.8" >> "$RESOLV_CONF"
    fi
    ;;
esac

exit 0
EOF_INNER

  chmod +x "$TARGET_MOUNT/etc/udhcpc/default.script"

  cat > "$TARGET_MOUNT/sbin/netup" <<'EOF_INNER'
#!/bin/sh

IFACE="${1:-eth0}"

RED="$(printf '\033[31m')"
GREEN="$(printf '\033[32m')"
YELLOW="$(printf '\033[33m')"
BLUE="$(printf '\033[34m')"
RESET="$(printf '\033[0m')"

ok() {
  echo "${GREEN}[ OK ]${RESET} $1"
}

warn() {
  echo "${YELLOW}[WARN]${RESET} $1"
}

fail() {
  echo "${RED}[FAIL]${RESET} $1"
}

info() {
  echo "${BLUE}[INFO]${RESET} $1"
}

has_ipv4() {
  ip addr show "$IFACE" 2>/dev/null | grep -q "inet "
}

has_default_route() {
  ip route 2>/dev/null | grep -q "^default "
}

http_works() {
  TMP="/tmp/dew-net-check.log"
  rm -f "$TMP"

  wget -O /dev/null http://example.com/ >"$TMP" 2>&1 && {
    rm -f "$TMP"
    return 0
  }

  if grep -q "HTTP/1." "$TMP" 2>/dev/null; then
    rm -f "$TMP"
    return 0
  fi

  if grep -q "server returned error" "$TMP" 2>/dev/null; then
    rm -f "$TMP"
    return 0
  fi

  if grep -q "Connecting to .*:80" "$TMP" 2>/dev/null; then
    rm -f "$TMP"
    return 0
  fi

  rm -f "$TMP"
  return 1
}

show_debug() {
  echo
  echo "Network debug:"
  echo "----------------------------------------"

  echo "Interface:"
  ip addr show "$IFACE" 2>/dev/null || ifconfig "$IFACE" 2>/dev/null || true

  echo
  echo "Routes:"
  ip route 2>/dev/null || route -n 2>/dev/null || true

  echo
  echo "DNS:"
  cat /etc/resolv.conf 2>/dev/null || true

  echo
}

repair_network() {
  warn "Trying to repair network on $IFACE..."

  ip link set "$IFACE" up 2>/dev/null || ifconfig "$IFACE" up 2>/dev/null || true

  if [ -x /etc/udhcpc/default.script ]; then
    udhcpc -i "$IFACE" -s /etc/udhcpc/default.script -q -n 2>/dev/null || true
  else
    warn "Missing /etc/udhcpc/default.script"
    udhcpc -i "$IFACE" -q -n 2>/dev/null || true
  fi

  if ! has_default_route; then
    if ip addr show "$IFACE" 2>/dev/null | grep -q "10.0.2."; then
      warn "Adding QEMU default route via 10.0.2.2"
      ip route del default 2>/dev/null || true
      ip route add default via 10.0.2.2 dev "$IFACE" 2>/dev/null || true
    fi
  fi

  if [ ! -s /etc/resolv.conf ]; then
    warn "Writing fallback DNS"
    echo "nameserver 10.0.2.3" > /etc/resolv.conf
    echo "nameserver 1.1.1.1" >> /etc/resolv.conf
    echo "nameserver 8.8.8.8" >> /etc/resolv.conf
  fi

  if ! http_works; then
    warn "HTTP check failed. Rewriting DNS fallback"
    echo "nameserver 10.0.2.3" > /etc/resolv.conf
    echo "nameserver 1.1.1.1" >> /etc/resolv.conf
    echo "nameserver 8.8.8.8" >> /etc/resolv.conf
  fi
}

info "Starting network on $IFACE..."

repair_network

if ! has_ipv4; then
  show_debug
  fail "No IPv4 address on $IFACE"
  exit 1
fi

ok "IPv4 address detected."

if ! has_default_route; then
  repair_network
fi

if ! has_default_route; then
  show_debug
  fail "No default route."
  exit 1
fi

ok "Default route detected."

if http_works; then
  ok "HTTP/DNS works."
  exit 0
fi

repair_network

if http_works; then
  ok "HTTP/DNS works after repair."
  exit 0
fi

show_debug
fail "Internet/Wi-Fi is not working."
exit 1
EOF_INNER

  chmod +x "$TARGET_MOUNT/sbin/netup"

  cat > "$TARGET_MOUNT/sbin/wifi-up" <<'EOF_INNER'
#!/bin/sh

IFACE="${1:-wlan0}"
CONF="/etc/wpa_supplicant/wpa_supplicant.conf"

if [ ! -f "$CONF" ]; then
  echo "Missing Wi-Fi config: $CONF"
  exit 1
fi

rfkill unblock wifi 2>/dev/null || true
rfkill unblock all 2>/dev/null || true

ip link set "$IFACE" up 2>/dev/null || ifconfig "$IFACE" up 2>/dev/null || true

killall wpa_supplicant 2>/dev/null || true

wpa_supplicant -B -i "$IFACE" -c "$CONF"

sleep 3

udhcpc -i "$IFACE" -s /etc/udhcpc/default.script

if ! ip addr show "$IFACE" 2>/dev/null | grep -q "inet "; then
  echo "Wi-Fi failed: no IPv4 address."
  exit 1
fi

if ! ip route 2>/dev/null | grep -q "^default "; then
  echo "Wi-Fi failed: no default route."
  exit 1
fi

echo "Wi-Fi is up."
EOF_INNER

  chmod +x "$TARGET_MOUNT/sbin/wifi-up"

  cat > "$TARGET_MOUNT/sbin/time-sync" <<'EOF_INNER'
#!/bin/sh

if command -v ntpd >/dev/null 2>&1; then
  ntpd -q -p pool.ntp.org
elif busybox --list 2>/dev/null | grep -q '^ntpd$'; then
  busybox ntpd -q -p pool.ntp.org
else
  echo "No NTP client found."
fi
EOF_INNER

  chmod +x "$TARGET_MOUNT/sbin/time-sync"
}

finish_install() {
  header

  sync

  echo "DewOS installed successfully."
  echo
  echo "Target disk:      $TARGET_DISK"
  echo "Target partition: $TARGET_PART"
  echo "Mounted at:       $TARGET_MOUNT"
  echo

  echo "Unmounting target..."
  umount "$TARGET_MOUNT" 2>/dev/null || true
  sync

  echo
  echo "Rebooting into installed DewOS..."
  echo "3..."
  sleep 1
  echo "2..."
  sleep 1
  echo "1..."
  sleep 1

  reboot -f
}

main() {
  mount_basic_fs

  ask_disk
  ask_user
  ask_hostname_timezone
  setup_network
  partition_format_mount
  copy_system
  write_config
  finish_install
}

main "$@"
