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
player data and remote actors, and exercises movement, jumping, weapon draw,
bow firing, and client disconnection. It restores controller profiles and stops
its processes on success, failure, timeout, or interruption. No physical input
or window focus is required. Avoid opening game dialogs during a run.

Each run restores its private saves from `test-baseline/a` and `test-baseline/b`.
The baseline is captured from the test profiles on the first run. The original
launcher saves are never reset or modified. Use a safe outdoor save with a bow,
arrows, and a melee weapon; missing prerequisites produce a failed check rather
than an assumed pass. The current fixture is the existing Great Plateau save.

Artifacts are stored under `Build/local-multiplayer/runs/<timestamp>/`:

- `report.json`: overall verdict, per-check measurements, input timeline, revision,
  and native-client binary hash. The command exits nonzero on any failed check.
- `a.jsonl` / `b.jsonl`: opt-in native telemetry sampled at up to 10 Hz per stream.
- Client launcher, Cemu, native-client, and dedicated-server logs.

Movement checks require real horizontal displacement and at least 90% of local
samples matched at the peer within two seconds: one world unit for received
positions and two for positions written by the remote-actor update path. Missing,
stationary, non-finite, or excessively late data cannot pass. Animation, equipment,
and arrow checks compare observed local events with received data. A missing local
shot is reported separately from a missing remote shot. Disconnect testing checks
that remote updates cease while the remaining client stays alive.

`HYRULE_TEST_TELEMETRY` enables these diagnostics only when explicitly set by the
test runner. The `applied` stream records the current remote actor and the position
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
vertical-only, missing, invalid, or delayed movement data.

## Current evidence — 2026-09-30

- Rebuilt the macOS universal client; the three harness tests pass, including real
  UDP controller isolation. The earlier five server tests also passed.
- Two unattended runs loaded the private EU BOTW v208 / DLC 3.0 saves, connected
  both clients, activated remote actors, exercised all scenarios, and cleaned up.
- The latest run (`runs/20260930-162501/report.json`) passed 11 of 13 checks:
  movement, received jump-animation hashes, and received weapon equipment matched
  in both directions; remote updates stopped after client B exited.
- Both arrow checks failed: no active local arrow ID was observed. Bow mode did
  change, but this does not establish that a shot was fired or captured. Inventory,
  input timing, and local projectile capture still need investigation.
- The previous run (`runs/20260930-161616/report.json`) additionally failed the
  B-to-A received-position threshold (74% matched versus the required 90%). Its
  applied-position comparison passed. Sampling/timing and synchronization behavior
  need investigation; the threshold has not been relaxed to hide the failure.
- A preceding startup run crashed while writing an animation control during actor
  refresh. Resolution, address checks, writes, and readback now share the existing
  animation-control lock with invalidation. Neither subsequent run reproduced the
  crash; this is limited repeat-run evidence, not proof of sustained stability.

The overall gameplay verdict remains **failed** until arrow checks pass. Rendered
models, animation appearance, equipment appearance, projectile physics, enemy/quest
sync, reconnect cleanup, and sustained performance remain outside these telemetry
assertions. No Linux or Android gameplay result is claimed.

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
