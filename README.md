# alttab

A lightweight Alt+Tab window switcher for X11 with live thumbnails.

## Features

- Alt+Tab window switching with visual overlay
- Live window thumbnails via XComposite
- Window icons from `_NET_WM_ICON`
- MRU (most-recently-used) window ordering
- Keyboard navigation (Tab, Shift+Tab, Enter, Escape)
- Preview: raises selected window while switching
- Customizable hotkey, colors, and tile sizes

## Dependencies

Build dependencies:
- GCC (or compatible C11 compiler)
- make
- pkg-config
- libx11-dev
- libxcomposite-dev
- libxrender-dev
- libcairo2-dev

On Debian/Ubuntu:
```bash
sudo apt install build-essential pkg-config libx11-dev libxcomposite-dev libxrender-dev libcairo2-dev
```

On Fedora:
```bash
sudo dnf install gcc make pkg-config libX11-devel libXcomposite-devel libXrender-devel cairo-devel
```

On Arch Linux:
```bash
sudo pacman -S base-devel libx11 libxcomposite libxrender cairo
```

## Building

```bash
make
```

## Installation

```bash
sudo make install
```

This installs `alttab` to `/usr/local/bin`. To install to a different location:

```bash
sudo make install PREFIX=/usr
```

To uninstall:

```bash
sudo make uninstall
```

## Usage

Run the program:

```bash
alttab
```

Use Alt+Tab to cycle through windows. Release Alt to confirm selection, or press Escape to cancel.

### Command-Line Options

| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-V, --version` | Show version |
| `-k, --key=KEY` | Trigger key (default: Tab) |
| `-m, --mod=MOD` | Modifier key: Mod1, Alt, Mod4, Super, Shift, Control (default: Mod1) |
| `-b, --bg-color=RGBA` | Background color (default: #1a1a1ad9) |
| `-s, --highlight-color=RGBA` | Selection highlight color (default: #4d80cce6) |
| `-W, --tile-width=PX` | Tile width in pixels (default: 200) |
| `-H, --tile-height=PX` | Tile height in pixels (default: 150) |
| `-T, --no-thumbnails` | Disable thumbnail capture |
| `-v, --verbose` | Enable debug logging |

Colors can be specified as `#RRGGBBAA` (hex) or `r,g,b,a` (floats 0.0-1.0).

### Examples

```bash
# Use Super+Tab instead of Alt+Tab
alttab --mod=Super

# Darker background with larger tiles
alttab --bg-color=#000000cc --tile-width=250 --tile-height=200

# Disable thumbnails (faster, icons only)
alttab --no-thumbnails
```

## Autostart at Login

### Using ~/.xinitrc (startx)

Add to your `~/.xinitrc` before the window manager:

```bash
alttab &
```

### Using Desktop Entry (Display Manager)

Create `~/.config/autostart/alttab.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=alttab
Exec=alttab
X-GNOME-Autostart-enabled=true
```

### Using systemd User Service

Create `~/.config/systemd/user/alttab.service`:

```ini
[Unit]
Description=Alt+Tab window switcher
After=graphical-session.target

[Service]
ExecStart=/usr/local/bin/alttab
Restart=on-failure

[Install]
WantedBy=graphical-session.target
```

Enable and start:

```bash
systemctl --user daemon-reload
systemctl --user enable alttab.service
systemctl --user start alttab.service
```

### Window Manager Config

For i3, add to `~/.config/i3/config`:

```
exec --no-startup-id alttab
```

For sway (note: alttab is X11-only, won't work on Wayland):

N/A - Use sway's built-in switcher instead.

For Openbox, add to `~/.config/openbox/autostart`:

```bash
alttab &
```

## Troubleshooting

**"Failed to grab hotkey"**: Another program is already using Alt+Tab. Either close that program or use a different key combination with `--mod` and `--key`.

**No thumbnails appearing**: Your compositor may not support XComposite, or you're running without a compositor. Try running with a compositor like `picom` or `compton`, or use `--no-thumbnails`.

## License

MIT
