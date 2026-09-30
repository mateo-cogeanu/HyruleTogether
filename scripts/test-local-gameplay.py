#!/usr/bin/env python3
"""Drive two prepared Cemu clients and report observed synchronization checks."""
import argparse
import configparser
import hashlib
import platform
import json
import math
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'CrossPlatform'))
from testing.dsu import Gamepad, write_profile


def records(path, kind=None, since=0):
    if not path.exists():
        return []
    result = []
    for line in path.read_text().splitlines():
        try:
            row = json.loads(line)
            if (kind is None or row['kind'] == kind) and row['time_ms'] >= since:
                result.append(row)
        except (ValueError, KeyError):
            pass  # The final append may still be in flight.
    return result


def distance(a, b):
    if not all(isinstance(x, (float, int)) and math.isfinite(x) for x in a + b):
        return math.inf
    return math.dist(a, b)


def live(path):
    samples = records(path, 'local', time.time() * 1000 - 2000)
    return bool(samples and samples[-1]['health'] > 0 and samples[-1]['map'] and
                10 < distance(samples[-1]['position'], [0, 0, 0]) < 1e6)


def wait_until(test, seconds, description, alive):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        alive()
        if test():
            return
        time.sleep(0.2)
    raise RuntimeError(f'Timed out: {description}')


def movement_result(local, remote, applied, start, end):
    sent = [r for r in local if start <= r['time_ms'] <= end]
    received = [r for r in remote if start <= r['time_ms'] <= end + 2000]
    rendered = [r for r in applied if start <= r['time_ms'] <= end + 2000]
    if len(sent) < 10 or len(received) < 10 or len(rendered) < 10:
        return dict(passed=False, reason='Missing local, received, or applied samples')
    displacement = distance(sent[0]['position'][::2], sent[-1]['position'][::2])
    matches = [min((distance(s['position'], r['position']) for r in received
                    if 0 <= r['time_ms'] - s['time_ms'] <= 2000), default=math.inf) for s in sent]
    applied_matches = [min((distance(s['position'], r['position']) for r in rendered
                           if 0 <= r['time_ms'] - s['time_ms'] <= 2000), default=math.inf) for s in sent]
    packet_ratio = sum(x < 1 for x in matches) / len(matches)
    applied_ratio = sum(x < 2 for x in applied_matches) / len(applied_matches)
    return dict(passed=displacement > 1 and packet_ratio >= .9 and applied_ratio >= .9,
                displacement=displacement, received_match_ratio=packet_ratio,
                applied_match_ratio=applied_ratio, sample_count=len(sent))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT / 'Build/local-multiplayer')
    parser.add_argument('--server', type=Path, default=ROOT / 'Build/server' / (('osx' if sys.platform == 'darwin' else 'linux') + ('-arm64' if platform.machine().lower() in ('arm64','aarch64') else '-x64')) / 'MBL.DedicatedServer')
    parser.add_argument('--boot-timeout', type=float, default=240)
    args = parser.parse_args()
    root = args.directory.resolve()
    for label in ('a', 'b'):
        config = json.loads((root / label / 'config.json').read_text())
        if config.get('player_name') != f'Local {label.upper()}' or config.get('server_host') != '127.0.0.1':
            parser.error('Use isolated profiles made by local-multiplayer.py prepare.')
        save = root / label / 'cemu/mlc01/usr/save'
        if not save.resolve().is_relative_to((root/label).resolve()):
            parser.error('Test saves must reside inside the isolated profile.')
        pid = root / label / 'cemu-session.pid'
        if pid.exists():
            try:
                os.kill(int(pid.read_text()), 0)
            except ProcessLookupError:
                pass
            else:
                parser.error(f'Client {label} is already running; close it first.')
    ini = configparser.ConfigParser()
    ini.read(root / 'ServerConfig.ini')
    if ini['Connection']['IP'] != '127.0.0.1':
        parser.error('Test server must bind loopback.')
    for label in ('a', 'b'):
        config = json.loads((root/label/'config.json').read_text())
        if config['server_port'] != ini.getint('Connection','Port'):
            parser.error(f'Client {label} must use the test server port.')
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(('127.0.0.1', ini.getint('Connection', 'Port')))
    run = root / 'runs' / time.strftime('%Y%m%d-%H%M%S')
    run.mkdir(parents=True)
    result = dict(passed=False, checks=[], scope='Controller-driven game and native synchronization telemetry; visual appearance requires separate review.')
    result['revision'] = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    result['native_client_sha256'] = hashlib.sha256(Path(config['client_library']).read_bytes()).hexdigest()
    result['actions'] = []
    processes, pads, logs, profiles = [], {}, [], []
    def alive():
        for label,pad in pads.items():
            if not pad.thread.is_alive():
                raise RuntimeError(f'{label} virtual gamepad stopped')
        for name, process in processes:
            if process.poll() is not None:
                raise RuntimeError(f'{name} exited with code {process.returncode}')
    def check(name, **values):
        result['checks'].append(dict(name=name, **values))
        print(json.dumps(result['checks'][-1]), flush=True)
    def spawn(name, command, env):
        log = (run / f'{name}.log').open('w'); logs.append(log)
        process = subprocess.Popen(command, env=env, stdin=subprocess.PIPE, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        processes.append((name, process))
        return process
    def action(label, duration, **state):
        result['actions'].append(dict(client=label, time_ms=time.time()*1000, duration=duration, state=state))
        pads[label].hold(duration, **state)
        time.sleep(.3)
    def stop_requested(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop_requested)
    try:
        data = run / 'server-data'; data.mkdir()
        spawn('server', [str(args.server.resolve()), '--config', str(root / 'ServerConfig.ini'), '--non-interactive'],
              dict(os.environ, MILKBAR_DATA_DIR=str(data)))
        time.sleep(1); alive()
        for label in ('a', 'b'):
            client = root / label
            # Keep an immutable baseline so repeated runs do not accumulate autosaves.
            save = client / 'cemu/mlc01/usr/save'
            baseline = root / 'test-baseline' / label / 'save'
            if not baseline.exists():
                baseline.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(save, baseline)
            shutil.rmtree(save)
            shutil.copytree(baseline, save)
            profile = client / 'cemu/config/controllerProfiles/controller0.xml'
            profiles.append((profile, profile.read_bytes() if profile.exists() else None))
            pads[label] = Gamepad()
            write_profile(profile, pads[label].port)
            spawn(label, [sys.executable, '-u', str(ROOT / 'CrossPlatform/milkbar_launcher.py'), 'launch'],
                  dict(os.environ, MILKBAR_DATA_DIR=str(client), HYRULE_TEST_TELEMETRY=str(run / f'{label}.jsonl')))
        print(f'Artifacts: {run}', flush=True)
        for label in ('a', 'b'):
            wait_until(pads[label].connected.is_set, 30, f'{label} DSU subscription', alive)
        check('independent_virtual_gamepads', passed=True, ports=[pads[x].port for x in ('a', 'b')])
        # Input can load the save while optional native signature scans run.
        # Both clients start at Continue; repeated A confirms the latest copied save.
        deadline = time.monotonic() + args.boot_timeout
        loaded = set()
        while len(loaded) < 2 and time.monotonic() < deadline:
            alive()
            for label in ('a', 'b'):
                if label in loaded:
                    continue
                if live(run / f'{label}.jsonl'):
                    loaded.add(label)
                    check(f'{label}_save_loaded', passed=True)
                else:
                    action(label,.2,buttons=['a'])
            time.sleep(2)
        if len(loaded) != 2:
            raise RuntimeError('Saved-game loading did not finish before the deadline')
        for label in ('a', 'b'):
            wait_until(lambda: records(run / f'{label}.jsonl', 'applied', time.time()*1000 - 2000), 60, f'{label} remote actor active', alive)
        check('remote_actors_active', passed=True)
        for label, other in (('a', 'b'), ('b', 'a')):
            start = time.time() * 1000
            action(label,3,ly=1)
            end = time.time() * 1000
            time.sleep(3); alive()
            check(f'{label}_to_{other}_movement', **movement_result(records(run/f'{label}.jsonl','local'),
                  records(run/f'{other}.jsonl','received'), records(run/f'{other}.jsonl','applied'), start,end))
            start = time.time() * 1000
            action(label,.3,buttons=['x']); time.sleep(3)
            local = records(run/f'{label}.jsonl','local',start)
            remote = records(run/f'{other}.jsonl','received',start)
            animations = {x['animation'] for x in local}
            received = {x['animation'] for x in remote}
            check(f'{label}_to_{other}_jump_animation', passed=len(animations)>1 and animations.issubset(received),
                  sent_animations=sorted(animations), received_animations=sorted(received))
            start = time.time()*1000
            action(label,.3,buttons=['y']); time.sleep(2)
            local = records(run/f'{label}.jsonl','local',start)
            remote = records(run/f'{other}.jsonl','received',start)
            held = {tuple(r['equipment']) for r in local if r['equipment_state'] != 0}
            matched = {tuple(r['equipment']) for r in remote if r['equipment_state'] != 0}
            check(f'{label}_to_{other}_draw_weapon', passed=bool(held) and held.issubset(matched),
                  local_equipment=sorted(held), received_equipment=sorted(matched))
            action(label,.3,buttons=['b']); time.sleep(2)
            start = time.time()*1000
            action(label,.3,buttons=['zr']); time.sleep(.5)
            action(label,2,buttons=['zr']); time.sleep(4)
            local = records(run/f'{label}.jsonl','local',start)
            remote = records(run/f'{other}.jsonl','received',start)
            fired = {r['arrow_id'] for r in local if r['arrow_active']}
            received = {r['arrow_id'] for r in remote if r['arrow_active']}
            check(f'{label}_to_{other}_arrow_packets', passed=bool(fired) and fired.issubset(received),
                  local_arrow_ids=sorted(fired), received_arrow_ids=sorted(received),
                  reason='Local shot was not observed' if not fired else ('Remote shot was not observed' if not fired.issubset(received) else 'Matched'))
            action(label,.3,buttons=['b']); time.sleep(1)
        # Intentional client loss must stop its updates without taking down A or the server.
        victim = next(p for name,p in processes if name == 'b')
        os.killpg(victim.pid,signal.SIGTERM); victim.wait(timeout=10)
        processes[:] = [(name,p) for name,p in processes if name != 'b']
        time.sleep(5); alive()
        cutoff=time.time()*1000-2000
        check('disconnect_stops_remote_updates', passed=bool(records(run/'a.jsonl','local',cutoff)) and
              not records(run/'a.jsonl','received',cutoff) and not records(run/'a.jsonl','applied',cutoff))
        result['passed'] = all(c['passed'] for c in result['checks'])
    except (Exception, KeyboardInterrupt) as error:
        result['error'] = str(error) or 'Interrupted'
        print(f'Test stopped: {result["error"]}', flush=True)
    finally:
        for pad in pads.values(): pad.set()
        for _, process in reversed(processes):
            try: os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError: pass
        for _, process in processes:
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL); process.wait()
        for profile, original in profiles:
            if original is None: profile.unlink(missing_ok=True)
            else: profile.write_bytes(original)
        for pad in pads.values(): pad.close()
        for log in logs: log.close()
        for label in ('a','b'):
            for source, name in [('LatestLog.txt','native'),('cemu/config/log.txt','cemu')]:
                path=root/label/source
                if path.exists():shutil.copy2(path,run/f'{label}-{name}.log')
        (run/'report.json').write_text(json.dumps(result,indent=2)+'\n')
        print(f'Report: {run / "report.json"}',flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
