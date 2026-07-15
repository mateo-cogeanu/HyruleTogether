#!/usr/bin/env python3
"""Native macOS/Linux launcher for Hyrule Together's Cemu client."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import socket
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any


ROOT = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))
APPDATA_FILES = ROOT / "WPF .NET 6" / "Breath of the Wild Multiplayer" / "AppdataFiles"
CEMU_RUNTIMES = {
    "mac_x86_64": "Mac x86_64",
    "mac_arm64_Metal": "Mac arm64 — Metal",
    "Linux_x86_64": "Linux x86_64",
    "Linux_arm64": "Linux arm64",
}

UKMM_DEPLOYMENT_SCHEMA = b"stable-title-bg-v2"
REQUIRED_MULTIPLAYER_GAME_DATA = (
    b"Jugador1_Hold",
    b"Jugador1_animationthing",
    b"Jugador1_AttackAnimation",
)


def data_directory() -> Path:
    override = os.environ.get("MILKBAR_DATA_DIR")
    if override:
        return Path(override).expanduser()
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "MilkBarLauncher"
    return Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")) / "MilkBarLauncher"


def config_path() -> Path:
    return data_directory() / "config.json"


def cemu_session_path() -> Path:
    return data_directory() / "cemu-session.pid"


def cemu_session_active() -> bool:
    path = cemu_session_path()
    try:
        pid = int(path.read_text(encoding="ascii").strip())
        os.kill(pid, 0)
        return True
    except (FileNotFoundError, ValueError, ProcessLookupError):
        path.unlink(missing_ok=True)
        return False
    except PermissionError:
        return True


def set_cemu_session(pid: int) -> None:
    path = cemu_session_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(str(pid), encoding="ascii")


def clear_cemu_session(pid: int | None = None) -> None:
    path = cemu_session_path()
    if pid is not None:
        try:
            if int(path.read_text(encoding="ascii").strip()) != pid:
                return
        except (FileNotFoundError, ValueError):
            return
    path.unlink(missing_ok=True)


def external_process_environment() -> dict[str, str]:
    """Return an environment safe for native programs launched by the bundle."""
    environment = os.environ.copy()
    if sys.platform == "linux" and getattr(sys, "frozen", False):
        # PyInstaller prepends its _internal directory to LD_LIBRARY_PATH so
        # its own Qt application can start. Letting an external Cemu inherit
        # that path mixes the bundled X11/GTK/GLib libraries with the host's
        # Vulkan driver and can crash in XGetXCBConnection. PyInstaller saves
        # the user's original value for precisely this child-process case.
        original = environment.pop("LD_LIBRARY_PATH_ORIG", None)
        if original:
            environment["LD_LIBRARY_PATH"] = original
        else:
            environment.pop("LD_LIBRARY_PATH", None)
    return environment


def cemu_process_environment(cemu: Path | str) -> dict[str, str]:
    """Return the clean environment required by this bundled Cemu build."""
    environment = external_process_environment()
    if sys.platform != "linux":
        return environment
    metadata_path = Path(cemu).expanduser().resolve().parent / "runtime.json"
    try:
        display_backend = json.loads(metadata_path.read_text(encoding="utf-8")).get("display_backend")
    except (OSError, ValueError, AttributeError):
        display_backend = None
    if display_backend == "x11":
        # wxGTK otherwise follows a Wayland desktop session even though this
        # Cemu binary has no Wayland window-system support. Force its supported
        # X11 path so Vulkan receives a valid XWayland surface.
        environment["GDK_BACKEND"] = "x11"
    return environment


def preloadable_client_library(library: Path) -> Path:
    """Stage Linux's preload library at a path the ELF loader can parse."""
    if sys.platform != "linux" or not any(character.isspace() or character == ":" for character in str(library)):
        return library

    # ld.so tokenizes LD_PRELOAD on spaces as well as colons and provides no
    # quoting syntax. A branded bundle directory such as "Hyrule Together"
    # therefore cannot be referenced directly. Cache this exact client build
    # under a private, delimiter-free path and preload that copy.
    cache_directory = data_directory() / "native-client"
    if any(character.isspace() or character == ":" for character in str(cache_directory)):
        cache_directory = Path("/tmp") / f"hyrule-together-{os.getuid()}" / "native-client"
    cache_directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    cache_directory.chmod(0o700)

    digest = hashlib.sha256()
    with library.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    destination = cache_directory / f"libMilkBarClient-{digest.hexdigest()[:16]}.so"
    if not destination.is_file():
        temporary = cache_directory / f".{destination.name}.{os.getpid()}.tmp"
        shutil.copy2(library, temporary)
        os.replace(temporary, destination)
    for old_library in cache_directory.glob("libMilkBarClient-*.so"):
        if old_library != destination:
            old_library.unlink(missing_ok=True)
    return destination


def bundled_runtime_root() -> Path | None:
    executable = Path(sys.executable).resolve()
    candidates = [
        executable.parent / "runtime",
        executable.parent.parent / "Resources" / "runtime",
        ROOT / "runtime",
    ]
    return next((path for path in candidates if path.is_dir()), None)


def bundled_cemu_executable() -> Path | None:
    runtime = bundled_runtime_root()
    if runtime is None:
        return None
    if sys.platform == "darwin":
        binaries = sorted((runtime / "cemu" / "Cemu.app" / "Contents" / "MacOS").glob("Cemu_*"))
        return binaries[0] if binaries else None
    candidate = runtime / "cemu" / "Cemu"
    return candidate if candidate.is_file() else None


def bundled_client_library() -> Path | None:
    runtime = bundled_runtime_root()
    if runtime is None:
        return None
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    candidate = runtime / "client" / f"libMilkBarClient{suffix}"
    return candidate if candidate.is_file() else None


def bundled_ukmm_executable() -> Path | None:
    runtime = bundled_runtime_root()
    candidate = runtime / "tools" / "ukmm" if runtime else None
    return candidate if candidate and candidate.is_file() else None


def bundled_model_builder() -> Path | None:
    runtime = bundled_runtime_root()
    candidate = runtime / "tools" / "milkbar-model-builder" if runtime else None
    return candidate if candidate and candidate.is_file() else None


def bundled_mod_archive() -> Path | None:
    runtime = bundled_runtime_root()
    candidate = runtime / "mod" / "MilkBarLauncher.bnp" if runtime else None
    return candidate if candidate and candidate.is_file() else None


def host_cemu_runtime() -> str:
    machine = platform.machine().lower()
    if sys.platform == "darwin":
        return "mac_arm64_Metal" if machine in ("arm64", "aarch64") else "mac_x86_64"
    return "Linux_arm64" if machine in ("arm64", "aarch64") else "Linux_x86_64"


def managed_cemu_executable(runtime: str) -> Path:
    root = data_directory() / "runtimes" / "cemu" / runtime
    if runtime.startswith("mac_"):
        app_executable = root / "Cemu.app" / "Contents" / "MacOS"
        if app_executable.is_dir():
            matches = sorted(path for path in app_executable.iterdir() if path.name.startswith("Cemu_"))
            if matches:
                return matches[0]
        return app_executable / "Cemu_release"
    return root / "Cemu"


def default_library() -> Path:
    bundled = bundled_client_library()
    if bundled:
        return bundled
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    return ROOT / "Build" / "cross-platform" / f"libMilkBarClient{suffix}"


def discover_cemu() -> str:
    bundled = bundled_cemu_executable()
    if bundled:
        return str(bundled)
    managed = managed_cemu_executable(host_cemu_runtime())
    if managed.is_file():
        return str(managed)
    configured = os.environ.get("CEMU_PATH")
    if configured:
        return configured
    if sys.platform == "darwin":
        candidates = [
            Path("/Applications/Cemu.app/Contents/MacOS/Cemu"),
            Path.home() / "Applications" / "Cemu.app" / "Contents" / "MacOS" / "Cemu",
        ]
        for candidate in candidates:
            if candidate.exists():
                return str(candidate)
    for name in ("Cemu", "cemu", "Cemu.AppImage"):
        found = shutil.which(name)
        if found:
            return found
    return ""


def defaults() -> dict[str, Any]:
    return {
        "cemu": discover_cemu(),
        "cemu_runtime": host_cemu_runtime(),
        "client_library": str(default_library()),
        "game_rpx": "",
        "title_id": "",
        "player_name": "Link",
        "server_name": "Hyrule Together",
        "server_host": "127.0.0.1",
        "server_port": 5050,
        "server_password": "",
    }


def load_config() -> dict[str, Any]:
    result = defaults()
    path = config_path()
    if path.exists():
        result.update(json.loads(path.read_text(encoding="utf-8")))
    bundled_cemu = bundled_cemu_executable()
    bundled_client = bundled_client_library()
    if bundled_cemu and bundled_client:
        result["cemu"] = str(bundled_cemu)
        result["client_library"] = str(bundled_client)
        result["cemu_runtime"] = host_cemu_runtime()
    return result


def save_config(config: dict[str, Any]) -> None:
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    for filename in ("QuestFlags.txt", "QuestFlagsNames.txt", "WeaponDamages.txt", "ArmorMapping.txt"):
        source = APPDATA_FILES / filename
        destination = path.parent / filename
        if source.exists() and not destination.exists():
            shutil.copy2(source, destination)


def normalize_cemu(value: str) -> Path:
    path = Path(value).expanduser()
    if sys.platform == "darwin" and path.suffix == ".app":
        path = path / "Contents" / "MacOS" / "Cemu"
    return path.resolve()


def validation_errors(config: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if sys.platform not in ("darwin", "linux"):
        errors.append("This native launcher supports macOS and Linux.")
    cemu = normalize_cemu(str(config.get("cemu", "")))
    runtime = str(config.get("cemu_runtime", host_cemu_runtime()))
    if not bundled_cemu_executable():
        if runtime not in CEMU_RUNTIMES:
            errors.append(f"Unknown Cemu runtime: {runtime}")
        elif runtime != host_cemu_runtime():
            errors.append(f"{CEMU_RUNTIMES[runtime]} cannot run on this host; select {CEMU_RUNTIMES[host_cemu_runtime()] }.")
    library = Path(str(config.get("client_library", ""))).expanduser().resolve()
    if not cemu.is_file():
        errors.append(f"Cemu executable not found: {cemu}")
    elif not os.access(cemu, os.X_OK):
        errors.append(f"Cemu is not executable: {cemu}")
    elif not cemu_has_hooks(cemu):
        errors.append("Cemu does not export the native hooks required by Hyrule Together; patch and build current Cemu with scripts/patch-cemu.sh.")
    if not library.is_file():
        errors.append(f"Native client not found: {library} (run scripts/build-native.sh)")
    game_rpx = str(config.get("game_rpx", ""))
    title_id = str(config.get("title_id", ""))
    if not game_rpx and not title_id:
        errors.append("Set game_rpx or title_id in the configuration.")
    elif game_rpx and not Path(game_rpx).expanduser().is_file():
        errors.append(f"U-King.rpx not found: {game_rpx}")
    try:
        port = int(config.get("server_port", 0))
        if not 1 <= port <= 65535:
            raise ValueError
    except (TypeError, ValueError):
        errors.append("server_port must be between 1 and 65535.")
    return errors


def cemu_has_hooks(cemu: Path) -> bool:
    command = ["nm", "-gU", str(cemu)] if sys.platform == "darwin" else ["nm", "-D", str(cemu)]
    try:
        symbols = subprocess.run(command, capture_output=True, text=True, timeout=10, check=False).stdout
    except (OSError, subprocess.TimeoutExpired):
        return False
    return ("memory_getBase" in symbols
            and "osLib_registerHLEFunction" in symbols
            and "milkbar_isTitleActive" in symbols)


def _botw_suffix(config: dict[str, Any]) -> str:
    game_rpx = Path(str(config.get("game_rpx", ""))).expanduser()
    meta = game_rpx.parent.parent / "meta" / "meta.xml"
    if not meta.is_file():
        raise RuntimeError("BOTW meta/meta.xml was not found beside the selected U-King.rpx")
    root = ET.parse(meta).getroot()
    title_id = next((str(node.text).strip().lower() for node in root.iter()
                     if node.tag.rsplit("}", 1)[-1] == "title_id" and node.text), "")
    if len(title_id) != 16 or title_id[8:] not in {"101c9300", "101c9400", "101c9500"}:
        raise RuntimeError(f"The selected game is not a recognized BOTW Wii U dump ({title_id or 'no title ID'})")
    return title_id[8:]


def _configure_cemu_settings(directory: Path) -> None:
    target_api = "2" if sys.platform == "darwin" and platform.machine().lower() in ("arm64", "aarch64") else "1"
    merged_rules = "graphicPacks/BreathOfTheWild_UKMM/rules.txt"
    extended_memory_rules = "graphicPacks/downloadedGraphicPacks/BreathOfTheWild/Mods/ExtendedMemory/rules.txt"
    old_rules = "graphicPacks/MilkBarLauncher/rules.txt"
    settings = directory / "settings.xml"
    if settings.exists():
        tree = ET.parse(settings)
        root = tree.getroot()
    else:
        root = ET.Element("content")
        tree = ET.ElementTree(root)
    changed = not settings.exists()
    legacy_graphic = root.find("graphic")
    if legacy_graphic is not None:
        root.remove(legacy_graphic)
        changed = True
    graphic = root.find("Graphic")
    if graphic is None:
        graphic = ET.SubElement(root, "Graphic")
        changed = True
    api = graphic.find("api")
    if api is None:
        api = ET.SubElement(graphic, "api")
    if api.text != target_api:
        api.text = target_api
        changed = True
    packs = root.find("GraphicPack")
    if packs is None:
        packs = ET.SubElement(root, "GraphicPack")
        changed = True
    for entry in list(packs.findall("Entry")):
        if entry.get("filename") == old_rules:
            packs.remove(entry)
            changed = True
    merged_pack = directory / "graphicPacks" / "BreathOfTheWild_UKMM" / "rules.txt"
    if merged_pack.is_file() and not any(entry.get("filename") == merged_rules for entry in packs.findall("Entry")):
        ET.SubElement(packs, "Entry", filename=merged_rules)
        changed = True
    extended_memory_pack = directory / extended_memory_rules
    if (extended_memory_pack.is_file()
            and not any(entry.get("filename") == extended_memory_rules for entry in packs.findall("Entry"))):
        ET.SubElement(packs, "Entry", filename=extended_memory_rules)
        changed = True
    if changed:
        directory.mkdir(parents=True, exist_ok=True)
        tree.write(settings, encoding="utf-8", xml_declaration=True)


def _require_extended_memory_pack(directory: Path) -> None:
    rules = directory / "graphicPacks" / "downloadedGraphicPacks" / "BreathOfTheWild" / "Mods" / "ExtendedMemory" / "rules.txt"
    if not rules.is_file():
        raise RuntimeError(
            "Hyrule Together requires BOTW's Extended Memory graphic pack. "
            "Open Manage Cemu Graphic Packs, download the latest community packs, then launch again."
        )


def _file_contains_all(path: Path, needles: tuple[bytes, ...]) -> bool:
    if not path.is_file() or not needles:
        return False
    longest = max(map(len, needles))
    missing = set(needles)
    overlap = b""
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            block = overlap + chunk
            missing = {needle for needle in missing if needle not in block}
            if not missing:
                return True
            overlap = block[-(longest - 1):] if longest > 1 else b""
    return False


def _files_identical(first: Path, second: Path) -> bool:
    if not first.is_file() or not second.is_file() or first.stat().st_size != second.stat().st_size:
        return False
    with first.open("rb") as left, second.open("rb") as right:
        while left_chunk := left.read(1024 * 1024):
            if left_chunk != right.read(len(left_chunk)):
                return False
    return True


def _deploy_final_ukmm_merge(merged_root: Path, output_pack: Path) -> None:
    """Replace UKMM's incremental output with its completed merged trees."""
    merged_content = merged_root / "content"
    if not merged_content.is_dir():
        raise RuntimeError("UKMM completed without producing its final merged content")

    for name in ("content", "aoc"):
        source = merged_root / name
        destination = output_pack / name
        shutil.rmtree(destination, ignore_errors=True)
        if source.is_dir():
            shutil.copytree(source, destination)

    title_bg = output_pack / "content" / "Pack" / "TitleBG.pack"
    if not _file_contains_all(title_bg, REQUIRED_MULTIPLAYER_GAME_DATA):
        raise RuntimeError(
            "UKMM's final TitleBG.pack is missing Hyrule Together's animation controls"
        )


def _restore_stable_title_bg(update_content: Path, output_pack: Path) -> None:
    """Preserve Nintendo's TitleBG layout so remote actors remain alive."""
    source = update_content / "Pack" / "TitleBG.pack"
    destination = output_pack / "content" / "Pack" / "TitleBG.pack"
    if not source.is_file():
        raise RuntimeError("The BOTW update is missing its original TitleBG.pack")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    if not _files_identical(source, destination):
        raise RuntimeError("Could not preserve the original BOTW TitleBG.pack")


def install_bundled_mod(config: dict[str, Any], force: bool = False) -> Path:
    source_tool = bundled_ukmm_executable()
    archive = bundled_mod_archive()
    model_builder = bundled_model_builder()
    if source_tool is None or archive is None or model_builder is None:
        raise RuntimeError("The bundled UKMM merger, model builder, or Hyrule Together mod archive is missing")
    suffix = _botw_suffix(config)
    game_root = Path(str(config["game_rpx"])).expanduser().resolve().parent.parent
    base_content = game_root / "content"
    cemu_root = data_directory() / "cemu"
    update_root = cemu_root / "mlc01" / "usr" / "title" / "0005000e" / suffix
    dlc_root = cemu_root / "mlc01" / "usr" / "title" / "0005000c" / suffix
    update_content = update_root / "content"
    if not base_content.is_dir() or not update_content.is_dir():
        raise RuntimeError("Install the BOTW v208 update before preparing the bundled multiplayer mod")
    aoc_content = dlc_root / "content" / "0010"
    if not aoc_content.is_dir():
        aoc_content = None

    patch_source = archive.parent / "patches"
    patches = sorted(patch_source.glob("patch_*.asm"))
    if not patches:
        raise RuntimeError("The bundled Hyrule Together code patches are missing")
    signature_data = [UKMM_DEPLOYMENT_SCHEMA, archive.read_bytes(), model_builder.read_bytes(),
                      str(update_root.stat().st_mtime_ns).encode()]
    for patch in patches:
        signature_data.extend((patch.name.encode(), patch.read_bytes()))
    if aoc_content:
        signature_data.append(str(dlc_root.stat().st_mtime_ns).encode())
    signature = hashlib.sha256(b"\0".join(signature_data)).hexdigest()
    graphics = cemu_root / "config" / "graphicPacks"
    output_pack = graphics / "BreathOfTheWild_UKMM"
    marker = output_pack / ".milkbar-signature"
    if not force and marker.is_file() and marker.read_text(encoding="utf-8").strip() == signature:
        active_title_bg = output_pack / "content" / "Pack" / "TitleBG.pack"
        vanilla_title_bg = update_content / "Pack" / "TitleBG.pack"
        if _files_identical(active_title_bg, vanilla_title_bg):
            _require_extended_memory_pack(cemu_root / "config")
            _configure_cemu_settings(cemu_root / "config")
            return output_pack

    ukmm_root = cemu_root / "ukmm"
    local_tool = ukmm_root / "ukmm"
    if not local_tool.is_file() or local_tool.stat().st_size != source_tool.stat().st_size:
        ukmm_root.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_tool, local_tool)
        local_tool.chmod(0o755)
    shutil.rmtree(ukmm_root / "config", ignore_errors=True)
    shutil.rmtree(ukmm_root / "storage", ignore_errors=True)
    shutil.rmtree(output_pack, ignore_errors=True)
    shutil.rmtree(graphics / "MilkBarLauncher", ignore_errors=True)
    (ukmm_root / "config").mkdir(parents=True, exist_ok=True)
    settings = {
        "current_mode": "WiiU", "system_7z": False,
        "storage_dir": str(ukmm_root / "storage"), "check_updates": "None",
        "show_changelog": False, "last_version": "0.17.1",
        "wiiu_config": {
            "language": "EUen" if suffix == "101c9500" else ("JPja" if suffix == "101c9300" else "USen"),
            "profile": "Default",
            "dump": {
                "bin_type": "Nintendo",
                "source": {"type": "Unpacked", "host_path": str(game_root.parent),
                           "content_dir": str(base_content), "update_dir": str(update_content),
                           "aoc_dir": str(aoc_content) if aoc_content else None},
                "endian": "Big",
            },
            "deploy_config": {"output": str(graphics), "method": "Copy", "auto": True,
                              "cemu_rules": True, "executable": None, "layout": "WithName"},
        },
        "switch_config": None, "lang": "English",
    }
    (ukmm_root / "config" / "settings.yml").write_text(json.dumps(settings, indent=2), encoding="utf-8")
    environment = external_process_environment()
    environment.update({
        "MILKBAR_UKMM_BASE": str(base_content),
        "MILKBAR_UKMM_UPDATE": str(update_content),
        "MILKBAR_UKMM_AOC": str(aoc_content) if aoc_content else "",
        "MILKBAR_UKMM_OUTPUT": str(graphics),
        "MILKBAR_UKMM_STORAGE": str(ukmm_root / "storage"),
        "MILKBAR_UKMM_LANGUAGE": "EUen" if suffix == "101c9500" else ("JPja" if suffix == "101c9300" else "USen"),
    })
    result = subprocess.run(
        [str(local_tool), "--portable", "--deploy", "install", str(archive)],
        capture_output=True, text=True, timeout=900, check=False, env=environment,
    )
    if result.returncode:
        details = (result.stdout + "\n" + result.stderr).strip()
        raise RuntimeError(f"Hyrule Together mod merge failed:\n{details[-3000:]}")
    if not (output_pack / "rules.txt").is_file():
        raise RuntimeError("UKMM completed without producing the Hyrule Together Cemu graphic pack")
    _deploy_final_ukmm_merge(
        ukmm_root / "storage" / "wiiu" / "profiles" / "Default" / "merged",
        output_pack,
    )
    model_result = subprocess.run(
        [str(model_builder), str(base_content), str(update_content), str(output_pack / "content")],
        capture_output=True, text=True, timeout=900, check=False,
    )
    if model_result.returncode:
        details = (model_result.stdout + "\n" + model_result.stderr).strip()
        raise RuntimeError(f"Hyrule Together player model creation failed:\n{details[-3000:]}")
    model_name = "Jugador1ModelNameLongForASpecificReason"
    for suffix_name in (".sbfres", ".Tex1.sbfres", ".Tex2.sbfres"):
        model_path = output_pack / "content" / "Model" / f"{model_name}{suffix_name}"
        if not model_path.is_file() or model_path.stat().st_size < 1024:
            raise RuntimeError(f"Hyrule Together player model creation did not produce {model_path.name}")
    animation_path = output_pack / "content" / "Model" / "Player_Animation_NoFace.sbfres"
    if not animation_path.is_file() or animation_path.stat().st_size < 1024:
        raise RuntimeError("Hyrule Together player model creation did not produce Player_Animation_NoFace.sbfres")
    _restore_stable_title_bg(update_content, output_pack)
    for patch in patches:
        shutil.copy2(patch, output_pack / patch.name)
    marker.write_text(signature, encoding="utf-8")
    shader_cache = cemu_root / "config" / "shaderCache"
    for cache in shader_cache.glob("**/00050000101c9*"):
        if cache.is_file():
            cache.unlink()
    _require_extended_memory_pack(cemu_root / "config")
    _configure_cemu_settings(cemu_root / "config")
    return output_pack


def prepare_cemu_config() -> Path:
    directory = data_directory() / "cemu" / "config"
    directory.mkdir(parents=True, exist_ok=True)
    # Generated Metal shaders embed the sampler argument layout. Invalidate
    # caches once when the bundled renderer mapping changes so an older broken
    # pipeline cannot survive a launcher update.
    renderer_marker = directory / ".milkbar-metal-samplers-v2"
    if sys.platform == "darwin" and not renderer_marker.exists():
        for cache in (directory / "shaderCache").glob("**/00050000101c9*"):
            if cache.is_file():
                cache.unlink()
        renderer_marker.write_text("deduplicated-16-slot-sampler-map\n", encoding="utf-8")
    _configure_cemu_settings(directory)
    return directory


def command_configure(args: argparse.Namespace) -> int:
    config = load_config()
    updates = {
        "cemu": args.cemu,
        "cemu_runtime": args.cemu_runtime,
        "client_library": args.client_library,
        "game_rpx": args.game_rpx,
        "title_id": args.title_id,
        "player_name": args.player_name,
        "server_name": args.server_name,
        "server_host": args.server_host,
        "server_port": args.server_port,
        "server_password": args.server_password,
    }
    config.update({key: value for key, value in updates.items() if value is not None})
    save_config(config)
    print(f"Saved {config_path()}")
    return 0


def command_doctor(_: argparse.Namespace) -> int:
    config = load_config()
    save_config(config)
    print(f"Platform: {platform.system()} {platform.machine()}")
    print(f"Configuration: {config_path()}")
    print(f"Cemu: {config.get('cemu') or '(not found)'}")
    print(f"Native client: {config.get('client_library')}")
    errors = validation_errors(config)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("Ready for Cemu 2.6 or newer.")
    return 0


def receive_reply(connection: socket.socket) -> str:
    data = connection.recv(2048)
    if not data:
        raise RuntimeError("The native client closed the IPC connection")
    return data.rstrip(b"\0").decode("utf-8", errors="replace")


def send_instruction(connection: socket.socket, instruction: str) -> None:
    connection.sendall((instruction + ";[END]").encode("utf-8"))
    reply = receive_reply(connection)
    if "Succeeded" not in reply:
        raise RuntimeError(f"Native client rejected instruction: {reply or '(empty response)'}")


def command_launch(_: argparse.Namespace) -> int:
    if cemu_session_active():
        print("ERROR: Close Cemu or the graphic-pack manager before starting the game.", file=sys.stderr)
        return 1
    config = load_config()
    errors = validation_errors(config)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    if bundled_mod_archive():
        try:
            install_bundled_mod(config)
        except (OSError, RuntimeError, subprocess.SubprocessError, ET.ParseError) as error:
            print(f"ERROR: {error}", file=sys.stderr)
            return 1

    cemu = normalize_cemu(str(config["cemu"]))
    try:
        library = preloadable_client_library(
            Path(str(config["client_library"])).expanduser().resolve())
    except OSError as error:
        print(f"ERROR: Could not stage the native client for Linux: {error}", file=sys.stderr)
        return 1
    ipc_path = data_directory() / "milkbar.sock"
    ipc_path.parent.mkdir(parents=True, exist_ok=True)
    ipc_path.unlink(missing_ok=True)

    listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    listener.bind(str(ipc_path))
    listener.listen(1)
    listener.settimeout(120)

    environment = cemu_process_environment(cemu)
    environment["MILKBAR_IPC_PATH"] = str(ipc_path)
    environment["MILKBAR_DATA_DIR"] = str(data_directory())
    environment["MILKBAR_CEMU_DATA_DIR"] = str(prepare_cemu_config())
    environment["MILKBAR_WAIT_FOR_HOOKS"] = "1"
    preload_name = "DYLD_INSERT_LIBRARIES" if sys.platform == "darwin" else "LD_PRELOAD"
    existing = environment.get(preload_name, "")
    environment[preload_name] = str(library) + ((":" + existing) if existing else "")

    command = [str(cemu)]
    command += ["--mlc", str(data_directory() / "cemu" / "mlc01")]
    if config.get("game_rpx"):
        command += ["-g", str(Path(str(config["game_rpx"])).expanduser().resolve())]
    else:
        command += ["--title-id", str(config["title_id"])]

    print("Starting Cemu…")
    process = subprocess.Popen(command, env=environment)
    set_cemu_session(process.pid)
    try:
        connection, _ = listener.accept()
        with connection:
            selected = str(config.get("selected_model", "Link"))
            npc_data = {}
            npc_file = ROOT / "WPF .NET 6" / "Breath of the Wild Multiplayer" / "Resources" / "NpcData.json"
            if npc_file.exists():
                npc_data = {item["Folder"].removeprefix("Npc_"): item for item in json.loads(npc_file.read_text(encoding="utf-8-sig"))}
            if selected == "Link" or selected in ("BumiiMaker", "Environmental"):
                character_type, model, model_name = "0", "Jugador1ModelNameLongForASpecificReason", "Link"
            else:
                character = npc_data.get(selected, {"Folder": f"Npc_{selected}", "Name": selected.replace("_", " ")})
                character_type, model, model_name = "1", character["Folder"], character["Name"]

            connect = ";".join([
                "!connect",
                str(config["server_host"]),
                str(config["server_port"]),
                str(config.get("server_password", "")),
                str(config["player_name"]),
                str(config["server_name"]),
                character_type,
                f"{model}:{model_name}",
            ])
            send_instruction(connection, connect)
            send_instruction(connection, "!startServerLoop")
            print("Connected. Hyrule Together is running; close Cemu to exit.")
            while process.poll() is None:
                try:
                    message = connection.recv(2048)
                except OSError:
                    break
                if not message:
                    break
                print(message.rstrip(b"\0").decode("utf-8", errors="replace"))
        return process.wait()
    except (TimeoutError, RuntimeError, OSError) as error:
        process.terminate()
        if isinstance(error, TimeoutError):
            print("ERROR: Native client did not connect to the launcher within 120 seconds.", file=sys.stderr)
            latest_log = data_directory() / "LatestLog.txt"
            if latest_log.is_file():
                try:
                    lines = latest_log.read_text(encoding="utf-8", errors="replace").splitlines()
                    if lines:
                        print(f"Last native-client status: {lines[-1]}", file=sys.stderr)
                except OSError:
                    pass
        else:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    finally:
        clear_cemu_session(process.pid)
        listener.close()
        ipc_path.unlink(missing_ok=True)


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description="Hyrule Together for native Cemu on macOS and Linux")
    commands = result.add_subparsers(dest="command", required=True)
    configure = commands.add_parser("configure", help="write launcher settings")
    for option in ("cemu", "cemu-runtime", "client-library", "game-rpx", "title-id", "player-name", "server-name", "server-host", "server-password"):
        configure.add_argument(f"--{option}", dest=option.replace("-", "_"))
    configure.add_argument("--server-port", type=int)
    configure.set_defaults(handler=command_configure)
    doctor = commands.add_parser("doctor", help="check the current setup")
    doctor.set_defaults(handler=command_doctor)
    launch = commands.add_parser("launch", help="start Cemu and join the configured server")
    launch.set_defaults(handler=command_launch)
    gui = commands.add_parser("gui", help="open the graphical launcher")
    gui.set_defaults(handler=lambda _args: subprocess.call([sys.executable, str(Path(__file__).with_name("milkbar_qt_gui.py"))]))
    return result


def main() -> int:
    args = parser().parse_args()
    return int(args.handler(args))


if __name__ == "__main__":
    raise SystemExit(main())
