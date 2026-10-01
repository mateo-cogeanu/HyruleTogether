# Two-client desktop testing

Use two isolated Cemu profiles and one loopback server to reproduce synchronization
issues before testing on separate Linux and Android machines. Android development
is paused while this desktop test pass is underway.

Build the current client and server:

```sh
./scripts/build-native.sh
./scripts/build-server.sh
```

Prepare from an existing desktop launcher installation that already has the v208
update, multiplayer pack, Extended Memory pack, and a playable save:

```sh
python3 scripts/local-multiplayer.py prepare --game '/path/to/BOTW/code/U-King.rpx'
python3 scripts/local-multiplayer.py run
```

`--source` selects another launcher data folder; `--directory` selects a fresh test
folder. The default output is `Build/local-multiplayer`. Existing profiles are
rejected instead of overwritten. Use `--server` for a different server executable.
Set `--port` during preparation to change the default loopback port, 5051.

The two clients are named **Local A** and **Local B**. Their saves, installed titles,
settings, graphic packs, controller profiles, IPC sockets, and logs are separate.
The base game is shared. macOS uses APFS file clones and separately identified,
development-signed Cemu application copies. Linux copies the data, so allow space
for two installations. Unrelated and cheat graphic packs are disabled in the test
copies; multiplayer, Extended Memory, and existing FPS++ settings are retained,
with the normal FPS++ limit set to 30 FPS.

Load the same save in both windows with your configured controller. Test one
client's input at a time; check whether the controller affects an unfocused window
before relying on independent movement. Ctrl-C in the runner stops its server and
both client process groups. Closing either client also ends that runner session.

Inspect `a/LatestLog.txt`, `b/LatestLog.txt`, each client's `launcher.log` and
`cemu/config/log.txt`, and `server.log` under the test folder. Each new native
session replaces LatestLog and archives the previous session in that profile.

## Unattended gameplay regression run

After preparing the profiles, run:

```sh
./scripts/build-native.sh
python3 scripts/test-local-gameplay.py
```

The runner starts the server and both clients, supplies each with a loopback DSU
controller configured as a Wii U GamePad. Both pads remain neutral until native
telemetry has been fresh and continuous for at least 2.5 seconds, including at the paused title
screen. This prevents early A presses from becoming Cemu's calibration baseline.
The runner then navigates Continue, loads the copied saves, waits for live
player data, remote actors, and continuously unpaused gameplay in both clients
at the same instant, then exercises movement, jumping, weapon draw, bow firing,
and client disconnection. Readiness is rechecked before each movement action.
The runner restores controller profiles and stops its processes on success, failure, timeout, or interruption. No physical input
or window focus is required. Avoid opening game dialogs during a run.

Each run restores its private saves from `test-baseline/a` and `test-baseline/b`.
The baseline is captured from the test profiles on the first run. The original
launcher saves are never reset or modified. Use a safe outdoor save with a bow,
arrows, and a melee weapon; missing prerequisites produce a failed check rather
than an assumed pass. The current fixture is the existing Great Plateau save. The movement script walks
backward for two seconds to avoid its nearby cliff; bow aiming turns away from
overlapping players. Each direction makes up to four draw/fire attempts and
requires two distinct airborne arrow generations. `--bow-hold 12` extends the
trigger hold for visual diagnosis (valid range: 1–30 seconds).
`--inspection-hold 30` adds a bounded pause after each movement check for visual
inspection (valid range: 0–60 seconds; default: no pause).

To exercise the delayed-creation regression beyond the former ten-second retry:

```sh
python3 scripts/test-local-gameplay.py --spawn-delay-ms 12000
```

This opt-in fixture delays only the first remote-player spawn per slot, retains
normal game execution, and adds two assertions that the delay actually occurred.
The native client ignores `HYRULE_TEST_SPAWN_DELAY_MS` unless test telemetry is
also enabled. Delay values are bounded to 0–30000 ms. A pending request is no
longer resubmitted just because ten seconds elapsed; a missing callback still
fails the runner's bounded remote-actor prerequisite.

Artifacts are stored under `Build/local-multiplayer/runs/<timestamp>/`:

- `report.json`: overall verdict, per-check measurements, input timeline, revision,
  native-client binary hash, emulated controller type, and check timestamps. The command exits
  nonzero on any failed check.
- `a-controller.xml` / `b-controller.xml`: the exact generated test profiles,
  archived before launch and independent of the restored user profiles.
- `a.jsonl` / `b.jsonl`: opt-in native telemetry sampled at up to 10 Hz per stream,
  with every remote-player create/erase event retained without sampling.
- Client launcher, Cemu, native-client, and dedicated-server logs; input actions are
  also printed as they execute.

Movement checks require real horizontal displacement and at least 90% of local
samples matched at the peer within two seconds: one world unit for received
positions and two for positions written by the remote-actor update path. Missing,
stationary, non-finite, or excessively late data cannot pass. Animation, equipment,
and arrow checks compare observed local events with received data. Arrow checks
require two IDs observed more than three units from Link, with valid world
coordinates, and matching received ID/type pairs. Separate assertions require
those IDs to reach live replica position writes. Missing local flight is reported
separately from missing receipt or replica updates. Disconnect testing checks
that remote updates cease while the remaining client stays alive.

Two actor-lifecycle checks count unique guest actors from every create/erase
callback, including callbacks ignored by player adoption. Each client must retain
exactly one remote actor, never overlap two actors during creation or refresh,
and apply updates only to that tracked actor. Missing events, stale writes, and
ignored duplicates cannot pass. This verifies callback lifecycle; it does not
replace visual review or prove that all meshes belong to a tracked actor.

`HYRULE_TEST_TELEMETRY` enables these diagnostics only when explicitly set by the
test runner. `readiness` records the game pause state, and `projectile_applied`
records live replica IDs/types and position readback immediately after a write.
This readback does not prove the pose persists through a physics tick. The `applied` stream records the current remote actor and the position
cached by its write path; it does **not** prove the rendered mesh, equipment,
animation appearance, collision, or arrow physics are correct. Those require
additional visual or game-state assertions. These are bounded smoke/regression
checks, not a claim that all multiplayer behavior is covered.

Test the harness itself without Cemu:

```sh
python3 -m unittest discover -s CrossPlatform/testing -p 'test_*.py' -v
```

These tests exercise DSU packet CRC/layout, real UDP subscription, controller
isolation and button release, and rejection of false passes from stationary,
vertical-only, missing, invalid, or delayed movement data. They also reject stale,
paused, interrupted readiness data, and arrow passes based only on a nocked arrow,
one generation, missing peer receipt, an incorrect type, or invalid coordinates.
Actor tests reject ignored duplicates, transient overlap, missing erasure, stale
adoption, and missing/stale activity; repeated notifications of the same address
do not inflate the count.

## Shared item regression

```sh
python3 scripts/test-local-gameplay.py --inventory-drops --inventory-pickups --quest-fixture --enemy-fixture
```

The known copied save drops a Throwing Spear and wood through real inventory
controls. New checks match stable item IDs on both clients, inspect the peer game
factory's durability/modifier values, and require both live actors to remain at
matching positions after five seconds. A creation callback followed by immediate
erasure cannot pass. The pickup check requires completed removal of the same item
on the peer after server confirmation. `--inventory-pickup-client b` tests a peer
picking up A's drop. Its first run failed; the repeat now clears interaction state
before/after pickup and passes.
Do not infer pickup success from sending a controller button.

The protocol carries only verified value parameters in a 2,048-byte optional
tail. New clients rebuild guest key storage locally. The server retains queued
updates that cannot fit, supplies snapshots/tombstones to late joiners, and binds
creator authority to connection sessions rather than reusable player slots.
A snapshot with an unknown identity must spawn even if its old owner slot now
belongs to the receiving client. Native tests cover this case; reconnect gameplay
has not yet been certified.

`20261001-184117` passes all 25 checks available in that run, including persistent
peer spear/wood and owner pickup cleanup. `20261001-185019` adds and passes peer
factory metadata checks, but fails reverse pickup and later B input checks; its
report remains failed. Earlier callback-only evidence overstated wood lifetime:
removing local carry initialization from replica spawn packs fixes its immediate
removal. The current checks include lifetime verification. `20261001-190344` passes all 26
checks with owner pickup, peer metadata, persistent positions, and the existing
bidirectional player/arrow and synthetic enemy/quest checks. The reverse-pickup
repeat `20261001-191846` also passes all 26 checks, requiring B's completed pickup
and A's erase of the same shared item.

Remaining item work includes arbitrary food/special-item metadata, distinguishing
long-held material props and every unload/delete cause, shared physics updates,
and arbitrating simultaneous pickups before either game adds to inventory.
Enemy packets still contain health only. Enemy AI/attacks, concurrent damage,
quest dialogue/rewards/journal/stages, and authentic sword/shield attachment timing
are separate unfinished work. Android remains paused.

## Current evidence — 2026-10-01

- The current harness has twelve passing tests, including real
  UDP controller isolation and negative fixture readback cases. Sixteen server tests pass.
- `python3 scripts/test-local-gameplay.py --quest-fixture --enemy-fixture` enables
  synthetic writes only in the isolated test clients: A marks quest V1365
  (`HatenoMini_CameraBoy_Activated`) and reduces a baseline Bokoblin from 13 HP
  to 7. The checks require the same quest ID/enemy hash in B's memory after A's
  write. This does not test NPC dialogue, rewards, journal updates, actual hits,
  enemy AI, or additive simultaneous damage.
- `runs/20260930-201751/report.json` passes all 21 checks, including both fixtures.
  Quest replication took approximately 8.4 seconds through the existing event
  worker; this is functional transport evidence, not seamless quest UX.
- The final repeat `runs/20260930-202452/report.json` also passes all 21 checks
  with enemy memory access gated during loading/pause and the stricter fixture
  checker, which records the matching peer readback in the report.
- Native quest indexing finds 2,986 flags in each client. Initial capture and
  event application wait for stable loaded gameplay; enemy health is independent
  of quest-service readiness. Both services have scoped server locks and bounded
  queue drains. The existing enemy protocol carries health only, not AI state.
- `--scenario-window 180` accepts appended JSON lines in the printed run's
  `input.jsonl`, e.g. `{"client":"a","duration":0.2,"state":{"buttons":["plus"]}}`.
  Commands use the test DSU controllers; each hold is bounded to ten seconds.
  Return both games to unpaused gameplay before the window ends, otherwise
  subsequent readiness checks fail. The inventory-drop inspection
  `runs/20260930-200416` observed a dropped spear actor but failed this prerequisite
  with the inventory left open. No dropped-item replication assertion exists yet.
- `--inventory-drops` patches and archives the isolated spawn assembly and scripts
  inventory navigation, a Throwing Spear drop, and holding/releasing wood. Root
  tabs are normalized with L,L,R before selecting Inventory; quest events can
  leave Adventure Log selected. `--inventory-step-hold 5` provides optional pauses
  for visual diagnosis. The fixture checks local factory capture only, requiring
  weapon metadata and two wood creation events; it does not test peer creation,
  item transforms, ownership, or pickup. All ten checker/controller tests and the
  native bounded spawn-parameter tests pass.
- The new v208 factory hook exposes the spear's durability/modifiers/transform and
  wood's carry-box metadata. Wood's actor is `Obj_FireWoodBundle`; materials may
  lack an initial matrix. Failed reports `runs/20260930-205307` (wrong root tab)
  and `runs/20260930-210223` (wrong wood checker) remain unchanged, including their
  failed overall verdicts.
- `runs/20261001-165758/report.json` passes all 22 checks with
  `--inventory-drops --quest-fixture --enemy-fixture` and no inspection pauses.
  This includes local spear/wood capture, synthetic quest/health peer readback,
  and the existing gameplay/uniqueness/disconnect assertions. It is still not a
  dropped-item network test or a full combat/quest test.
- Two consecutive runs (`runs/20260930-172300/report.json` and
  `runs/20260930-172556/report.json`) passed all 16 checks available at that point. Both directions
  show movement, jump-animation packets, weapon-data packets, and two airborne
  bomb-arrow generations received and applied to live replicas. Client loss stops
  remote updates while the surviving client and server stay alive.
- Earlier arrow failures exposed an incorrect transform resolver: the character
  controller is null for arrows, and the bomb resource-handle path is unsuitable.
  The arrow resolver now follows the live rigid-body sets to the Havok body, with
  checked pointer reads and verified Wii U v208 position/rotation offsets. Local
  candidates wait for initialized positions before passing the ownership-radius
  check; erased/reused actors and invalid positions stop streaming.
- Inputs previously started during a loading fade, and forward movement could
  take Link over a cliff. The runner now waits for fresh unpaused samples spanning
  at least 2.5 seconds, changes the movement path, and makes bounded bow attempts
  until two airborne generations are observed.
- Fixed the Wii U v208 erase-hook argument: `ActorCreator::eraseActor(this, actor)`
  supplies the actor in `r4`; the client previously read the creator in `r3` and
  missed real erasure. Equipment refresh now retains the old actor until that
  callback before allowing a replacement.
- The new lifecycle checker rejects the pre-fix diagnostic run
  (`runs/20260930-174609`): two remaining actors in A and three in B. The first
  corrected run (`runs/20260930-175102`) passes its 16 existing checks; replaying
  its telemetry passes both new uniqueness checks, with one remaining actor each.
- The updated run (`runs/20260930-175548/report.json`) passes all 18 checks,
  including both lifecycle assertions. A repeat (`runs/20260930-175929/report.json`)
  failed its save-loading prerequisite: A remained on the title screen while B
  loaded. Its failed report is retained. Subsequent startup changes and validation
  are recorded below.
- The runner now keeps input neutral through native startup, uses VPAD mappings,
  and waits for both clients to be ready simultaneously. Cemu calibrates from its
  first raw input state and filters baseline buttons (`ControllerBase::calibrate`
  and `update_state`); pressing A before that read can cause it to be ignored
  permanently. The earlier subscription-only gate did not establish neutral
  calibration time. The title-screen failure is consistent with this race; the
  old report does not expose Cemu's internal calibration state.
- The first VPAD-only diagnostic (`runs/20260930-181149`) loaded both games but
  failed A's movement check. The sequential readiness check could leave A's
  previously accepted samples stale while waiting for B. Simultaneous readiness
  replaces that check, and the diagnostic failure remains recorded.
- The first neutral-warmup run (`runs/20260930-181849`) loaded both saves, but
  exposed a second spawn-timing fault: B queued a retry ten seconds after request,
  while the original was still constructing. Both callbacks arrived, leaving an
  ignored actor. Its duplicate/missing-sample checks fail and remain recorded.
  The client now retains pending requests and warns once instead of blindly
  resubmitting. Readiness is also rechecked before each movement action.
- The fixed delayed-startup run (`runs/20260930-184510/report.json`) passes all
  21 checks. The first actor callbacks arrived 17.692 seconds after deferral in A
  and 16.691 seconds in B; both clients warned that creation was delayed without
  resubmitting the pending request. Both uniqueness checks pass with one remaining
  actor, and movement/jump/weapon/arrow/disconnect checks all pass.
- The following ordinary run (`runs/20260930-184904/report.json`) passes all
  19 checks with the delay disabled. Both corrected runs load both saves through
  controller input without UI interaction; their exact VPAD profiles are archived.
  These are bounded macOS startup/gameplay regressions, not a guarantee against
  every future timing failure.
- Visual inspection of client A after movement shows one clothed remote Link and
  no extra T-pose copy. Computer-use access to B was denied, so both-client visual
  correctness is not claimed.
- A prior startup animation-address crash did not reproduce in subsequent completed
  runs after the animation-control locking fix. One diagnostic run failed to reach
  healthy gameplay in client A; its failed report is retained.

This is a passing telemetry regression run, not a complete multiplayer verdict.
Rendered animation/equipment correctness beyond the inspected scene, projectile collisions
and damage, other arrow types, shared enemy movement/attacks, quest dialogue/rewards, reconnect cleanup, and sustained
performance still need testing. No Linux or Android gameplay result is claimed.

## Gameplay pass

Record observations in both directions (A to B and B to A):

1. Load the same outdoor save; confirm exactly one remote actor in each client.
2. Walk, run, jump, crouch, turn, and stop; compare position, rotation, and animation.
3. Draw/sheathe and switch melee weapons, shields, bows, and armor; check for stale
   equipment, invisible meshes, repeated respawns, or a T-pose.
4. Fire successive arrows and switch arrow types; check direction, appearance,
   duplication, and disappearance.
5. Enter/leave a menu, move beyond visibility range, and return.
6. Fight an enemy and complete a disposable quest on these copied saves; inspect
   the corresponding state in the other client.
7. Exit one client, confirm its remote actor disappears, then start a fresh pair
   and check for stale or duplicate actors.
8. Repeat on the Linux machine before resuming Android work.
