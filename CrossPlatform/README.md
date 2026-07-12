# Hyrule Together on macOS and Linux

This port runs the multiplayer client inside native 64-bit Cemu and replaces
the Windows-only WPF injector, Win32 process APIs, Winsock assumptions, and
named pipes with a Python launcher, a portable C++ layer, POSIX sockets, and a
native `.dylib`/`.so`.

## Complete bundled builds

The distributable launcher is compiled for exactly one platform target. That
target is not a user-facing setting. This single command also creates the
Python environment, installs packaging dependencies, and builds all native and
server components, so `setup-cross-platform.sh` is not required first:

```sh
./scripts/build-bundled-launcher.sh mac_x86_64
./scripts/build-bundled-launcher.sh mac_arm64_Metal
./scripts/build-bundled-launcher.sh Linux_x86_64
./scripts/build-bundled-launcher.sh Linux_arm64
```

Run the command matching the build computer. Cemu links native graphics,
windowing, input, and audio libraries, so each target must be built on its
matching OS and CPU architecture. Output is written under
`Build/launcher/<target>` as a macOS `.app` and zip or a Linux directory and
tarball.

The resulting package already contains:

- current patched Cemu for that target;
- the native Hyrule Together multiplayer client;
- the self-contained .NET 8 dedicated server;
- the Hyrule Together mod archive and a patched UKMM 0.17.1 merger; and
- the BOTW-styled launcher and its assets.

There is no Cemu selector, executable path, native-library path, or server
runtime setup in the finished application. The player selects their decrypted
BOTW `code/U-King.rpx`, installs the decrypted update and DLC folders with the
two launcher buttons, and presses Play. Update and DLC are copied into the
launcher's private Cemu MLC directory, so they do not depend on another Cemu
installation.

The port targets Cemu 2.6 and the current Cemu source tree. Hyrule Together needs
`memory_getBase` and `osLib_registerHLEFunction`; the build applies an export-
only patch because official macOS/Linux binaries do not expose those symbols
to dynamically loaded libraries. The patch does not alter emulation behavior.
The Apple Silicon build uses Cemu's Metal backend.

On macOS the build expects CMake, Ninja, Git, NASM, pkg-config, Rust/Cargo, and the Boost
packages used by Cemu. On Linux, install the packages listed in upstream
`BUILD.md`. Linux packaging also requires PyInstaller from
`CrossPlatform/requirements.txt`. The macOS builder can create its portable
application bundle directly when PyInstaller is unavailable.

## Build and configure

Requirements are CMake, a C++17 compiler, Python 3.9 or newer, and `curl`.
The setup script installs a private .NET 8 SDK only when one is not already
available, then publishes a self-contained server; players do not need a
system-wide .NET installation to host.

To prepare a development checkout and run the GUI without packaging, the
standalone setup helper remains available:

```sh
./scripts/setup-cross-platform.sh
./scripts/run-gui.sh
```

Individual development components can also be built manually:

```sh
./scripts/build-native.sh
./scripts/build-server.sh
python3 -m pip install -r CrossPlatform/requirements.txt

# Open the graphical launcher
./scripts/run-gui.sh

./scripts/install-patched-cemu.sh mac_arm64_Metal
```

The Qt GUI paints one BOTW background once at the window root. Every panel,
button, server card, input, header, and footer is genuinely alpha-composited
over that single image—there are no duplicated background tiles or opaque page
fills. It includes animated hover/page transitions, Sheikah-styled controls,
the Lobby Browser, server editor, Settings page, full model browser, setup
diagnostics, loading overlay, and Cemu launch status. It uses the native
macOS/Linux title bar and can also be opened with
`./CrossPlatform/milkbar_launcher.py gui`.

The Lobby Browser's **Host Server** action configures and starts the dedicated
server in an interactive system terminal, adds its loopback address to the
server list, and shuts the server down with the launcher. The terminal shows
live player join/leave activity and accepts the complete dedicated-server
command set (`help` lists the available commands). The server supports
IPv4, IPv6, and hostnames; handles fragmented TCP frames; and stores its shared
mapping data in the same platform data directory as the launcher and client.
For a standalone host, run the executable produced under `Build/server/<rid>`
with `--config /path/to/ServerConfig.ini --non-interactive`.

Settings and logs live in:

- macOS: `~/Library/Application Support/MilkBarLauncher`
- Linux: `$XDG_DATA_HOME/MilkBarLauncher`, or `~/.local/share/MilkBarLauncher`

The existing Milk Bar BNP contains delta archives, so mounting its files
directly as a Cemu graphic pack is unsafe. The distributable includes a
target-native UKMM merger. After the v208 update is installed, the launcher
merges the BNP against the player's own dumped game/update/DLC files and enables
the resulting Cemu graphic pack automatically. The player does not need BCML,
UKMM, or a separate mod-install step.

## Runtime scope

The desktop launcher covers setup, saved servers, background selection, model
selection, validation, starting Cemu, client loading, server connection, IPC,
and launch status. The old overlay that was attached directly to Cemu's Win32
window cannot be reproduced on Wayland/macOS in the same way; runtime messages
are presented by the launcher instead.
