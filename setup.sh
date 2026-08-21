#!/usr/bin/env bash
#
# setup.sh -- provisions a freshly-flashed Raspberry Pi 3 Model A+
# (64-bit Raspberry Pi OS, Bookworm or later) to run the NexStar WiFi
# Observatory hub as a boot-time service.
#
# Run once, as root, on the Pi itself, after first boot:
#   sudo bash setup.sh
#
# Idempotent-ish: safe to re-run if something fails partway through.

set -euo pipefail

# ------------------------------------------------------------------
# Configuration -- edit these if your setup differs
# ------------------------------------------------------------------

REPO_URL="https://github.com/Connor-Sebastian-S/nexstar-wifi-observatory.git"
INSTALL_DIR="/home/nexstar/telescope-nexstar-wifi"
SERVICE_USER="nexstar"
GPS_DEVICE="/dev/serial0"
GPS_BAUD="115200"
GPS_TIME_SYNC_TIMEOUT_SECONDS="90"

# ------------------------------------------------------------------

if [[ $EUID -ne 0 ]]; then
    echo "Run this with sudo: sudo bash setup.sh" >&2
    exit 1
fi

echo "=================================================="
echo "1. System packages"
echo "=================================================="

apt-get update
apt-get install -y --no-install-recommends \
    build-essential cmake git libsqlite3-dev \
    python3 python3-pip python3-serial

echo "=================================================="
echo "2. I2C + UART configuration"
echo "=================================================="

# 0 = enabled, 1 = disabled -- raspi-config's inverted convention
raspi-config nonint do_i2c 0
raspi-config nonint do_serial_hw 0     # enable the UART hardware
raspi-config nonint do_serial_cons 1   # disable login console on it,
                                        # freeing it for the GPS

CONFIG_FILE="/boot/firmware/config.txt"
if [[ ! -f "$CONFIG_FILE" ]]; then
    CONFIG_FILE="/boot/config.txt"   # pre-Bookworm images
fi

if ! grep -q "^dtoverlay=disable-bt" "$CONFIG_FILE"; then
    echo "dtoverlay=disable-bt" >> "$CONFIG_FILE"
    echo "Added dtoverlay=disable-bt to $CONFIG_FILE"
    echo "(the 3A+ has onboard Bluetooth, which otherwise steals the"
    echo " good hardware UART -- this keeps /dev/serial0 on the full"
    echo " PL011 UART instead of the less reliable mini-UART, so the"
    echo " GPS's 115200 baud reads are solid.)"
fi

systemctl disable hciuart.service 2>/dev/null || true

echo "=================================================="
echo "3. Service user + directories"
echo "=================================================="

if ! id -u "$SERVICE_USER" &>/dev/null; then
    useradd -m -G dialout,i2c,gpio "$SERVICE_USER"
    echo "Created user $SERVICE_USER"
fi

mkdir -p "$(dirname "$INSTALL_DIR")"

echo "=================================================="
echo "4. Source code"
echo "=================================================="

if [[ -d "$INSTALL_DIR/.git" ]]; then
    echo "Repo already present at $INSTALL_DIR -- pulling latest."
    sudo -u "$SERVICE_USER" git -C "$INSTALL_DIR" pull
else
    echo "Cloning into $INSTALL_DIR..."
    sudo -u "$SERVICE_USER" git clone "$REPO_URL" "$INSTALL_DIR"
fi

# If you're copying a working tree over by hand (scp/rsync) instead of
# via git, comment the block above out and place your files at
# $INSTALL_DIR before running this script.

echo "=================================================="
echo "5. Build"
echo "=================================================="

BUILD_DIR="$INSTALL_DIR/pi/build"

sudo -u "$SERVICE_USER" mkdir -p "$BUILD_DIR"
sudo -u "$SERVICE_USER" bash -c "cd '$BUILD_DIR' && cmake .. && cmake --build . -- -j2"

BINARY="$BUILD_DIR/telescopehub"

if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: build did not produce $BINARY -- see output above." >&2
    exit 1
fi

echo "=================================================="
echo "6. Data / catalogue directories"
echo "=================================================="

mkdir -p "$INSTALL_DIR/data"
chown -R "$SERVICE_USER":"$SERVICE_USER" "$INSTALL_DIR"

if [[ ! -f "$INSTALL_DIR/catalogue/telescopehub_catalogue.db" ]]; then
    PI_IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
    echo ""
    echo "!! $INSTALL_DIR/catalogue/telescopehub_catalogue.db is missing."
    echo "   The dashboard's recommended-targets list needs this file, and"
    echo "   it's too large for git -- copy it over directly instead."
    echo "   From the machine that already has it, run:"
    echo ""
    echo "     scp telescopehub_catalogue.db ${SERVICE_USER}@${PI_IP:-<this-pi-ip>}:$INSTALL_DIR/catalogue/"
    echo ""
    echo "   (needs a password or SSH key set up for the '$SERVICE_USER' user"
    echo "   first -- 'sudo passwd $SERVICE_USER' or 'ssh-copy-id' from the"
    echo "   other machine.)"
    echo ""
    echo "   Or, if you'd rather regenerate it from the Stellarium source"
    echo "   files (catalog-3.23.dat + names.dat, copied the same way):"
    echo ""
    echo "     cd $INSTALL_DIR/catalogue"
    echo "     python3 parse_catalog.py catalog-3.23.dat"
    echo "     python3 import_names.py"
    echo ""
fi

echo "=================================================="
echo "7. GPS time-sync helper (runs once at boot)"
echo "=================================================="

cat > /usr/local/bin/gps-time-sync.py <<PYEOF
#!/usr/bin/env python3
"""
Reads NMEA sentences from the GPS until a valid fix with date/time
comes through, sets the system clock from it, then exits.

Runs once at boot, strictly BEFORE telescopehub.service starts, so
nothing else has the serial port open at the same time -- this
process and the main C++ program never touch the port concurrently.

Gives up quietly after a timeout rather than blocking boot forever if
there's no fix yet; Raspberry Pi OS's built-in fake-hwclock will have
already restored an approximate time from before shutdown, so the
system clock is never wildly wrong (just imprecise) even if this
script times out.
"""
import serial
import subprocess
import sys
import time
from datetime import datetime, timezone

DEVICE = "${GPS_DEVICE}"
BAUD = ${GPS_BAUD}
TIMEOUT_SECONDS = ${GPS_TIME_SYNC_TIMEOUT_SECONDS}


def parse_rmc(line):
    # \$GPRMC/\$GNRMC,hhmmss.ss,A,...,ddmmyy,...  ('A' = valid fix)
    parts = line.split(",")
    if len(parts) < 10 or parts[2] != "A":
        return None
    time_str, date_str = parts[1], parts[9]
    if not time_str or not date_str:
        return None
    try:
        hh, mm, ss = int(time_str[0:2]), int(time_str[2:4]), int(time_str[4:6])
        dd, mo, yy = int(date_str[0:2]), int(date_str[2:4]), int(date_str[4:6])
        return datetime(2000 + yy, mo, dd, hh, mm, ss, tzinfo=timezone.utc)
    except ValueError:
        return None


def main():
    deadline = time.time() + TIMEOUT_SECONDS
    try:
        with serial.Serial(DEVICE, BAUD, timeout=1) as port:
            while time.time() < deadline:
                try:
                    raw = port.readline().decode("ascii", errors="ignore").strip()
                except Exception:
                    continue
                if "RMC" not in raw:
                    continue
                dt = parse_rmc(raw)
                if dt is None:
                    continue
                subprocess.run(
                    ["date", "-u", "-s", dt.strftime("%Y-%m-%d %H:%M:%S")],
                    check=True,
                )
                print(f"GPS time sync: system clock set to {dt.isoformat()}")
                return 0
    except Exception as exc:
        print(f"GPS time sync: could not open {DEVICE}: {exc}", file=sys.stderr)

    print("GPS time sync: no fix within timeout, keeping existing clock.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
PYEOF

chmod +x /usr/local/bin/gps-time-sync.py

cat > /etc/systemd/system/gps-time-sync.service <<'UNITEOF'
[Unit]
Description=Set system clock from GPS before the observatory service starts
Before=telescopehub.service
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/usr/bin/python3 /usr/local/bin/gps-time-sync.py
TimeoutStartSec=100
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
UNITEOF

echo "=================================================="
echo "8. telescopehub service"
echo "=================================================="

cat > /etc/systemd/system/telescopehub.service <<UNITEOF
[Unit]
Description=NexStar WiFi Observatory hub
After=network-online.target gps-time-sync.service
Wants=network-online.target
Requires=gps-time-sync.service

[Service]
Type=simple
User=${SERVICE_USER}
WorkingDirectory=${BUILD_DIR}
ExecStart=${BINARY}
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
UNITEOF

echo "=================================================="
echo "9. Enable + start"
echo "=================================================="

systemctl daemon-reload
systemctl enable gps-time-sync.service
systemctl enable telescopehub.service
systemctl restart gps-time-sync.service
systemctl restart telescopehub.service

echo ""
echo "=================================================="
echo "Done."
echo "=================================================="
echo ""
echo "A reboot is recommended so the I2C/UART/Bluetooth changes"
echo "take full effect:"
echo "    sudo reboot"
echo ""
echo "After that, check on things with:"
echo "    systemctl status telescopehub.service"
echo "    journalctl -u telescopehub.service -f"
echo "    journalctl -u gps-time-sync.service"
