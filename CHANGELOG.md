# Changelog

All notable changes made while turning the original Windows-only Milk Bar Launcher 2.0.1 codebase into **Hyrule Together** are documented here. This changelog describes the current development state relative to upstream tag `2.0.1` (`22d5184`).

## Unreleased — Hyrule Together cross-platform port

### Project identity

- Renamed the user-facing application from **Milk Bar Launcher** to **Hyrule Together**.
- Updated the Qt application name, window titles, footer, dialogs, server-hosting labels, command-line description, macOS bundle display name, executable name, package names, and primary documentation.
- Changed the macOS bundle identifier to `app.hyruletogether.launcher`.
- Preserved the legacy `MilkBarLauncher` application-data directory and internal BNP/mod filenames intentionally, allowing existing game paths, installed updates, DLC, Cemu configuration, saves, graphic packs, and server settings to continue working after the rename.

### Supported targets and packaging

- Added native build targets for:
  - macOS x86-64;
  - macOS ARM64 with Metal;
  - Linux x86-64; and
  - Linux ARM64.
- Added `scripts/build-bundled-launcher.sh` to produce one target-specific application. The compiled launcher no longer asks users to choose among architectures at runtime.
- Bundled the matching patched Cemu runtime, native multiplayer client, dedicated server, mod archive, UKMM merger, model builder, graphical assets, and Python/Qt runtime.
- Added a direct macOS application-bundle fallback for environments where PyInstaller is unavailable.
- Added target/host validation so incompatible packages cannot accidentally be built on the wrong operating system or CPU architecture.
- Added code signing of development macOS bundles and ZIP packaging through `ditto`.
- Added native component build scripts for the client, server, model builder, UKMM tool, and patched Cemu.
- Added setup and launcher scripts for development use.
- Made `build-bundled-launcher.sh` fully self-bootstrapping: it now creates the local Python virtual environment and installs all packaging requirements before building, eliminating the separate setup command for distributable builds.

### Native launcher and setup experience

- Replaced the Windows-only WPF injector workflow on macOS/Linux with a Python and Qt launcher.
- Added automatic discovery and validation of the bundled runtime and native client.
- Added guided selection of the decrypted game executable (`code/U-King.rpx`).
- Added update and DLC installation into the launcher's private Cemu MLC directory.
- Corrected update-title validation to accept BOTW's update title ID rather than treating the base-game title ID as the expected update.
- Added persistent launcher configuration, saved servers, selected player model, background choice, and hosting settings.
- Added setup diagnostics and clear validation errors for missing game, update, DLC, runtime, mod, or Cemu hooks.
- Added launch-state reporting and automatic cleanup of stale IPC sockets and Cemu session state.
- Added a Settings action that opens Cemu's built-in Graphic Pack Manager for BOTW, allowing FPS++, resolution packs, enhancements, and user-installed cheats to be managed without a separate Cemu setup.
- Restored the application icon using the bundled `icon.png` asset.

### User interface

- Recreated the launcher as a cross-platform Qt interface while retaining the visual direction of the original application.
- Uses one root background image across the entire window; panels and controls are alpha-composited over that single image rather than repeating or faking the background inside individual controls.
- Removed custom close and minimize controls in favor of the operating system's native title-bar controls.
- Added BOTW-inspired buttons, borders, typography, server cards, settings controls, and loading presentation.
- Added hover animation, page transitions, animated loading state, and interactive feedback.
- Added the Lobby Browser, host-server editor, Settings page, model browser, diagnostics, and status views.
- Added true per-pixel translucency without opaque page fills.

### Bundled Cemu integration

- Updated the port for current native Cemu rather than relying on the old Windows injection model.
- Added a reproducible Cemu patching flow.
- Exported `memory_getBase` and the HLE registration entry points required by the native client.
- Added explicit Cemu readiness and hook-readiness synchronization so the client registers callbacks only after coreinit HLE initialization is complete.
- Added support for client-provided HLE-only pseudo modules, enabling the UKMM actor hooks to resolve on native Cemu.
- Added a command-line option that opens Cemu's built-in Graphic Pack Manager filtered to a supplied title ID.
- Added additional crash diagnostics for active PPC state and nearby opcodes.
- Added a Metal sampler mapping fix:
  - maps up to 31 Wii U texture bindings onto Metal's 16 physical sampler slots;
  - declares each physical sampler only once;
  - maps every generated shader reference to the same deduplicated slot; and
  - avoids both out-of-range sampler bindings and duplicate sampler declarations.
- Added one-time invalidation of stale BOTW Metal shader and pipeline caches after the sampler-layout change.

### Cross-platform native client

- Added a CMake build for the injected/native multiplayer client, producing `.dylib` on macOS and `.so` on Linux.
- Added a compatibility platform layer for Win32 types, timing, threads, virtual-memory queries, dynamic symbol lookup, filesystem paths, sockets, and endian-sensitive memory access.
- Replaced Windows named pipes with a local Unix-domain socket IPC channel on macOS/Linux.
- Added portable application-data path handling.
- Reworked logging so the client, launcher, and server use predictable platform-specific locations.
- Corrected several non-portable exception constructions, path operations, string conversions, socket assumptions, and structure definitions.
- Added safe initialization waits for Cemu's emulated-memory base and HLE subsystem.
- Hardened client startup and disconnect behavior when configuration or runtime hooks are missing.

### Native Cemu memory scanning

- Reworked legacy `VirtualQuery`-based scans that assumed Cemu's Wii U heap was the eighth Windows memory region.
- On native Cemu, region-8 scans now operate only inside Cemu's 4 GiB emulated-memory reservation.
- Native scans start at the true emulated-memory base rather than trusting Windows allocation-order offsets that may point beyond the desired value.
- Prevented failed scans from traversing unrelated host-process address space indefinitely.
- Corrected location and equipped-item scans for native Cemu's memory layout.
- Added detailed scan-stage logging for location, equipment, world day, attack modifier, core player state, and multiplayer flags.
- Confirmed the client now reaches `Scanned game instance successfully.` on macOS ARM64 Cemu.
- Added address validation before runtime notification writes.

### Multiplayer startup and resilience

- Added the in-game quest-log notification `Server sync started!` after Link is discovered and the synchronization scan begins.
- Added explicit logging when the notification is written successfully.
- Fixed the previous timing behavior where the notification could be queued before safe game memory was available.
- Added actor-hook diagnostics and verified Link discovery through native Cemu HLE callbacks.
- Prevented an uncaught missing-flag exception from aborting Cemu with `signal 6`.
- Made absent legacy animation, held/equipped-state, attack-animation, and map-pin flags optional when UKMM does not expose them in the current runtime data.
- Added guarded writes for every optional address.
- Core position, state, player-name, connection, server, and actor synchronization can proceed when optional cosmetic flags are unavailable.
- Added the success log `Scanned core game flags successfully.` for this compatibility mode.
- Activated the existing remote equipment application path: received weapon, shield, bow, head, upper-body, and lower-body IDs are now applied when a remote actor spawns and whenever its equipment packet changes.
- Restored native animation synchronization by resolving each remote actor's animation-state pointer directly and applying changed animation hashes without relying on unavailable legacy UKMM string flags.
- Added a sub-40 ms low-latency movement mode with TCP packet coalescing disabled on clients and servers plus 120 Hz remote extrapolation; higher-latency sessions retain the stable 60 Hz path.

### Player model and game-data preparation

- Bundled a target-native model-builder utility.
- Added automatic creation and validation of the remote-player BFRES model assets.
- Added automatic UKMM merge against the user's own decrypted base game, update, and DLC files.
- Added validation that required model and animation files were produced before launch.
- Added automatic installation and enablement of the generated Cemu graphic pack.
- Added a belt-lookup guard patch to avoid invalid equipment/model lookup crashes.
- Preserved support for selectable player/NPC models and associated model metadata.

### Dedicated server

- Ported the dedicated server projects to modern cross-platform .NET targets.
- Added self-contained publication for macOS and Linux runtime identifiers so users do not need to install .NET.
- Added a non-interactive dedicated-server mode suitable for launcher-managed hosting.
- Added explicit server configuration-file support.
- Added platform-specific shared-data and log paths.
- Added graceful shutdown handling and launcher-managed process cleanup.
- Improved TCP framing so fragmented or combined packets are handled safely.
- Added IPv4, IPv6, hostname, and loopback handling.
- Hardened connection removal, shared-state access, serialization, and malformed-client handling.
- Updated tests and project references for the modern server target.

### Hosting and connectivity

- Added a GUI flow to create, save, and start a local dedicated server.
- Automatically adds a newly hosted loopback server to the server list.
- Streams server startup and failure status back into the launcher.
- Keeps the server alive for the Cemu session and shuts it down with the launcher.
- Supports connecting native clients across the target platforms through the shared network protocol.

### Diagnostics and stability work

- Added detailed `LatestLog.txt` output for server connection, player assignment, actor hooks, memory-scan stages, notification delivery, and compatibility fallbacks.
- Diagnosed and fixed the infinite native scan that consumed several CPU cores without reaching scan completion.
- Diagnosed the post-scan `signal 6` as an intentional uncaught exception caused by unavailable legacy flags, then removed that fatal path.
- Added guards around message, animation, equipment-state, attack, and map-pin memory writes.
- Added Cemu stack-trace detail to distinguish host-side failures from active PPC game faults.
- Fixed several invalid pointer and native address assumptions that previously caused inventory-related or delayed crashes.

### Documentation and build hygiene

- Added cross-platform build, development, packaging, configuration, hosting, and runtime documentation.
- Documented target-specific build requirements and output locations.
- Documented the private application-data layout and why its legacy name is retained.
- Expanded `.gitignore` for generated cross-platform artifacts, local tools, caches, and build products.
- Removed obsolete legacy Windows updater sources that contained embedded GitHub credentials; the cross-platform launcher does not use authenticated updater requests.

### Current compatibility notes

- Core multiplayer initialization and scanning are operational in the tested macOS ARM64 Metal build.
- A second physical client is still required for complete end-to-end validation of cross-device spawning and movement.
- Current UKMM-generated data does not expose the legacy optional animation/equipment/map-pin flags expected by the original Windows client. Core synchronization continues, but those cosmetic fields are disabled until equivalent addresses or data definitions are implemented.
- Native Cemu and the launcher do not contain copyrighted game files. Users must supply their own legally obtained and decrypted base game, update, and DLC data.
