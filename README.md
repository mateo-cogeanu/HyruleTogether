# Hyrule Together

Explore Hyrule together. **Hyrule Together** is a community multiplayer project
for the Wii U version of *The Legend of Zelda: Breath of the Wild*, running
through Cemu. It brings the original Milk Bar Launcher codebase to macOS and
Linux with a native client, a bundled desktop launcher, and a dedicated server.

The goal is a seamless cooperative adventure—from adding your game to joining
friends and sharing the world. The project is under active development; some
parts of world synchronization are still unfinished.

## What’s included

- A desktop launcher with game setup, saved servers, player models, and hosting.
- Patched Cemu, the native multiplayer client, and a self-contained dedicated
  server in each desktop package.
- Automatic multiplayer mod merging against your own game, update, and DLC
  files using the bundled UKMM merger.
- Player movement, equipment transitions, and projectile synchronization.
- Shared inventory drops and pickup cleanup for the tested weapon/material
  paths, combined enemy damage, and quest flag synchronization.

Authentic shield animation timing, shared enemy AI and attacks, complete quest
journal/dialogue/reward behavior, and broader dropped-item coverage still need
work. The automated two-client gameplay checks currently run on macOS; Linux
needs further gameplay validation. Android development is paused.

## Desktop platforms

| Platform | Build target | Package |
| --- | --- | --- |
| macOS · Apple Silicon | `mac_aarch64` | `.app` in a `.zip` |
| macOS · Intel | `mac_64` | `.app` in a `.zip` |
| Linux · x86-64 | `linux_64` | Launcher directory in a `.tar.gz` |
| Linux · ARM64 | `linux_aarch64` | Launcher directory in a `.tar.gz` |

Apple Silicon builds retain Cemu’s Metal backend. Build each target on a
computer with the matching operating system and CPU architecture.

## Getting started

You need your own dumped Wii U BOTW game files, the **v208 update**, and any DLC
you want to use. Game, update, and DLC files are not included in this repository
or its launcher packages.

1. Build the package for your machine using the command below, then open
   **Hyrule Together** from the resulting package.
2. Select the decrypted game’s `code/U-King.rpx` in the launcher. Install your
   decrypted update and DLC folders using the launcher’s setup controls.
3. Download Cemu’s community graphic packs through the launcher’s graphic-pack
   manager. The launcher checks and enables BOTW **Extended Memory** and merges
   the multiplayer mod automatically.
4. Set your player name and model. Use **Host Server** to start a local server,
   or add a server address to join friends, then press **Play**.

For detailed setup, build dependencies, and troubleshooting, see the
[desktop guide](CrossPlatform/README.md).

## Building

From a checkout of this repository, run the command matching your machine:

```sh
# macOS · Apple Silicon
./scripts/build.sh mac_aarch64

# macOS · Intel
./scripts/build.sh mac_64

# Linux · x86-64
./scripts/build.sh linux_64

# Linux · ARM64
./scripts/build.sh linux_aarch64
```

Install the native build dependencies described in the
[desktop guide](CrossPlatform/README.md#complete-bundled-builds) first. The build
script creates the Python environment, installs packaging dependencies, and
builds the launcher and its bundled components. Packages are written to
`Build/launcher/<target>/`; the target is fixed when compiling.

For a development checkout without packaging:

```sh
./scripts/setup-cross-platform.sh
./scripts/run-gui.sh
```

## Project layout

| Directory | Purpose |
| --- | --- |
| [`CrossPlatform/`](CrossPlatform/) | macOS/Linux launcher, model builder, and desktop test tooling |
| [`DLL/InjectDLL/`](DLL/InjectDLL/) | Native C++ multiplayer client and game integration |
| [`C#/`](C%23/) | Dedicated server and shared networking code |
| [`scripts/`](scripts/) | Build, packaging, setup, and automated gameplay checks |
| [`BNP Files/`](BNP%20Files/) | Multiplayer mod archives |
| [`Android/`](Android/) | Experimental Android client; development currently paused |
| [`WPF .NET 6/`](WPF%20.NET%206/) | Original Windows launcher |

## Development and testing

See the [local multiplayer testing guide](CrossPlatform/LOCAL_MULTIPLAYER_TESTING.md)
for unattended testing with two isolated Cemu clients and a local server.
The [changelog](CHANGELOG.md) records implemented features, passing checks, and
remaining limitations.

When contributing, describe the behavior you changed and how you verified it.
Keep the changelog updated, and keep game files, personal saves, and generated
build output out of commits.

## Credits and license

Hyrule Together builds on **Milk Bar Launcher 2.0.1** and uses
[Cemu](https://github.com/cemu-project/Cemu) and
[UKMM](https://github.com/NiceneNerd/UKMM). Credit belongs to the original project
contributors and the developers of these tools. See [LICENSE](LICENSE) for this
repository’s license notice; bundled third-party components retain their own
licenses.

---

Hyrule Together is an unofficial community project and is not affiliated with,
sponsored by, or endorsed by Nintendo. *The Legend of Zelda* and related names
and assets belong to their respective owners.
