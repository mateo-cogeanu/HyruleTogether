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

The runner starts the server and both clients, supplies one loopback DSU virtual
gamepad per client, navigates Continue, loads the copied saves, waits for live
player data, remote actors, and continuously unpaused gameplay, then exercises
movement, jumping, weapon draw, bow firing, and client disconnection. It restores controller profiles and stops
its processes on success, failure, timeout, or interruption. No physical input
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

Artifacts are stored under `Build/local-multiplayer/runs/<timestamp>/`:

- `report.json`: overall verdict, per-check measurements, input timeline, revision,
  and native-client binary hash. The command exits nonzero on any failed check.
- `a.jsonl` / `b.jsonl`: opt-in native telemetry sampled at up to 10 Hz per stream.
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

## Current evidence — 2026-09-30

- Rebuilt the macOS universal client; all five harness tests pass, including real
  UDP controller isolation. The earlier five server tests also passed.
- Two consecutive runs (`runs/20260930-172300/report.json` and
  `runs/20260930-172556/report.json`) pass all 16 current checks. Both directions
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
- Visual observation exposed extra remote Link actors, including a T-pose actor.
  This remains unresolved and is **not detected by the current passing checks**.
- A prior startup animation-address crash did not reproduce in subsequent completed
  runs after the animation-control locking fix. One diagnostic run failed to reach
  healthy gameplay in client A; its failed report is retained.

This is a passing telemetry regression run, not a complete multiplayer verdict.
Duplicate actors, rendered animation/equipment correctness, projectile collisions
and damage, other arrow types, enemy/quest sync, reconnect cleanup, and sustained
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
