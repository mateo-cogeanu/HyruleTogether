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

## Current evidence — 2026-09-28

- Rebuilt the macOS universal client and ARM64 dedicated server.
- Started two isolated ARM64 Metal Cemu processes, both running EU BOTW v208 and
  DLC 3.0 from the user's own installation.
- Both reached the title screen, connected to `127.0.0.1:5051`, and received distinct
  player slots. Both logged discovery of the other player and live EventFlow
  animation-control addresses.
- All five existing server tests passed (using .NET 10 major-version roll-forward
  for the net8.0 test assembly). They cover password decoding, data paths, animation
  mapping, equipment mode, and binary bow/arrow payloads.
- Automated keyboard input was unreliable at the game menus. The saved DualSense
  profile was restored in both test copies. Loading the saves requires controller
  input before gameplay observations can proceed.

These checks establish startup and connection only. Movement, animation, equipment,
projectile rendering, enemy/quest sync, reconnect cleanup, and sustained two-client
performance remain unverified in this pass. No gameplay synchronization fix is
claimed from title-screen evidence.

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
