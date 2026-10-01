# Changelog

All notable changes made while turning the original Windows-only Milk Bar Launcher 2.0.1 codebase into **Hyrule Together** are documented here. This changelog describes the current development state relative to upstream tag `2.0.1` (`22d5184`).

## Unreleased — Hyrule Together cross-platform port

### Combined enemy damage and attached-item filtering

- Negotiate enemy damage deltas in the JSON connection response. Updated clients send their initial tracked health once, then encode bounded local damage in the reserved `INT_MIN + damage` range of the existing enemy-health field. Server replies remain absolute health; older servers retain the client's legacy minimum-health behavior.
- Track the health last written to the game separately from received health. Count local hits before applying received damage, avoid echoing remote hits, combine independent hits at the server, clamp deaths to zero, and ignore stale healing replies. Keep active server combat baselines instead of silently discarding them after one hour. Blood Moon/respawn generations and shared enemy AI/movement/attacks remain unimplemented.
- Add a two-source isolated-game fixture and a one-handed club/shield inventory fixture. Require stable, fresh combined enemy readback and reject a temporary match followed by extra damage. Seventeen server tests, thirteen harness tests, and production native state/protocol tests pass; universal client and macOS server builds pass.
- Preserve failed diagnostic runs: `20261001-195059` left the inspected peer weapon held and failed its later draw check. Both `20261001-200103` and `20261001-200854` count the two enemy hits (13 minus 6 minus 3 equals 4) but fail A-to-B weapon draw. Neither is a full passing run.
- Investigating that failure exposed equipped weapons entering shared-drop capture: their raw-life factory parameters resemble inventory drops, and collecting a replica could delete the source's equipped weapon. Exclude current/pending calculation children using checked Wii U v208 parent fields, verified against the owned executable's parent getter/setter. Add an unattended assertion that equipped clubs never enter the shared-item stream. The context-menu fixture also explicitly selects Equip before confirming.
- The parent-only diagnostic `20261001-201553` still publishes equipped clubs and is retained as a failure. Also recognize the preceding player-equipment factory flag (`@PC`) and check the live equipped-item catalogue before publishing its raw-life creation. This guards the tested equipped-weapon path; dropping a same-resource duplicate immediately after equipping still needs broader classification coverage.
- The corrected run `20261001-202319` passes all 28 checks, including equipped-weapon exclusion, both one-handed draw relays, persistent spear/wood drops, reverse pickup cleanup, and stable combined enemy damage. This is packet/state validation, not proof of authentic shield attachment timing.
- Apply received enemy health with a conditional atomic write. If the game changes health after it was read, recompute local damage from the newer value before committing the tracking state. A production state test injects a hit between read and write and verifies that all 15 local damage is retained without an echo. Bounded retries preserve pending state under contention.
- The repeat `20261001-203025` with conditional health writes also passes all 28 checks. Both damage sources start at 13 health and converge to a persistent 4. Both clients exclude their equipped clubs and retain passing one-handed draw relays after reverse pickup cleanup. All test processes stop automatically; original launcher saves remain untouched.
- Authentic shield attachment timing, full quest dialogue/journal/rewards, arbitrary food/item metadata, live item physics, reconnect/unload behavior, Linux validation, and Android remain outside this validation. Android stays paused.

### Shared inventory drops and pickup cleanup

- Add a bounded, optional item tail to the existing desktop protocol. Keep stable item identities, server-owned creator sessions, queued snapshots for late joiners, and removal tombstones. Preserve verified weapon durability/modifier/scale/transform values and player material spawn values; guest pointers and local carry initialization (`@I`) never cross machines.
- Rebuild the Wii U v208 item factory parameter pack in local guest memory and dispatch it through the existing atomic game-thread queue. Adopt the created item, suppress echo capture, defer spawning to loaded gameplay/the correct map, and limit failed creation retries. Unknown snapshot identities are replicas even when a disconnected creator's player slot has been reused. Reset item bindings on a new client session.
- Capture verified inventory drops after loaded gameplay resumes. Restrict unmarked raw-life weapon creation to a recent inventory pause to avoid blindly forwarding enemy weapon loot. On completed nearby normal deletion, publish a removal; after server confirmation the peer requests and observes the same item's actual erase. This is a first implementation: simultaneous inventory acquisition is not arbitrated before the game awards an item, and unload/source classification and long-held materials still need broader coverage.
- Fix the quest worker's reward-only queue stall and independent boolean/integer/korok/item channel scheduling. Keep quest change suppression until its own event channel acknowledges completion, and deduplicate paraglider catch-up flags. These fixes do not establish complete quest journal/reward behavior or integer-stage replication.
- Replace unchecked timed acquisition of the outer server state mutex with an exception-safe scoped monitor. Budget enemy/quest batches before draining them; retain queued items when the remaining fixed frame cannot fit them. Release per-update client DTO allocations after serialization.
- Extend the unattended drop checks to require matching peer identities, actual peer factory metadata, and persistent matching actor positions. Add pickup cleanup checks and a selectable pickup client. Twelve harness tests, sixteen server tests, and production native protocol/state tests pass, including malformed tails, unsafe parameters, duplicate receipt, late creates after removal, and owner-slot reuse.
- Preserve diagnostic runs: `20261001-182500` ended in a Cemu texture-loader bus error before drops; `20261001-182744` passed its callback checks but exposed an immediately erased peer wood actor (the later persistence check catches this). `20261001-183448` verifies persistent peer spear/wood; `20261001-184117` passes all 25 checks including owner pickup cleanup. `20261001-185019` verifies peer metadata/persistence but fails reverse pickup and subsequent B input checks. Do not count this run as a full pass.
- The final owner-pickup run `20261001-190344` passes all 26 checks, including the new peer metadata check, persistent item positions, pickup cleanup, existing bidirectional player/arrow checks, and synthetic enemy/quest readback. Native universal client and macOS server builds pass. The reverse-pickup repeat `20261001-191846` also passes all 26 after the scripted peer pickup clears its interaction state before and after collecting the item. Add a server regression for removal arriving before the first spawn packet: acknowledge its fully described tombstone so it cannot block subsequent drops. Equipment transitions retain dispatch priority over item bursts.
- Shared enemy movement/AI/attacks, additive concurrent combat damage, authentic one-handed sword/shield visual timing, complete quest behavior, arbitrary item/food metadata, live item physics, reconnect/unload gameplay, and Linux validation remain outstanding. Android stays paused.
- Keep a newer local pickup removal pending when an older creation acknowledgement arrives. A production native state regression covers this ordering and confirms that only a removal acknowledgement stops retransmission.

### Inventory-drop capture and automated inventory actions

- Intercept the additional Wii U v208 actor-creation wrapper used for inventory items; the original equipment/map wrappers missed this path. Add opt-in, read-only factory telemetry with checked guest strings and complete bounded spawn-parameter decoding. Guest pointers remain local and are not transmitted.
- Capture a dropped Throwing Spear's raw durability, modifiers, scale, and transform. Wood uses `Obj_FireWoodBundle` with carry-box parameters (`IsPlayerPut`, `@I`), rather than a weapon-style transform. This is local creation evidence; network replication, pickup ownership, and live item physics remain unimplemented.
- Add `--inventory-drops` to script spear and wood drops on the isolated save baseline. Normalize the pause menu to Inventory because quest updates can leave Adventure Log selected; allow menu animations to settle and require both held/released wood creations. Archive the patched spawn assembly with each fixture run. Add negative checks for held-only material props, invalid durability, equipment children, and missing metadata.
- Add native spawn-parameter tests for truncated buffers, invalid keys/types, unterminated strings, callback pointer width, and size limits. Native protocol tests and all ten harness unit tests pass. Retain failed runs: `20260930-205307` navigated the wrong root tab; `20260930-210223` captured the drops but its checker used the wrong wood actor name/layout. Neither was a passing full run.
- The corrected unattended macOS run `20261001-165758` passes all 22 checks: local spear/wood capture, synthetic peer quest/health readback, and the existing movement, jump, equipment packets, arrows, actor uniqueness, and disconnect assertions. It uses the patched factory hook in both clients. This does not establish dropped-item replication or full cooperative combat/quest behavior.

### Equipment, quest flags, and enemy health

- Use the Wii U v208 Hold equipment helper to resolve the actor's live weapon profile; the previous call cleared the animation parameter instead. Restrict the equipment initialization delay to newly created actors, removing the extra delay on every established draw/sheath transition. Authentic shield attachment timing still needs visual verification.
- Fix quest serialization to transmit the string's characters rather than the C++ string object. Add a production-serializer regression covering short and heap-backed IDs.
- Index native quest flags across readable guest allocations using checked v208 boolean/integer layouts; both test clients resolve all 2,986 catalogued flags. Recognize the boolean completion bit while preserving category encoding. Serialize quest queues/scanning with a recursive mutex, initialize optional counter addresses, and stop the worker when the title exits.
- Gate quest capture/application on stable loaded gameplay so title-screen defaults cannot enter the session. Fix the reversed elapsed-time subtraction and run flag capture every 250 ms. Skip already queued flags without getting stuck on the same queue entry.
- Enable enemy health updates independently of quest initialization, ignore invalid health, retain the lowest reported health, release erased bindings without reading freed storage, and ignore stale erase callbacks. Health capture/application waits for loaded gameplay; batches retain excess entries beyond the 200-enemy wire limit.
- Replace timed, unchecked server mutex acquisition in enemy/quest services with scoped locks. Preserve simultaneous updates and retain large enemy backlogs in bounded batches. All ten server tests and nine controller/checker tests pass.
- Add opt-in isolated-save quest and enemy damage fixtures with peer memory readback. The macOS run `20260930-201751` passes all 21 checks, including quest V1365 replication and Bokoblin health 13→7, plus the existing movement, animation, equipment, arrows, uniqueness, and disconnect checks. These fixtures exercise transport/application, not actual quest dialogue or combat.
- The final repeat `20260930-202452` also passes all 21 checks after gating enemy memory access during loading/pause and adding same-entity/after-write assertions to the fixture checker. Native universal build and production quest protocol test pass.
- Add a bounded controller-command window for inspecting inventory drops. A real spear drop was observed creating a new actor; that inspection run subsequently timed out because the inventory was left open. Dropped-item replication, shared enemy movement/AI/attacks, quest rewards/journal behavior, and Linux validation remain unfinished. Android development remains paused.

### Unattended startup input fix

- Keep both test controllers neutral until native telemetry is fresh and continuous before sending title-screen buttons. Cemu uses its first raw input state as the calibration baseline and filters buttons held then; the previous runner could send A immediately after DSU subscription, before calibration.
- Use emulated Wii U GamePads with the correct VPAD directional/stick IDs, avoiding Pro Controller connection/extension callbacks. Archive generated profiles and record controller type in each report.
- Require both clients to satisfy readiness at the same instant; waiting for B after accepting A could leave A's earlier readiness stale. Retained the initial VPAD-only diagnostic, which loaded both saves but failed A's movement check.
- Removed the blind ten-second remote-spawn retry after a slow startup queued two requests before the first callback arrived. Keep the original request pending and log a delay once; the unattended runner still fails if creation never completes. Added an opt-in first-spawn delay fixture to reproduce this timing case without slowing ordinary sessions.
- Recheck simultaneous readiness before each movement action, including after earlier bow inputs. The first neutral-warmup diagnostic exposed a delayed duplicate and a missing-sample movement failure; its report remains retained.
- Passed all 21 checks in the forced-delay macOS run: actor callbacks arrived after 17.692/16.691 seconds without blind resubmission, with one remote actor per client and passing gameplay/disconnect checks in both directions. The following ordinary run passes all 19 checks with the delay disabled; both load both saves without UI interaction.
- Added neutral-warmup freshness/paused-title tests and VPAD profile mapping coverage. All eight harness tests pass. Android development remains paused.

### Remote actor duplication fix

- Corrected the Wii U v208 `ActorCreator::eraseActor` hook to read the actor argument from `r4`, rather than the creator in `r3`. Real erases now retire player, projectile, bomb, and enemy references through the existing cleanup paths.
- Removed premature equipment-refresh completion: returning from `deleteLater` only requests deletion. The client retains the old actor address until the real erase callback before allowing its replacement, preventing extra remote Links and the stale T-pose copy in the tested scene.
- Added unsampled remote-player create/erase telemetry and two independent lifecycle assertions. Ignored callbacks count toward actor totals; overlaps, missing erases, stale adoption, and missing/stale activity fail. The checker rejects the pre-fix diagnostic run with two actors in A and three in B. All six harness unit tests pass.
- The updated unattended macOS run passes all 18 checks, including exactly one remote actor per client. Visual inspection of client A shows one clothed remote Link without the extra T-pose copy; computer-use access to B was denied, so visual verification is limited to A. Added a bounded optional inspection pause. A repeat failed before gameplay because A stayed at the title screen; the failed report is retained and startup/input reliability remains unresolved. Android development remains paused.

### Arrow capture and stronger desktop checks

- Corrected arrow transform resolution for the tested Wii U v208 build: traverse live rigid-body sets to the Havok body, use its translation/current rotation fields, and validate every pointer link. The previous character-controller path is null for arrows; the bomb path followed resource handles.
- Deferred local capture until the arrow has an initialized position within the ownership radius, removed erased/reused candidates, and stopped streaming when actor/body identity or world coordinates become invalid. Inactive packets no longer carry invalid positions.
- Added unpaused-gameplay readiness checks and a safer Great Plateau movement/aiming script, plus bounded bow attempts requiring two airborne generations, matching peer ID/type pairs, and live replica position readback. Added negative arrow/readiness tests and configurable bow holds for diagnosis.
- Passed all 16 checks in two consecutive unattended macOS runs, including bomb-arrow flight/receipt/replica updates in both directions; all five harness unit tests pass. Visual observation also exposed extra remote Link/T-pose actors, which remain unresolved and outside the current passing assertions. Android work remains paused.

### Automated desktop gameplay tests

- Added a loopback DSU virtual-gamepad driver and unattended two-client regression runner: restores isolated save baselines, loads both games, scripts movement/jump/equipment/bow inputs, tests client loss, restores controller settings, and stops its processes automatically.
- Added opt-in native JSONL telemetry for local packets, received remote packets, and remote-actor position writes, plus timestamped reports, action traces, binary hashes, and archived logs. Failed prerequisites, missing activity, stale data, and timeouts produce failure rather than an assumed pass.
- Added real UDP protocol/isolation/release tests and negative cases for the movement checker. Fixed local-runner port preflight to allow immediate reruns after TCP TIME_WAIT.
- Protected native animation resolution, address checks, writes, and readback against concurrent invalidation during remote-actor equipment refresh after the automated run exposed a startup crash. Two subsequent unattended runs completed without that crash.
- Recorded the actual regression verdict: the initial macOS run passes 11 of 13 checks, including movement/jump/weapon packets in both directions and disconnect handling; both arrow checks fail because no local shot is captured. A preceding run also failed the received-position threshold in one direction. Visual correctness and sustained stability remain unverified.

### Local desktop synchronization testing

- Paused Android development to prioritize two desktop clients on one machine, followed by Linux testing.
- Added `scripts/local-multiplayer.py` to prepare independent client profiles and supervise a loopback server plus two clients, with separate saves, logs, IPC sockets, and macOS application identities; existing profiles are protected from overwrite.
- Verified two macOS Metal clients reach the EU BOTW v208 title screen and connect as distinct players. The five existing server tests pass. Gameplay synchronization was unverified at that initial title-screen stage; subsequent unattended gameplay results are recorded above.

### Android launcher and game setup

- Replaced the stock Cemu landing screen and connection popup with Android implementations of the desktop Settings, Lobby Browser, and Model Selection pages, reusing the existing artwork, backgrounds, and icon.
- Added in-app folder selection and staged installation of decrypted BOTW, v208 updates, and DLC, with title/region validation, free-space checks, byte progress, cancellation, and protection of existing files on failed copies.
- Added saved server management, direct connections, reachability checks, setup diagnostics, emulator/graphic-pack settings links, and selected-character data in the multiplayer handshake.
- Added prepared multiplayer-pack import through Android's folder picker; automatic UKMM merging/model generation still requires the desktop launcher.
- Passed seven Android 16 instrumentation tests covering the launcher, server persistence, JNI checks, metadata validation, title imports, and failed-copy rollback.

### Android client proof of concept

- Added a pinned Android Cemu integration, ARM64 NDK client build, APK build script, connection dialog, and ADB helper for copying an existing prepared multiplayer pack.
- Adapted native symbol resolution, thread linking, and in-process launcher IPC for Android; guarded Windows-only Winsock linker directives and added 16 KiB native-library alignment.
- Added BOTW-only startup checks, automatic required-pack activation, session-only server passwords, connection status, and explicit opt-in native startup. Android server hosting and on-device mod merging are not included.
- Fixed Android builds on macOS by excluding host-specific dependency overlays, and adapted the pinned emulator's immediately joined JNI helper thread for NDK 27.
- Built the debug APK; passed four Android 16 ARM64 emulator smoke tests, installation/pack-copy checks, native library and APK 16 KiB alignment checks, and a macOS universal client rebuild.
- Documented the OnePlus 13 / Android 16 test target and the remaining physical-device game and multiplayer validation requirements.

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
- Made bundled builds always republish the dedicated server instead of reusing any existing executable, preventing client/server protocol changes from producing a mixed-version package.
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

- Corrected the equipment-controller diagnosis using the latest change-only capture: the observed `0x57` is ASCII `W`, and the verified neighboring bytes form `WeaponSmallSword` or `WeaponBow`; this memory is a live profile string, not independent flag bits. The existing one-byte wire field now carries explicit `sheathed`, `melee`, `bow`, `shield`, or legacy-held modes while retaining zero/nonzero compatibility. Every typed transition writes and verifies the matching profile through the same controller chain on the live `Jugador` actor before BOTW's proven notification, so sword-to-bow switches no longer collapse into an indistinguishable Hold operation.
- Added the first dedicated visual arrow relay. Matching clients and servers transmit a generation ID, arrow type, active lifetime, position, and rotation for normal, fire, ice, electric, bomb, and ancient arrows. The actor hook claims only bow-mode arrows created within Link's ownership radius, assigns each synthetic callback through an ordered player-and-generation claim before local observation, advances to every new shot while older BOTW actors continue their flight, streams the newest real transform, hides completed replicas, rejects stale callbacks, clears ownership on disconnect/title shutdown, and logs sender/server/receiver generations and actor addresses.
- Made remote weapon, shield, and bow sheath/unsheath synchronization reliable by reading Link's proven controller profile string and mapping it to a typed mode. The remote `Jugador + 0xb94` Hold/Equip state remains isolated to the verified receiver-side actor path; diagnostics record the exact local controller address, raw profile string, logical mode, received wire byte, and remote actor readback.
- Added the first remote melee weapon, shield, and bow factory resolver. The native client resolves each unique `Jugador` `NpcEquipment` placeholder to the received `Weapon_*` resource immediately before the hooked actor factory performs its lookup, verifies/logs the resulting child actor, and keeps the existing one-shot controlled reload for later equipment changes without touching the working animation, armour, visibility, or spawn paths.
- Corrected all 32 `Jugador` actor packs to expose BOTW's verified bow attachment node while retaining `EquipName1`, `EquipName2`, and `EquipName3` for melee, shield, and bow respectively. Change-only wire diagnostics record `WType`, sword, shield, bow, held state, exact resource addresses, requested values, readbacks, and child creation addresses.
- Kept the `Demo_ChangeEquipState` protocol semantics explicit: every nonzero typed `EquipmentState` requests `Hold`, while zero requests `Equip`; the typed byte preserves the active weapon category without conflating melee and bow transitions.
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

- Documented Debian 13 build and Qt/X11 runtime dependencies after a fresh second-client setup exposed missing libudev headers and XCB libraries. Added a reminder to rebuild generated CMake and Python environments on each platform.
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
