#!/usr/bin/env python3
"""Prepare isolated desktop clients, or run their loopback multiplayer session."""
import argparse
import configparser
import platform
import json
import os
from pathlib import Path
import plistlib
import shutil
import signal
import socket
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'CrossPlatform'))
import milkbar_launcher as launcher


def copy_once(source, target):
    if target.exists():
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    if sys.platform == 'darwin':
        # APFS clones have independent writes without duplicating game data.
        subprocess.run(['cp', '-cR', str(source), str(target)], check=True)
    elif source.is_dir():
        shutil.copytree(source, target)
    else:
        shutil.copy2(source, target)


def prepare(args):
    source = args.source.resolve()
    config = json.loads((source / 'config.json').read_text())
    game = args.game.expanduser().resolve()
    if game.is_dir():
        game = game / 'code/U-King.rpx'
    if not game.is_file():
        raise RuntimeError(f'Base game not found: {game}')
    suffix = launcher._botw_suffix(dict(config, game_rpx=str(game)))
    graphics = source / 'cemu/config/graphicPacks'
    if not launcher._file_contains_all(graphics / 'BreathOfTheWild_UKMM/content/Pack/TitleBG.pack', launcher.REQUIRED_MULTIPLAYER_GAME_DATA):
        raise RuntimeError('Prepare multiplayer with the desktop launcher first.')
    launcher._require_extended_memory_pack(source / 'cemu/config')
    for label in ('a', 'b'):
        if (args.directory / label).exists():
            raise RuntimeError(f'Profile {label} already exists; choose a fresh --directory to preserve saves.')
    for index, label in enumerate(('a', 'b')):
        dest = args.directory / label
        if (dest / 'config.json').exists():
            raise RuntimeError(f'{dest} already exists; choose a fresh --directory to preserve its saves.')
        dest.mkdir(parents=True, exist_ok=True)
        cemu_config = dest / 'cemu/config'
        cemu_config.mkdir(parents=True, exist_ok=True)
        for name in ('settings.xml', 'graphicPacks', 'controllerProfiles', 'shaderCache', '.milkbar-metal-samplers-v2'):
            if (source / 'cemu/config' / name).exists():
                copy_once(source / 'cemu/config' / name, cemu_config / name)
        for name in ('usr/save', 'sys'):
            if (source / 'cemu/mlc01' / name).exists():
                copy_once(source / 'cemu/mlc01' / name, dest / 'cemu/mlc01' / name)
        for title in ('0005000e', '0005000c'):
            installed = source / f'cemu/mlc01/usr/title/{title}/{suffix}'
            if installed.is_dir():
                copy_once(installed, dest / f'cemu/mlc01/usr/title/{title}/{suffix}')
            elif title == '0005000e':
                raise RuntimeError('Install the matching v208 update in the source launcher first.')
        for name in ('ArmorMapping.txt', 'WeaponDamages.txt', 'QuestFlags.txt', 'QuestFlagsNames.txt'):
            copy_once(launcher.APPDATA_FILES / name, dest / name)
        client = dict(config, game_rpx=str(game), player_name=f'Local {label.upper()}',
                      server_host='127.0.0.1', server_port=args.port, server_password='', server_name='Local sync test')
        if sys.platform == 'darwin':
            executable = Path(config['cemu'])
            app = next(p for p in executable.parents if p.suffix == '.app')
            copied = dest / f'Local {label.upper()}.app'
            copy_once(app, copied)
            info_path = copied / 'Contents/Info.plist'
            info = plistlib.loads(info_path.read_bytes())
            info.update(CFBundleIdentifier=f'app.hyruletogether.localtest.{label}', CFBundleName=f'Hyrule Local {label.upper()}')
            info_path.write_bytes(plistlib.dumps(info))
            subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(copied)], check=True)
            client['cemu'] = str(copied / executable.relative_to(app))
        tree = ET.parse(cemu_config / 'settings.xml')
        settings = tree.getroot()
        packs = settings.find('GraphicPack')
        if packs is not None:
            for entry in list(packs):
                if not any(name in entry.get('filename', '') for name in ('BreathOfTheWild_UKMM/', 'Mods/ExtendedMemory/', 'Mods/FPS++/')):
                    packs.remove(entry)
            for preset in packs.iter('Preset'):
                if preset.findtext('category') == 'Framerate Limit':
                    preset.find('preset').text = '30FPS (ideal for 240/120/60Hz displays)'
        for tag, x, y in [('window_position', index * 650, 40), ('window_size', 640, 400)]:
            node = settings.find(tag)
            if node is not None:
                node.find('x').text = str(x)
                node.find('y').text = str(y)
        tree.write(cemu_config / 'settings.xml', encoding='utf-8', xml_declaration=True)
        (dest / 'config.json').write_text(json.dumps(client, indent=2) + '\n')
    args.directory.joinpath('ServerConfig.ini').write_text(f'''[Connection]
IP=127.0.0.1
Port={args.port}
Password=
[ServerInformation]
Description=Local two-client synchronization test
[Gamemode]
DefaultGamemode=True
[DefaultGamemode]
Name=Custom
EnemySync=True
QuestSync=True
KorokSync=True
TowerSync=True
ShrineSync=True
LocationSync=True
DungeonSync=True
Special=0
''')
    print(f'Prepared independent profiles at {args.directory}')


def run(args):
    processes, logs = [], []
    ini = configparser.ConfigParser()
    ini.read(args.directory / 'ServerConfig.ini')
    if ini['Connection']['IP'] != '127.0.0.1':
        raise RuntimeError('Local testing requires a loopback-only server.')
    port = ini.getint('Connection', 'Port')
    for label in ('a', 'b'):
        data = args.directory / label
        config = json.loads((data / 'config.json').read_text())
        if config['server_host'] != '127.0.0.1' or config['server_port'] != port:
            raise RuntimeError(f'Client {label} does not match the local server.')
        pidfile = data / 'cemu-session.pid'
        if pidfile.exists():
            try:
                os.kill(int(pidfile.read_text()), 0)
            except ProcessLookupError:
                pass
            else:
                raise RuntimeError(f'Client {label} is already running.')
    server_data = args.directory / 'server-data'
    server_data.mkdir(exist_ok=True)
    try:
        # Check without connecting a probe to the game's binary protocol.
        with socket.socket() as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            probe.bind(('127.0.0.1', port))
        server_log = (args.directory / 'server.log').open('a')
        logs.append(server_log)
        server = subprocess.Popen([str(args.server.resolve()), '--config', str(args.directory / 'ServerConfig.ini'), '--non-interactive'],
                                  stdin=subprocess.PIPE, stdout=server_log, stderr=subprocess.STDOUT, start_new_session=True,
                                  env=dict(os.environ, MILKBAR_DATA_DIR=str(server_data)))
        processes.append(server)
        time.sleep(1)
        if server.poll() is not None:
            raise RuntimeError('Server exited; inspect server.log')
        for label in ('a', 'b'):
            data = args.directory / label
            log = (data / 'launcher.log').open('a')
            logs.append(log)
            env = dict(os.environ, MILKBAR_DATA_DIR=str(data))
            processes.append(subprocess.Popen([sys.executable, '-u', str(ROOT / 'CrossPlatform/milkbar_launcher.py'), 'launch'],
                                              env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True))
        print(f'Two clients running. Logs: {args.directory}. Ctrl-C stops this test session.', flush=True)
        while all(p.poll() is None for p in processes):
            time.sleep(1)
    finally:
        for process in reversed(processes):
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        for process in processes:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        for log in logs:
            log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'run'])
    parser.add_argument('--directory', type=Path, default=ROOT / 'Build/local-multiplayer')
    parser.add_argument('--source', type=Path, default=launcher.data_directory())
    parser.add_argument('--game', type=Path)
    parser.add_argument('--port', type=int, default=5051)
    parser.add_argument('--server', type=Path, default=ROOT / 'Build/server' / (('osx' if sys.platform == 'darwin' else 'linux') + ('-arm64' if platform.machine().lower() in ('arm64', 'aarch64') else '-x64')) / 'MBL.DedicatedServer')
    args = parser.parse_args()
    args.directory = args.directory.expanduser().resolve()
    if args.action == 'prepare' and not args.game:
        parser.error('prepare requires --game pointing to the base game folder or RPX')
    def stop_requested(_signum, _frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop_requested)
    try:
        (prepare if args.action == 'prepare' else run)(args)
    except KeyboardInterrupt:
        pass
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        parser.exit(1, f'Error: {error}\n')


if __name__ == '__main__':
    main()
