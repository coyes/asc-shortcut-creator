# asc — A Shortcut Creator

Create `.desktop` shortcuts for your start menu without touching a text editor.
Supports Wine/umu games and native Linux apps or AppImages.

## Features

- **Wine / umu tab** — create shortcuts for Windows games via `umu-launcher`
  - Auto-detects installed Proton and GE-Proton versions
  - Supports WINEPREFIX, locale (LANG/LC_ALL), WoW64, and Wayland flags
- **Native / AppImage tab** — create shortcuts for Linux binaries and AppImages
  - Auto `chmod +x` for AppImages
  - Full XFCE category list (Games, Accessories, Development, etc.)
- **Icon support** — accepts PNG, JPG, BMP and any common format, auto-converts and installs to all hicolor sizes (16x16 → 256x256)
- **Shortcuts button** — opens `~/.local/share/applications/` in your file manager for easy cleanup
- Uses your system fonts automatically, with CJK (Japanese, Chinese, Korean) support

## Dependencies

```bash
sudo apt install gcc libsdl2-dev zenity
```

- `zenity` handles the file/folder picker dialogs
- The GUI is [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) — pure C, single header, no install needed

## Setup

Download the required single-header libraries into the project folder:

```bash
# Nuklear (immediate mode GUI)
curl -LO https://raw.githubusercontent.com/Immediate-Mode-UI/Nuklear/master/nuklear.h
curl -LO https://raw.githubusercontent.com/Immediate-Mode-UI/Nuklear/master/demo/sdl_opengl2/nuklear_sdl_gl2.h

# stb (image loading, resizing, writing)
curl -LO https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
curl -LO https://raw.githubusercontent.com/nothings/stb/master/stb_image_resize2.h
curl -LO https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
```

Your project folder should look like:

```
asc/
├── main.c
├── Makefile
├── nuklear.h
├── nuklear_sdl_gl2.h
├── stb_image.h
├── stb_image_resize2.h
└── stb_image_write.h
```

## Build

```bash
make
```

## Run

```bash
./asc
```

Or install to `~/.local/bin` and run from anywhere:

```bash
make install
asc
```

## Usage

### Wine / umu tab

For Windows games launched via [umu-launcher](https://github.com/Open-Wine-Components/umu-launcher).

| Field | Description |
|---|---|
| Game Name | Display name in the start menu |
| Game Executable | Path to the `.exe` file |
| Proton Version | Auto-detected from Steam and compatibilitytools.d, or set a custom path |
| Icon | Any image file — auto-converted to all hicolor sizes |
| WINEPREFIX | Optional — leave empty to use umu's default at `~/.local/share/umu/` |
| Locale | Sets `LANG` and `LC_ALL` — useful for Japanese/Chinese/Korean games |
| WoW64 | Adds `PROTON_USE_WINE64=1` |
| Wayland | Adds `PROTON_ENABLE_WAYLAND=1` |

The generated `Exec=` line looks like:

```
env GAMEID=ulwgl-0 PROTONPATH="/path/to/proton" umu-run "/path/to/game.exe"
```

### Native / AppImage tab

For Linux-native binaries and AppImages.

| Field | Description |
|---|---|
| App Name | Display name in the start menu |
| Binary / AppImage | Path to the executable — `chmod +x` is applied automatically |
| Icon | Any image file — auto-converted to all hicolor sizes |
| Category | XFCE menu category (Games, Development, Multimedia, etc.) |

## What gets created

A `.desktop` file at `~/.local/share/applications/<n>.desktop` and icons at:

```
~/.local/share/icons/hicolor/{16,32,48,64,128,256}x{16,32,48,64,128,256}/apps/<n>.png
```

`update-desktop-database` is called automatically — the shortcut appears in your menu immediately.

## Proton auto-detection

Scans these paths automatically:

- `~/.steam/steam/steamapps/common/`
- `~/.steam/root/steamapps/common/`
- `~/.steam/root/compatibilitytools.d/`
- `~/.steam/steam/compatibilitytools.d/`

Any folder containing a `proton` executable is listed. GE-Proton installed via [ProtonUp-Qt](https://github.com/DavidoTek/ProtonUp-Qt) is picked up automatically.

## Notes

- Tested on Debian 13 + XFCE + X11
- Should work on any DE that respects the XDG desktop spec
- Built with pure C11, SDL2, and Nuklear — no GTK, no Qt
