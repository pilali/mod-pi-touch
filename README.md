# mod-pi-touch

A touchscreen UI for [mod-host](https://github.com/moddevices/mod-host) on Raspberry Pi. Manage LV2 plugin pedalboards, snapshots and parameters from a 7-inch capacitive touch display.

![Platform](https://img.shields.io/badge/platform-Raspberry%20Pi%205-red)
![Language](https://img.shields.io/badge/language-C-blue)
![UI](https://img.shields.io/badge/UI-LVGL%209-green)

---

## Hardware requirements

| Component | Specification |
|---|---|
| Board | Raspberry Pi 5 |
| Display | 7-inch capacitive touch, 720×1280 px (portrait framebuffer) |
| Audio | Any JACK-compatible ALSA device |
| MIDI | Up to 16 MIDI ports (USB or hardware) |

The display is driven through the Linux framebuffer (`/dev/fb0`) at 270° rotation, giving a 1280×720 landscape working area. The touch device is expected at `/dev/input/event1` (configurable).

---

## Dependencies

### Runtime

- **mod-host** — LV2 plugin host (TCP on `127.0.0.1:5555` / `5556`)
- **jackd** — JACK audio server
- **NetworkManager / nmcli** — WiFi management
- **systemd** — Service management

### Build

```bash
sudo apt install \
    build-essential cmake pkg-config git \
    liblilv-dev libserd-dev libsord-dev \
    libjack-jackd2-dev libevdev-dev
```

Third-party libraries bundled as git submodules: **LVGL 9** and **cJSON**.

---

## Installation

### 1. Clone the repository

```bash
git clone https://github.com/pilali/mod-pi-touch.git
cd mod-pi-touch
git submodule update --init --recursive
```

### 2. Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 3. Install the binary

```bash
sudo cp build/mod-pi-touch /usr/local/bin/
```

### 4. Configure sudoers (required for WiFi, power and JACK control)

Create `/etc/sudoers.d/mod-pi-touch`:

```sudoers
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli dev wifi rescan *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli dev wifi connect *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli dev wifi hotspot *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli con up *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli con down *
pistomp ALL=(ALL) NOPASSWD: /usr/bin/nmcli con delete *
pistomp ALL=(ALL) NOPASSWD: /bin/systemctl poweroff
pistomp ALL=(ALL) NOPASSWD: /bin/systemctl reboot
pistomp ALL=(ALL) NOPASSWD: /bin/cp /tmp/jackdrc_new /etc/jackdrc
pistomp ALL=(ALL) NOPASSWD: /bin/systemctl restart jack
pistomp ALL=(ALL) NOPASSWD: /bin/systemctl start mod-ui
pistomp ALL=(ALL) NOPASSWD: /bin/systemctl stop mod-ui
```

### 5. Install the systemd service

Copy your service file to `/lib/systemd/system/mod-pi-touch.service`, then:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now mod-pi-touch
```

### 6. Verify

```bash
systemctl status mod-pi-touch
```

---

## Configuration

All settings are stored in `$HOME/.mod-pi-touch/` and can be overridden via environment variables.

| Variable | Default | Description |
|---|---|---|
| `MPT_DATA_DIR` | `~/.mod-pi-touch` | User data directory |
| `MPT_PEDALBOARDS` | `~/.pedalboards` | Pedalboard bundle directory |
| `MPT_FACTORY_DIR` | `/usr/share/mod/pedalboards` | Read-only factory pedalboards |
| `MPT_HOST_ADDR` | `127.0.0.1` | mod-host address |
| `MPT_HOST_CMD_PORT` | `5555` | mod-host command port |
| `MPT_HOST_FB_PORT` | `5556` | mod-host feedback port |
| `MPT_FB_DEVICE` | `/dev/fb0` | Framebuffer device |
| `MPT_TOUCH_DEVICE` | `/dev/input/event1` | Touch input device |
| `MOD_USER_FILES_DIR` | `~/data/user-files` | User files root (models, IRs…) |

Persistent user preferences (language, audio settings, UI scale) are saved in `prefs.json` inside the data directory.

---

## Features

### Pedalboard canvas

The main view displays the active pedalboard as an interactive canvas.

- **Plugin blocks** — each loaded LV2 plugin appears as a block showing its name, bypass state, and up to 8 interactive parameter controls (arcs, toggles, enums). Tap a control directly on the block to change its value.
- **Connections** — audio, MIDI and CV signal connections between plugins and system I/O are drawn as lines. Tap a port to start or break a connection.
- **Plugin browser** — add a plugin to the canvas by tapping an empty area. Plugins are listed by category and can be searched by name.
- **Parameter editor** — long-press a block to open the full parameter editor: all ports with knobs, value labels, MIDI CC and CV assignments, and tempo-sync options.
- **Custom widget controls** — tap ⚙ in the parameter editor to choose which controls (up to 8) are displayed directly on the block. The block width adapts automatically (×1 for 1–4 controls, ×2 for 5–8).
- **Plugin bypass** — tap the bypass area on a block, or use the toggle inside the parameter editor.
- **Remove plugin** — available from the parameter editor footer.

### Snapshots

Snapshots capture the state of all plugin parameters and bypass states.

- Save, rename and delete snapshots from the snapshot bar at the bottom of the screen.
- Switch snapshots instantly with a single tap.
- Reorder snapshots via drag-and-drop.
- Snapshots are stored inside the pedalboard bundle and are fully compatible with mod-ui.

### Pedalboard management (Files menu)

- **New** — create a blank pedalboard with a custom name.
- **Save / Save as** — persist the current pedalboard, optionally under a new name.
- **Save snapshot** — overwrite the current snapshot with live state.
- **Rename** — rename the current pedalboard in place.
- **Delete** — remove the pedalboard bundle from disk.

### Bank browser

Banks are named collections of pedalboards for quick navigation on stage.

- Create and delete banks.
- Add or remove pedalboards from a bank.
- Tap any pedalboard in the browser to load it instantly.
- An automatic **All** bank shows every available pedalboard.

### Scene mode

The scene view offers a live-performance layout with 4 configurable slots.

- **Pédales tab** — each slot is assigned to a plugin instance. The top half of the card shows up to 4 interactive parameter controls; the bottom half shows the plugin name, active/bypassed state and a MIDI learn button. Long-press a slot to access the context menu (assign plugin, learn MIDI, customise parameters, unassign).
- **Setlist tab** — sequence scenes across pedalboards for a full set.
- Custom parameter selection for each slot is saved automatically and restored on the next launch.

### MIDI

- **MIDI learn** — long-press any parameter in the editor, then move a hardware knob or pedal to assign a CC. The channel and CC number are shown on a chip next to the parameter.
- **Bypass MIDI** — the bypass toggle can also be MIDI-learned per plugin.
- **Virtual MIDI loopback** — enable an internal MIDI loopback port from the pedalboard menu.

### CV (Control Voltage)

- Assign CV output ports from other plugins as modulation sources for any parameter.
- Configure CV range and operating mode (add, subtract, bypass, absolute) per assignment.

### Pre-FX

Accessible from the top bar, independent of the active pedalboard.

**Tuner**
- Chromatic pitch detection with note name (C–B), octave and cent offset (±50 cents bar).
- Configurable reference frequency (220–880 Hz, default 440 Hz).
- Input selection: both channels, left only or right only.

**Noise gate**
- Enable/disable with threshold (−70 to −10 dB) and decay (1–500 ms) controls.
- Four modes: Off, In 1, In 2, Stereo.

### Conductor

Accessible from the top bar.

- BPM display and direct value entry.
- Time signature (numerator and denominator).
- **Tap tempo** — up to 8 taps averaged for smooth BPM detection.
- Play/Stop transport control.
- Internal clock or MIDI clock slave mode.
- Tempo-synced parameters in the parameter editor automatically compute correct values at the current BPM.

### Settings

**Audio**
- Select the JACK audio device, buffer size (32/64/128/256 samples) and bit depth (16/24-bit).
- Detected input and output channel counts are shown.
- Apply changes restarts JACK automatically.

**MIDI**
- List of detected MIDI ports with per-port enable/disable.
- Virtual MIDI loopback toggle.

**System**
- mod-host connection status (address and port).
- CPU load indicator.
- UI language (English / French).
- UI scale percentage.
- Reboot and shutdown buttons.

**WiFi**
- Scan and connect to WPA2 networks.
- Hotspot mode (enable/disable, configure SSID password).
- Current connection and IP address display.

### MOD-UI co-existence

If the `mod-ui` web service is started, mod-pi-touch detects it, disconnects gracefully and shows an informational screen with the web UI address. When mod-ui is stopped, mod-pi-touch reconnects automatically.

---

## Data files

| File | Location | Description |
|---|---|---|
| `prefs.json` | `$MPT_DATA_DIR/` | User preferences |
| `last.json` | `$MPT_DATA_DIR/` | Last loaded pedalboard and snapshot |
| `plugin_cache.json` | `$MPT_DATA_DIR/` | LV2 metadata cache (rebuilt on first run) |
| `banks.json` | `$MPT_DATA_DIR/` | Bank definitions |
| `scene.json` | inside pedalboard bundle | Scene slot assignments |
| `<pb>.json` | `$MPT_DATA_DIR/widget_controls/` | Custom widget control selections per pedalboard |

Pedalboard bundles (`.pedalboard` directories) are stored in `$MPT_PEDALBOARDS` in standard mod-ui format and are fully interchangeable with mod-ui.

---

## License

See [LICENSE](LICENSE).
