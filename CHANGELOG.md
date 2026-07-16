# Changelog

All notable changes made while turning the original Windows-only Milk Bar Launcher 2.0.1 codebase into **Hyrule Together** are documented here. This changelog describes the current development state relative to upstream tag `2.0.1` (`22d5184`).

## Unreleased — Hyrule Together cross-platform port

### Project identity

- Corrected the GitHub repository name from the misspelled `HyruleToghether` to `HyruleTogether` and updated the repository instructions accordingly.
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
- Kept universal macOS native clients available for local development while thinning the client inside each signed launcher bundle to that bundle's single `arm64` or `x86_64` target.
- Added code signing of development macOS bundles and ZIP packaging through `ditto`.
- Made the refreshed signed macOS bundle replace the top-level test app as well as `dist/`, preventing an older neighboring app from silently launching a stale native client and graphic pack after a successful rebuild.
- Added native component build scripts for the client, server, model builder, UKMM tool, and patched Cemu.
- Added setup and launcher scripts for development use.
- Made `build-bundled-launcher.sh` fully self-bootstrapping: it now creates the local Python virtual environment and installs all packaging requirements before building, eliminating the separate setup command for distributable builds.
- Added Linux Cemu display-backend detection: builds include Wayland when `wayland-protocols >= 1.15` is installed and automatically fall back to X11/XWayland when it is missing, preventing late CMake configuration failures.
- Recorded the compiled Cemu display backend in each bundled runtime. Linux bundles built without Wayland support now force wxGTK onto X11/XWayland at launch, preventing an `Unsupported GTK backend` Vulkan crash on Wayland desktop sessions; Wayland-enabled builds retain native backend selection.

### Native launcher and setup experience

- Replaced the Windows-only WPF injector workflow on macOS/Linux with a Python and Qt launcher.
- Added automatic discovery and validation of the bundled runtime and native client.
- Added guided selection of the decrypted game executable (`code/U-King.rpx`).
- Added update and DLC installation into the launcher's private Cemu MLC directory.
- Corrected update-title validation to accept BOTW's update title ID rather than treating the base-game title ID as the expected update.
- Added persistent launcher configuration, saved servers, selected player model, background choice, and hosting settings.
- Added setup diagnostics and clear validation errors for missing game, update, DLC, runtime, mod, or Cemu hooks.
- Added launch-state reporting and automatic cleanup of stale IPC sockets and Cemu session state.
- Fixed packaged Linux game launches reopening a second Hyrule Together window instead of Cemu. Frozen bundles now re-enter the executable through a dedicated backend-worker mode, while development and portable macOS builds continue to invoke the Python backend directly.
- Prevented Linux Cemu from inheriting PyInstaller's private `LD_LIBRARY_PATH`. Cemu and its Graphic Pack Manager now load the host's matching X11, GTK, GLib, and Vulkan libraries instead of mixing packaged GUI libraries with the system GPU driver and crashing in `XGetXCBConnection`.
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
- Added an exported title-lifetime signal that becomes inactive at the beginning of Cemu teardown, before emulated memory is invalidated.
- Fixed the reproducible Cemu patcher to resolve the Metal shader implementation relative to its supplied header path, allowing clean one-command bundles to apply the sampler patch successfully.
- Added Pillow as an explicit packaging dependency so PyInstaller can convert the launcher PNG icon to the native macOS ICNS format during clean builds.
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

- Made remote weapon, shield, and bow sheath/unsheath synchronization reliable by reading Link's proven controller-chain state as a raw byte instead of an invalid C++ `bool`, mapping its visually verified polarity (`nonzero` held, zero sheathed), canonicalizing the serialized state to `0` or `1`, and accepting nonzero values from older clients. The remote `Jugador + 0xb94` Hold/Equip state remains isolated to the verified receiver-side actor path; fresh diagnostics record the exact local controller address, raw flags, logical state, received wire byte, and remote actor readback.
- Added the first remote melee weapon, shield, and bow factory resolver. The native client resolves each unique `Jugador` `NpcEquipment` placeholder to the received `Weapon_*` resource immediately before the hooked actor factory performs its lookup, verifies/logs the resulting child actor, and keeps the existing one-shot controlled reload for later equipment changes without touching the working animation, armour, visibility, or spawn paths.
- Corrected all 32 `Jugador` actor packs to expose BOTW's verified bow attachment node while retaining `EquipName1`, `EquipName2`, and `EquipName3` for melee, shield, and bow respectively. Change-only wire diagnostics record `WType`, sword, shield, bow, held state, exact resource addresses, requested values, readbacks, and child creation addresses.
- Kept the `Demo_ChangeEquipState` protocol semantics explicit: `IsEquipped=true` requests `Hold`, while false requests `Equip`; Link's raw controller flag is normalized before entering this shared cross-platform path.
- Fixed a macOS crash exposed by the first equipment reload. BOTW can deliver a delayed duplicate `Jugador` create callback for the deleted actor at the same guest address just before the worker requests its replacement; remote actors are now adopted only when a completed synthetic spawn dispatch has published a one-shot callback token, preventing stale-generation equipment writes and duplicate replacement requests.
- Primed each remote player's live `Demo_ChangeEquipState` parameter before spawning or replacing `Jugador`. The generated NPC EventFlow can execute only once on native Cemu; previously the early `baseAddr == 0` return left `Jugador*_Hold` in that action during creation, so BOTW created the correct `Weapon_*` children but never attached or rendered them, and a later `Hold`/`Equip` write was too late.
- Made macOS bundle cleanup tolerate Finder metadata races after moving prior build output aside, so a concurrently recreated `.DS_Store` cannot abort signing and packaging.
- Applied BOTW's equipment state to the live remote actor through the exact native operation used by `ChangeWeaponEquipState::oneShot_`: write `Hold=0` or `Equip=1` at actor offset `0xb94`, then notify its controller with message `0x2c`. Logs proved the prior EventFlow parameter was updated and the correct weapon children were created, but native Cemu never scheduled that EventFlow action, leaving those children invisible; the direct state dispatch runs after child creation and after every replacement without changing animation or armour dispatch.
- Corrected the equipment resolver boundary using the decompressed v208 RPX rather than the public synthetic-spawn path. NPC equipment bypasses `ActorCreator`'s public wrapper and enters its internal `createActor_` wrapper at `0x037b5be0`; the narrowly scoped resolver now patches only `Jugador` equipment placeholders there, before the child resource lookup, and logs every resulting `Weapon_*` actor creation.
- Preserved the macOS stale-pointer crash fix by removing the disproven persistent-template address cache entirely. Post-create diagnostics write only through the currently live actor-local `NpcEquipment` strings after validating their Cemu mappings, while replacement-in-progress gating still prevents retries from reaching deleted actors.
- Fixed the Linux crash at `setupActor+0x13a` and the subsequent macOS replacement-spawn crash revealed by the platform logs. The native client now captures the complete first proven-good actor-factory template—including stable `r3`, `r5`, and actor-storage bytes—and reuses it instead of submitting a deleted actor address, a null factory argument, or an unrelated actor's short-lived `r7` after an equipment deletion.
- Fixed a macOS crash during the controlled equipment reload. If the asynchronous replacement callback arrives after the player worker has already selected a create action, the stale creation is now cancelled at the worker, host queue, and final PPC dispatch boundaries instead of reaching BOTW as a duplicate synthetic actor.
- Added the first controlled equipment reload and restored visible armor without changing direct animation dispatch. The first actor stages its cached equipment resource names before that one reload, subsequent replacements do not loop, and armor logs include the exact selected model names.
- Prevented remote players from flashing in a T-pose and then disappearing during equipment synchronization. The client now caches armor and weapon data before the actor's first spawn, deterministically enables its replacement when a direct refresh deletion returns even on native Cemu builds that omit the asynchronous actor-erase callback, and prevents a delayed stale erase callback from clearing the replacement actor.
- Fixed the remaining native-Cemu remote-player T-pose race. The client now stages the latest normal animation before actor creation, and the generated multiplayer EventFlow no longer permanently suppresses `Demo_PlayASForDemo` after an initial placeholder request; all 32 player actions retry each frame until the live `Anim_<hash>` control is consumed.
- Fixed persistent remote-player T-poses on native macOS Cemu. The client now finds Cemu's deserialized live EventFlow parameter block instead of writing animation names into the inactive loaded-archive copy, derives already-replaced normal-animation controls from intact attack/equipment anchors, and refreshes those addresses whenever a remote actor is recreated.
- Prevented duplicate remote actors when Cemu's asynchronous spawn callback takes longer than the old three-second retry interval. A spawn remains pending until its callback arrives or a bounded timeout permits one retry.
- Fixed a macOS SIGABRT during startup when the optional remote map-pin signature is absent. Map-pin discovery now times out as a non-fatal capability, validates the remaining contiguous layout with a bound, and remote players skip map writes when no address is available, allowing the already-resolved EventFlow animation controls to proceed.
- Verified the custom actor's animation route end to end through its assets: the mutable controls are EventFlow `ASName` parameter buffers, normal hashes resolve through the 1,292-entry `MultiplayerAS` list, and attack hashes resolve through `MultiplayerAI`. The scanner now rejects any address that does not still contain the exact per-player EventFlow control name, logs the resolved player-1 buffers, and verifies/logs the first normal and attack write readback with signed and unsigned hashes, status, and address.
- Added a CMake build for the injected/native multiplayer client, producing `.dylib` on macOS and `.so` on Linux.
- Closed a title-shutdown lifetime hole exposed by the macOS log. When Cemu marks BOTW inactive, remote-player workers now observe the title signal themselves, leave deletion/spawn waits, are joined without writing despawn state into invalidating game memory, and clear only their host-side pointers before the server loop returns. The delayed connection-message worker now obeys the same title lifetime.
- Removed the disproven direct `GameROMPlayer` animation pointer-chain fallback. The custom `Jugador` NPC actor never creates that internal Link component, so retrying its null third link could not animate the remote model and exposed Linux to invalid pointer assumptions. Remote animation now uses the actor's designed `Anim_<hash>` and `Attack_<hash>` GameData/EventFlow controls exclusively.
- Added a pre-sync stale-player cleanup phase. Before accepting server-driven spawns, the client discards queued `Jugador` replacements, raises the established delete status for all 32 player slots, rejects actor creations observed during the two-second cleanup window, tracks erasures, clears stale native addresses, and only then enables replacement actors. This removes leftover duplicate player instances across reconnects/title sessions without changing platform-specific spawning behavior.
- Added a compatibility platform layer for Win32 types, timing, threads, virtual-memory queries, dynamic symbol lookup, filesystem paths, sockets, and endian-sensitive memory access.
- Replaced Windows named pipes with a local Unix-domain socket IPC channel on macOS/Linux.
- Added portable application-data path handling.
- Reworked logging so the client, launcher, and server use predictable platform-specific locations.
- Corrected several non-portable exception constructions, path operations, string conversions, socket assumptions, and structure definitions.
- Added safe initialization waits for Cemu's emulated-memory base and HLE subsystem.
- Hardened client startup and disconnect behavior when configuration or runtime hooks are missing.
- Added a separate generated Linux client source area. Linux builds copy the shared implementation to `Build/linux-client-source`, normalize inherited `static class`/`extern struct` MSVC extensions for GCC, and add the required `<cmath>` include, while macOS continues compiling the original tested source tree unchanged.
- Extended the generated Linux compatibility pass for GCC's stricter math namespace rules (`std::tan`), and removed legacy `.cpp` `#pragma once` and integer `NULL` warnings from Linux build output.

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

- Restored the original launcher's required BOTW Extended Memory check in the cross-platform launcher. The launcher now enables the downloaded community pack automatically and stops with a precise setup instruction when it is absent; without its actor/resource heap expansion Linux accepted and returned every `Jugador1` factory request but never reached `OnActorCreate`, while the otherwise matching macOS client with Extended Memory active spawned the player successfully.
- Made remote-player actor spawning a single-consumer PPC transaction. The three emulated cores now atomically claim each queued spawn, and the request remains blocked until the winning actor-factory call returns, preventing Linux from racing duplicate calls and silently failing to create the other player.
- Added narrowly scoped spawn-transaction diagnostics that log the exact shared actor-factory argument block and distinguish PPC-visible requests, successful atomic claims, actor-factory returns, and final actor creation. Added a release fence before the ready byte is published so ARM hosts cannot expose the flag before the shared arguments and claim state.
- Replaced the broken macOS/Linux `notPaused` game-data polling dependency with a heartbeat from the native per-frame actor hook. UKMM retained the legacy flag but did not refresh it, causing both connected clients to remain permanently "paused" and reject every remote-player creation request; normal gameplay can now spawn remote actors while menus and loading states remain guarded.
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
- Restored native animation synchronization through the custom actor's intended GameData/EventFlow interface. The client writes received animation hashes to each `Jugador` animation or attack control, logs when all remote animation controls are available, and confirms the first control actually applied for each remote player.
- Added a sub-40 ms low-latency movement mode with TCP packet coalescing disabled on clients and servers plus 120 Hz remote extrapolation; higher-latency sessions retain the stable 60 Hz path.

### Player model and game-data preparation

- Fixed persistent remote-player T-posing on native Cemu. The generated NPC's
  per-frame EventFlow was never scheduled, so valid synchronized animation
  strings could not reach the actor. The client now dispatches `Anim_<hash>`
  directly to the live Wii U AS controller through the existing atomic PPC
  function-call bridge, including the required floating-point arguments, while
  coalescing unchanged network updates. Equipment changes and setup retries now
  update the existing actor instead of deleting and duplicating it.
- Restored remote armour refreshes without regressing animation synchronization.
  Equipment changes now use BOTW's real `BaseProc::deleteLater` entry instead of
  the unscheduled delete EventFlow, then respawn exactly once with the updated
  model names. Fixed the first bow-sync attempt corrupting newly created actors:
  the remote actor exposes two equipment entries, not three, so bows now use its
  valid right-hand entry when no melee weapon is equipped. Local equipment reads
  are zero-initialized so absent items cannot cause random refresh loops.
- Fixed the macOS PPC crash when an equipment refresh returned through Cemu's
  multicore function dispatcher. Direct refresh deletes now bypass the early
  `deleteLater` HLE callback and wait for BOTW's real actor-erasure callback
  before respawning. Actor erase/create also resets the direct AS completion
  cache so a reused actor address cannot leave the replacement T-posed on the
  other platform.
- Bundled a target-native model-builder utility.
- Added automatic creation and validation of the remote-player BFRES model assets.
- Added automatic UKMM merge against the user's own decrypted base game, update, and DLC files.
- Fixed UKMM's incremental deployment leaving older content in Cemu after producing a correct final merge. The launcher now replaces the deployed content and DLC trees from UKMM's completed merged storage, validates the source multiplayer animation controls before post-processing, and versions this deployment behavior so existing installations rebuild automatically on both macOS and Linux.
- Made the deployment marker self-healing: even when its signature matches, the launcher now rebuilds instead of launching if the active Cemu graphic-pack `TitleBG.pack` is missing the multiplayer animation controls.
- Diagnosed the create/delete loop in the rebuilt EventFlow archive: `Jugador*_Status` was saved with a default value of `1`, while `MultiplayerEvent` also interpreted `1` as `SystemDelete`, allowing a remote actor to erase itself during initialization.
- Re-enabled remote animation EventFlow without the create/delete loop. The model builder now patches all 32 delete checks in place to use an explicit status value instead of the BNP's saved/default value, the native client uses that reserved value for despawning, and per-player attack writes no longer overrun shorter EventFlow string buffers.
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
- GUI-hosted servers now open in a real interactive macOS/Linux terminal, showing live join/leave logs and accepting every dedicated-server console command; a PID-file handshake preserves launcher readiness checks and managed shutdown.
- Supports connecting native clients across the target platforms through the shared network protocol.

### Diagnostics and stability work

- Fixed the separate `mainServerLoop` title-shutdown crash reported while Cemu logged `Game: Not running`. Cemu now marks title memory inactive before scheduler and memory teardown, and the multiplayer server, helper, pause, and quest workers stop before further game-memory access; the network connection is closed without calling the legacy fatal disconnect path or terminating Cemu.
- Fixed one-way remote-player visibility on Linux by making actor-spawn submission independent of HLE register write-back and emulated-core ownership. The original PPC hook stored one global request flag while its prepared argument registers remained local to the callback's emulated core; Linux's multicore recompiler could therefore let a different core consume the request with unrelated registers. Stack-pointer and saved-register ownership attempts both proved backend-dependent: fresh Linux logs reached `Submitted actor` indefinitely but never reached `Spawned player`. The native callback now persists all eight actor-factory arguments in PPC-visible transfer memory, and the PPC wrapper explicitly reloads them after the callback before consuming the request. Submission is transactional across concurrent callbacks: the argument block is written first, the ready byte is published last, and a pending request cannot be overwritten by another queued actor. Bundled patch contents remain part of the installed-mod signature so launcher updates automatically redeploy changed PPC patches.
- Fixed a Linux `SIGSEGV` introduced while diagnosing remote actor creation. The original `patch_SpawnActors.asm` transfer block was 44 bytes including two implicit alignment bytes—not 42 bytes—and is deliberately kept at the corrected 48-byte ABI with an atomic dispatch-state word. The matching reversed native padding is retained, exact size/offset assertions document and enforce the PPC ABI, invalid ring-buffer addresses are rejected before dereferencing, and instance storage is read only when an actor is actually queued. Scoped packing still prevents this ABI alignment from leaking into unrelated native types.
- Reverted the experimental direct actor-queue path after cross-device testing showed lost visibility and macOS graphics instability. The original deferred path is restored, with passive diagnostics distinguishing helper-thread transfer, HLE queueing, submission to BOTW's spawn function, and final actor creation.
- Fixed asymmetric cross-platform visibility when the server host occupied player slot 0. Remote actor slots are now compacted around the local server ID, so a two-player session consistently uses `Jugador1` on both clients instead of incorrectly requesting `Jugador2` on the first client; the corrected mapping is shared by names, models, close/far updates, prop-hunt state, and disconnect tracking.
- Fixed a Linux-only Cemu Vulkan `SIGTRAP` during BOTW startup. Cemu's continued-draw path incorrectly required a vertex descriptor even though its first-draw path supports pixel-only and descriptor-free pipelines; the bundled Linux patch now restores each active descriptor independently without altering the macOS renderer path.
- Fixed misleading post-handshake "crashes": server connection rejections now preserve their protocol reason, log whether the server is full, the password is incorrect, the server is unreachable, or its response is malformed, and return that explanation through launcher IPC instead of reporting a generic native-client failure.
- Added human-readable rejection reasons to the cross-platform dedicated server handshake while retaining the existing numeric response codes for compatibility with original clients.
- Fixed a native startup race exposed by fast macOS title loading: the Time Manager HLE callback could run before launcher IPC created `Game::GameInstance`, causing a native null dereference reported at the DynamicBranch PPC return address `0x01808994`. The local instance is now created before Cemu resumes title startup, preserved through multiplayer setup, and all world/actor/bomb callbacks defensively ignore events until their state exists.
- Hardened Unix launcher IPC after a post-connect native termination with no Cemu or macOS stack trace: acknowledgements now send only their actual length, command buffers are initialized and parsed using the received byte count, read/write failures are logged, Linux uses `MSG_NOSIGNAL`, and macOS enables `SO_NOSIGPIPE` so a closed launcher socket cannot terminate Cemu.
- Started native-client logging at preload time instead of after HLE hook installation, ensuring bootstrap failures always produce `LatestLog.txt`.
- Added explicit bootstrap diagnostics for Cemu memory-export resolution, emulated-memory initialization, HLE readiness, hook installation, hook acknowledgement, and launcher IPC connection.
- Added Linux packaging validation for all four required Cemu dynamic exports and made launcher IPC timeouts report the last native-client bootstrap status.
- Fixed Linux native-client preloading from the branded `Hyrule Together` package directory. Because `ld.so` splits `LD_PRELOAD` on whitespace without supporting quoted paths, the launcher now caches the exact client build in a private delimiter-free location before starting Cemu.
- Added detailed `LatestLog.txt` output for server connection, player assignment, actor hooks, memory-scan stages, notification delivery, and compatibility fallbacks.
- Diagnosed and fixed the infinite native scan that consumed several CPU cores without reaching scan completion.
- Diagnosed the post-scan `signal 6` as an intentional uncaught exception caused by unavailable legacy flags, then removed that fatal path.
- Added guards around message, animation, equipment-state, attack, and map-pin memory writes.
- Added Cemu stack-trace detail to distinguish host-side failures from active PPC game faults.
- Fixed several invalid pointer and native address assumptions that previously caused inventory-related or delayed crashes.

### Documentation and build hygiene

- Fixed `build-server.sh` on the Bash 3 version bundled with macOS, where expanding an empty restore-options array under `set -u` aborted the build before `dotnet publish` started.
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
