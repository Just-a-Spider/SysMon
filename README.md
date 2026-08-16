# SysMon

SysMon is a small project that turns your Nintendo 3DS into a secondary PC monitor and macro pad. Built mainly for my own personal use, also, vibe-coded in Rust and C.

The project connects your PC and 3DS over Wi-Fi, showing you live hardware stats and giving you a few handy controls right on your console's bottom screen.

## How it works

SysMon is split into three parts:
- **sysmon-server**: A lightweight Rust app that sits in your PC's system tray. It reads your hardware data (temperatures, RAM, etc.) and waits for the 3DS to connect.
- **sysmon-web**: A local web dashboard used to configure the server.
- **sysmon-3ds**: The actual 3DS homebrew application written in C.

## Features

- **Live Telemetry**: Watch your CPU/GPU temperatures, RAM usage, and Fan speeds in real-time.
- **Hang Hunter**: See the heaviest processes running on your PC and tap one to kill it instantly.
- **Pomodoro Timer**: A simple built-in timer to help you focus.
- **Media Controls**: Play, pause, and skip your PC's music straight from the 3DS.
- **Custom Macros**: Map your 3DS buttons (or the touch screen) to run shell commands, open folders, or trigger shortcuts on your PC.

## Build & Installation

### 1. PC Server (Linux)
The server can be compiled into a native RPM package using `cargo-generate-rpm`. 

**Standard Build (with Screen Streaming):**
```bash
cd sysmon-server
cargo build --release
cargo generate-rpm
sudo rpm -i target/generate-rpm/sysmon-server-*.rpm
```

**Lightweight No-Cam Build (Telemetry & Macros Only):**
```bash
cd sysmon-server
cargo build --release --no-default-features
cargo generate-rpm --variant nocam
sudo rpm -i target/generate-rpm/sysmon-server-nocam-*.rpm
```
Alternatively, just run `cargo run --release` (or `cargo run --release --no-default-features`) if you prefer not to install it system-wide.

### 2. Nintendo 3DS Client
Requires `devkitARM` and `libctru` to be installed and available in your environment.

**Standard Build (with CAM tab):**
```bash
cd sysmon-3ds
make cia
```

**No-Cam Build:**
```bash
cd sysmon-3ds
make cia-nocam
```
Then, install the resulting `.cia` (`sysmon-3ds.cia` or `sysmon-3ds-nocam.cia`) onto your Nintendo 3DS using FBI.

## Configuration & Web Dashboard
The `sysmon-server` runs headlessly in your system tray so it doesn't get in your way. 

To configure it:
1. Click the SysMon icon in your Linux system tray and select **"Open Web Config"**.
2. The config dashboard will open in your browser at `http://127.0.0.1:7342/config`. (For security, this page can only be accessed from the PC itself).
3. From here, you can set an Auth PIN, change ports, and set up your macros.

*Note: You can also open `http://<YOUR_PC_IP>:7342/` on your phone or tablet on the same Wi-Fi to view a read-only live telemetry dashboard.*

### Firewall
You'll need to allow the ports through your firewall so the 3DS can connect. The default ports are `7341` for the 3DS and `7342` for the Web UI.
```bash
sudo firewall-cmd --add-port=7341/tcp --permanent
sudo firewall-cmd --add-port=7342/tcp --permanent
sudo firewall-cmd --reload
```

## Using the 3DS App

Once your server is running, open SysMon on your 3DS. Make sure both devices are on the same Wi-Fi network.

1. **Connecting:** On the first launch, the app will ask for your PC's IP address, the port, and your Auth PIN. It saves these to your SD card (`sdmc:/sysmon_cfg.txt`) so you don't have to type them again.
2. **Global Controls:**
   - Press **START** at any time to safely quit the app.
   - Press **SELECT** to pause the connection. This stops the telemetry and macros, which is great for saving battery or bandwidth if you are stepping away.
3. **The Screens:**
   - The **Top Screen** permanently displays your live PC telemetry.
   - The **Bottom Screen** has 5 touch tabs on the right side:
     - **POMO:** A Pomodoro timer (Press **A** to toggle, **Y** to reset).
     - **KILL:** Lists your heaviest PC processes. Tap one to kill it.
     - **MACRO:** Displays your custom touch macros (`T1`, `T2`, etc.).
     - **MEDIA:** Buttons to control your PC's currently playing media.
     - **SET:** Settings menu to change your IP or port if needed.

### Setting up Macros
You can set up macros from the Web Dashboard. Currently supported macro types:
- `cmd`: Executes a shell command (e.g., `wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle`).
- `keys`: Native keyboard shortcut simulation via enigo. *(Note: Linux Wayland blocks simulated keystrokes for global OS shortcuts like closing windows. Use `cmd` with tools like `xdotool` or `ydotool` instead if you need to bypass this).*
- `open`: Instantly opens URLs or local directories.
- `text`: Instantly types a string of text and hits Enter.

## Credits & Acknowledgments
The `sysmon-3ds` compilation uses tools from the amazing Nintendo 3DS Homebrew community:
- [bannertool](https://github.com/Steveice10/bannertool) by **Steveice10**
- [makerom](https://github.com/3DSGuy/Project_CTR/tree/master/makerom) by **profi200** / 3DSGuy

The 3DS UI font is [Monocraft](https://github.com/IdreesInc/Monocraft) by **IdreesInc** (distributed under the SIL Open Font License 1.1).
