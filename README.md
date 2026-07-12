# Hyrule Together
This repository contains the projects needed to build Hyrule Together.

> **macOS and Linux:** build a target-specific, self-contained launcher with
> Cemu, the multiplayer client, and the dedicated server already included. See
> [`CrossPlatform/README.md`](CrossPlatform/README.md).

**There are four important folders in this project listed below:**

## [C#](C%23)
- This project contains the implementation for the server.
- The solution is located at `C#\GUIApp\GUIApp.sln`.
- This project is implemented on C# and uses sockets for the connectivity.

## [DLL](DLL/InjectDLL)
- This project contains the implementation for the code that is injected into the emulator.
- The solution is located at `DLL\InjectDLL\InjectDLL.sln`.
- The entrypoint for this project is located at `DLL\InjectDLL\dllmain.cpp`.
- Most of the implementation is based on finding important memory addresses to read and write the player information.
- This project is implemented on C++ and uses NamedPipes to communicate with the front end application.

## [WPF .NET 6](WPF%20.NET%206/Breath%20of%20the%20Wild%20Multiplayer)
- This projects contains the implementation for the front end application.
- The solution is located at `WPF .NET 6\Breath of the Wild Multiplayer\Breath of the Wild Multiplayer.sln`.
- This project is implemented in C# using WPF. This app works as an injector for our code and communicates with this code using NamedPipes.

## [BNP Files](BNP%20Files)
- This folder contains the bnp files that need to be installed in order to get the mod working.

# Building the project
Building the project should not be too complicated and this process can be automated using the [python script](buildWPF.py).

Build the complete launcher for the current machine with one of these targets.
The script creates its Python environment, installs packaging dependencies, and
builds every required component automatically; no separate setup command is
required:

```sh
./scripts/build-bundled-launcher.sh mac_x86_64
./scripts/build-bundled-launcher.sh mac_arm64_Metal
./scripts/build-bundled-launcher.sh Linux_x86_64
./scripts/build-bundled-launcher.sh Linux_arm64
```

The selected target is fixed at compile time and is not shown as an option in
the finished launcher. Each package contains its matching patched Cemu, native
multiplayer client, and self-contained dedicated server.

# Usage
- You can use the project in any way you prefer.
- You can make any modifications to the project, but you cannot redistribute it unless you have modified a substantial portion of the project's code. Modification to non functional parts of the code, do not count as code modification.
