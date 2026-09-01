#!/usr/bin/env bash
set -e

echo "==========================================================="
echo "   SysMon Server Installer (Arch Linux / CachyOS / Manjaro)"
echo "==========================================================="

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 1. Check dependencies
echo ":: Checking build dependencies..."
MISSING_PKGS=()
for pkg in cargo gtk3 xdotool; do
    if ! pacman -Qi "$pkg" &>/dev/null; then
        MISSING_PKGS+=("$pkg")
    fi
done

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    echo ":: Installing missing dependencies: ${MISSING_PKGS[*]}"
    sudo pacman -S --needed --noconfirm "${MISSING_PKGS[@]}"
fi

# 2. Build release binary
echo ":: Compiling sysmon-server in release mode..."
cd "$SCRIPT_DIR/sysmon-server"
cargo build --release

# 3. Install user-level binary and desktop files
echo ":: Installing binaries and desktop integration..."
mkdir -p ~/.local/bin ~/.local/share/applications ~/.local/share/icons/hicolor/512x512/apps

cp target/release/sysmon-server ~/.local/bin/sysmon-server
chmod +x ~/.local/bin/sysmon-server
cp packaging/sysmon-server.desktop ~/.local/share/applications/sysmon-server.desktop
sed -i "s|Exec=.*|Exec=$HOME/.local/bin/sysmon-server|" ~/.local/share/applications/sysmon-server.desktop
cp "$SCRIPT_DIR/sysmon-3ds/icon.png" ~/.local/share/icons/hicolor/512x512/apps/sysmon.png

# Update desktop icon caches
gtk-update-icon-cache -f ~/.local/share/icons/hicolor 2>/dev/null || true
update-desktop-database ~/.local/share/applications 2>/dev/null || true

# 4. System permissions (uinput, udev, groups)
echo ":: Configuring system uinput permissions & udev rules..."
sudo modprobe uinput || true
echo "uinput" | sudo tee /etc/modules-load.d/uinput.conf >/dev/null
sudo cp "$SCRIPT_DIR/sysmon-server/packaging/99-sysmon-uinput.rules" /etc/udev/rules.d/99-sysmon-uinput.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo usermod -aG input "$USER"

# 5. Firewall configuration (if UFW active)
if systemctl is-active --quiet ufw; then
    echo ":: Configuring UFW firewall rules..."
    sudo ufw allow 7341/tcp comment 'SysMon Telemetry' >/dev/null 2>&1 || true
    sudo ufw allow 7342/tcp comment 'SysMon Web' >/dev/null 2>&1 || true
    sudo ufw allow 7339/udp comment 'SysMon Gamepad' >/dev/null 2>&1 || true
fi

# 6. Restart daemon if already running
if pgrep -x sysmon-server >/dev/null; then
    echo ":: Restarting running sysmon-server process..."
    pkill -x sysmon-server || true
    sleep 0.5
    nohup ~/.local/bin/sysmon-server >/dev/null 2>&1 &
fi

echo "==========================================================="
echo "   SysMon Server successfully installed!"
echo "   Binary: ~/.local/bin/sysmon-server"
echo "   Udev Rule: /etc/udev/rules.d/99-sysmon-uinput.rules"
echo "   Ports: TCP 7341, TCP 7342, UDP 7339"
echo "==========================================================="
