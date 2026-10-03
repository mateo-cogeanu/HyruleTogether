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


def readiness_result(rows, now, require_unpaused=True):
    rows = [row for row in rows if now - 3000 <= row['time_ms'] <= now]
    return (len(rows) >= 20 and rows[-1]['time_ms'] >= now - 500
            and rows[-1]['time_ms'] - rows[0]['time_ms'] >= 2500
            and (not require_unpaused or all(not row['paused'] for row in rows))
            and all(b['time_ms'] - a['time_ms'] <= 300 for a, b in zip(rows, rows[1:])))


def fixture_readback(source, peer, kind):
    """Require a changed value on the same entity after the source write."""
    if not source:
        return None
    change = source[-1]
    for row in peer:
        if row['time_ms'] < change['time_ms']:
            continue
        if kind == 'quest' and change['before'] == 0 and row['id'] == change['id'] and row['value'] & 1:
            return row
        if kind == 'enemy' and change['before'] > change['after'] >= 0 and row['slot'] == change['slot'] and row['health'] == change['after']:
            return row
    return None


def combined_damage_readback(sources, clients, now):
    """Both independent hits must leave fresh, stable health on the same enemy."""
    if len(sources) != 2 or len(clients) != 2 or not all(sources): return False
    changes = [rows[-1] for rows in sources]
    if any(row['slot'] != -988114952 or row['before'] - row['after'] != damage
           for row, damage in zip(changes, (6, 3))): return False
    after = max(row['time_ms'] for row in changes)
    for rows in clients:
        recent = [row for row in rows if row['slot'] == -988114952
                  and max(after, now - 2000) <= row['time_ms'] <= now]
        if (len(recent) < 3 or recent[-1]['time_ms'] < now - 500
                or recent[-1]['time_ms'] - recent[0]['time_ms'] < 1000
                or any(row['health'] != 4 for row in recent)): return False
    return True


def replicated_drop_names(source, peer, live=False):
    """Match stable identities, and for live samples require nearby actor poses."""
    return {row['name'] for row in source if any(
        other['id'] == row['id'] and other['name'] == row['name'] and
        (distance(row['position'], other['position']) < 2 if live else
         other['time_ms'] >= row['time_ms']) for other in peer)}


def item_motion_result(fixtures, source, peer, applied, now):
    """Require actual actor movement and fresh sustained peer poses, not write telemetry."""
    for fixture in fixtures:
        identity = fixture['id']
        recent_source = [r for r in source if r['id'] == identity and now-2000 <= r['time_ms'] <= now]
        recent_peer = [r for r in peer if r['id'] == identity and now-2000 <= r['time_ms'] <= now]
        if not recent_source or len(recent_peer) < 3: continue
        target = recent_source[-1]
        if (target.get('revision', 0) < 1 or distance(target['position'], fixture['before']) < 1 or
            target['time_ms'] < now-500 or recent_peer[-1]['time_ms'] < now-500 or
            recent_peer[-1]['time_ms']-recent_peer[0]['time_ms'] < 1000): continue
        if len({r['actor'] for r in recent_peer}) != 1: continue
        if any(r.get('revision', 0) != target['revision'] or
               distance(r['position'], target['position']) > .5 for r in recent_peer): continue
        if not any(r['id'] == identity and r['revision'] == target['revision'] and
                   r['time_ms'] >= fixture['time_ms'] for r in applied): continue
        return dict(passed=True, id=identity, revision=target['revision'],
                    displacement=distance(target['position'], fixture['before']),
                    scope='Synthetic displacement of an actual dropped body; persistent actor positions on both clients.')
    return dict(passed=False, scope='Missing movement, application, or sustained fresh peer actor readback.')


def pickup_cleanup_ids(local_erased, peer_erased):
    return {row['id'] for row in local_erased if row['removed'] and any(
        other['id'] == row['id'] and other['removed'] and
        other['time_ms'] >= row['time_ms'] for other in peer_erased)}


def inventory_drop_result(rows, start, end):
    found = {}
    wood_creations = set()
    for row in rows:
        if not start <= row['time_ms'] <= end or row['name'] not in ('Weapon_Spear_030', 'Obj_FireWoodBundle'):
            continue
        for pack in row['packs']:
            params = {entry['key']: entry for entry in pack['params']}
            if '@PC' in params or '@ND' in params or '@D' in params or 'IsPlayerPut' not in params:
                continue
            if row['name'] == 'Weapon_Spear_030':
                transform = params.get('@M', {})
                if transform.get('type') != 7 or len(transform.get('bytes', '')) != 96:
                    continue
                life = params.get('Life', {})
                if life.get('type') != 0 or len(life.get('bytes', '')) != 8 or int.from_bytes(bytes.fromhex(life['bytes']), 'big', signed=True) <= 0:
                    continue
            else:
                # Materials use the carry-box path, without an initial @M.
                # Wood is Obj_FireWoodBundle, not the Bird Egg material actor.
                instance = params.get('@I', {})
                if params['IsPlayerPut'].get('bytes') != '01' or instance.get('type') != 0 or len(instance.get('bytes', '')) != 8:
                    continue
                wood_creations.add(row['time_ms'])
            found[row['name']] = row
    return dict(passed=len(found) == 2 and len(wood_creations) >= 2, captured=found, wood_creations=len(wood_creations),
                scope='Local inventory-drop factory capture only; no peer replication or pickup assertion.')


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


def arrow_result(local, remote):
    fired = {r['arrow_id'] for r in local if r['arrow_active'] and r['arrow_id'] > 0
             and 10 < distance(r['arrow_position'], [0.,0.,0.]) < 1e6
             and 3 < distance(r['arrow_position'], r['position']) < 10000}
    local_types = {(r['arrow_id'], r['arrow_type']) for r in local if r['arrow_id'] in fired and r['arrow_active']}
    remote_types = {(r['arrow_id'], r['arrow_type']) for r in remote if r['arrow_active']}
    received = {identifier for identifier, _ in remote_types}
    return dict(passed=len(fired) >= 2 and local_types.issubset(remote_types),
                local_arrow_ids=sorted(fired), received_arrow_ids=sorted(received),
                reason='Fewer than two airborne arrows observed' if len(fired) < 2 else
                       ('Remote arrow ID/type was not observed' if not local_types.issubset(remote_types) else 'Matched'))


def actor_lifecycle_result(rows, start, end):
    """Count actual actor callbacks independently of the adopted player address."""
    active, maximum, overlap = set(), 0, False
    events = [r for r in rows if r['kind'] in ('actor_create', 'actor_erase') and r['time_ms'] <= end]
    applied = [r for r in rows if r['kind'] == 'applied' and start <= r['time_ms'] <= end]
    mismatches = 0
    # Preserve sink order for callbacks and writes sharing a millisecond.
    observed = [r for r in rows if
                (r['kind'] in ('actor_create', 'actor_erase') and r['time_ms'] <= end) or
                (r['kind'] == 'applied' and start <= r['time_ms'] <= end)]
    for row in sorted(observed, key=lambda r: r['time_ms']):
        key = (row['slot'], row['actor'])
        if row['kind'] == 'actor_create':
            active.add(key)
        elif row['kind'] == 'actor_erase':
            active.discard(key)
        elif active != {key}:
            mismatches += 1
        maximum = max(maximum, len(active))
        overlap |= len(active) > 1
    return dict(passed=bool(events) and len(applied) >= 10 and
                applied[-1]['time_ms'] >= end - 1000 and len(active) == 1 and
                not overlap and mismatches == 0,
                remaining_actors=len(active), maximum_actors=maximum,
                mismatched_applied_samples=mismatches, applied_samples=len(applied))


def equipment_attachment_result(received, snapshots, start, end):
    """Read both NPC attachment flags after a one-handed draw, not just packets."""
    held = [r for r in received if start <= r['time_ms'] <= end
            and r['equipment_state'] == 2 and r['equipment'][0] == 1
            and r['equipment'][2] != 0]
    if not held:
        return dict(passed=False, reason='No received one-handed draw')
    requested = held[0]['time_ms']
    samples = [r for r in snapshots if requested <= r['time_ms'] <= end]
    matches = []
    for row in samples:
        children = row.get('children', [])
        if (row.get('state') == 0 and len(children) >= 2
                and all(len(child) == 4 and all(isinstance(v, int) for v in child)
                        and child[0] != 0 and child[1] != 0xffffffff
                        and child[3] >> 24 == 0 for child in children[:2])):
            matches.append(row)
    # Require consecutive fresh samples for the same actor. One transient flag
    # match followed by a delayed shield attachment cannot pass.
    stable = next(((a, b) for a, b in zip(samples, samples[1:])
                   if a in matches and b in matches and a['actor'] == b['actor']
                   and 0 < b['time_ms'] - a['time_ms'] <= 300
                   and b['time_ms'] - requested <= 600), None)
    settled = [r for r in samples if stable and r['time_ms'] >= stable[0]['time_ms']]
    persistent = bool(stable and settled and settled[-1]['time_ms'] >= end - 500
                      and all(r in matches and r['actor'] == stable[0]['actor'] for r in settled)
                      and all(b['time_ms'] - a['time_ms'] <= 300
                              for a, b in zip(settled, settled[1:])))
    return dict(passed=persistent,
                attachment_ms=stable[1]['time_ms'] - requested if stable else None,
                scope='Verified v208 NPC sword/shield attachment flags; rendered bone timing still needs visual verification.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT / 'Build/local-multiplayer')
    parser.add_argument('--server', type=Path, default=ROOT / 'Build/server' / (('osx' if sys.platform == 'darwin' else 'linux') + ('-arm64' if platform.machine().lower() in ('arm64','aarch64') else '-x64')) / 'MBL.DedicatedServer')
    parser.add_argument('--boot-timeout', type=float, default=240)
    parser.add_argument('--bow-hold', type=float, default=2, help='Seconds to hold a drawn bow before release (increase for visual diagnosis).')
    parser.add_argument('--inspection-hold', type=float, default=0, help='Optional pause after movement for visual inspection, in seconds (0 to 60).')
    parser.add_argument('--scenario-window', type=float, default=0, help='Seconds to accept bounded DSU commands from the run input.jsonl file.')
    parser.add_argument('--enemy-fixture', action='store_true', help='Synthetic enemy damage replication in isolated saves.')
    parser.add_argument('--one-handed-fixture', action='store_true', help='Equip the baseline one-handed club and shield in both clients before testing.')
    parser.add_argument('--enemy-concurrent-fixture', action='store_true', help='Apply independent synthetic damage from both clients; requires --enemy-fixture.')
    parser.add_argument('--inventory-pickup-client', choices=['a', 'b'], default='a', help='Client that picks up the owner A drop.')
    parser.add_argument('--material-drop-fixture', action='store_true', help='Drop the baseline first material stack and require an Item_ actor on the peer; requires --inventory-drops.')
    parser.add_argument('--item-motion-fixture', action='store_true', help='Move a real dropped wood body in isolated client A and require persistent peer actor readback; requires --inventory-drops.')
    parser.add_argument('--inventory-pickups', action='store_true', help='Probe pickup of the scripted drops; requires --inventory-drops.')
    parser.add_argument('--inventory-drops', action='store_true', help='Script spear/wood drops and verify peer creation, spawn metadata, and persistent matching positions.')
    parser.add_argument('--inventory-step-hold', type=float, default=0, help='Optional 0–15 second inspection pause after each inventory button.')
    parser.add_argument('--quest-fixture-source', choices=['a', 'b'], default='a', help='Client that originates the synthetic quest flag.')
    parser.add_argument('--quest-fixture', action='store_true', help='Opt-in synthetic quest flag replication check in isolated saves.')
    parser.add_argument('--spawn-delay-ms', type=int, default=0, help='Delay the first remote-player spawn for the timeout regression fixture (0 to 30000).')
    args = parser.parse_args()
    if args.enemy_concurrent_fixture and not args.enemy_fixture:
        parser.error('--enemy-concurrent-fixture requires --enemy-fixture')
    if not math.isfinite(args.inventory_step_hold) or not 0 <= args.inventory_step_hold <= 15:
        parser.error('--inventory-step-hold must be between 0 and 15 seconds')
    if args.material_drop_fixture and not args.inventory_drops:
        parser.error('--material-drop-fixture requires --inventory-drops')
    if args.item_motion_fixture and (not args.inventory_drops or args.inventory_pickups):
        parser.error('--item-motion-fixture requires --inventory-drops without --inventory-pickups')
    if args.inventory_pickups and not args.inventory_drops:
        parser.error('--inventory-pickups requires --inventory-drops')
    if not 0 <= args.spawn_delay_ms <= 30000:
        parser.error('--spawn-delay-ms must be between 0 and 30000')
    if not math.isfinite(args.scenario_window) or not 0 <= args.scenario_window <= 180:
        parser.error('--scenario-window must be between 0 and 180 seconds')
    if not math.isfinite(args.inspection_hold) or not 0 <= args.inspection_hold <= 60:
        parser.error('--inspection-hold must be between 0 and 60 seconds')
    if not math.isfinite(args.bow_hold) or not 1 <= args.bow_hold <= 30:
        parser.error('--bow-hold must be between 1 and 30 seconds')
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
    result = dict(passed=False, checks=[], controller_type='Wii U GamePad', spawn_delay_ms=args.spawn_delay_ms,
                  quest_fixture=args.quest_fixture, quest_fixture_source=args.quest_fixture_source, enemy_fixture=args.enemy_fixture,
                  enemy_concurrent_fixture=args.enemy_concurrent_fixture, scenario_window=args.scenario_window,
                  inventory_drops=args.inventory_drops, item_motion_fixture=args.item_motion_fixture, material_drop_fixture=args.material_drop_fixture,
                  scope='Controller-driven game and native synchronization telemetry; synthetic fixtures test transport/application. Visual appearance, dialogue, rewards, and shared enemy AI require separate review.')
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
        result['checks'].append(dict(name=name, time_ms=time.time()*1000, **values))
        print(json.dumps(result['checks'][-1]), flush=True)
    def spawn(name, command, env):
        log = (run / f'{name}.log').open('w'); logs.append(log)
        process = subprocess.Popen(command, env=env, stdin=subprocess.PIPE, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        processes.append((name, process))
        return process
    def action(label, duration, **state):
        result['actions'].append(dict(client=label, time_ms=time.time()*1000, duration=duration, state=state))
        print(json.dumps(dict(action=result['actions'][-1])), flush=True)
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
            if args.inventory_drops:
                spawn_patch = client / 'cemu/config/graphicPacks/BreathOfTheWild_UKMM/patch_SpawnActors.asm'
                subprocess.run([sys.executable, str(ROOT/'scripts/patch-equipment-factory.py'),
                                str(spawn_patch)], check=True)
                shutil.copy2(spawn_patch, run/f'{label}-spawn.asm')
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
            shutil.copy2(profile, run/f'{label}-controller.xml')
            fixture_env = {}
            if args.inventory_drops: fixture_env['HYRULE_TEST_ITEM_PROBE'] = '1'
            if args.item_motion_fixture and label == 'a': fixture_env['HYRULE_TEST_ITEM_MOTION'] = '1'
            if args.enemy_fixture and (label == 'a' or args.enemy_concurrent_fixture):
                fixture_env['HYRULE_TEST_ENEMY_SOURCE'] = '1'
                if label == 'b': fixture_env['HYRULE_TEST_ENEMY_DAMAGE'] = '3'
            if args.quest_fixture:
                fixture_env['HYRULE_TEST_QUEST_NAME'] = 'HatenoMini_CameraBoy_Activated'
                fixture_env['HYRULE_TEST_QUEST_PREREQUISITE'] = 'AncientLabo_AncientAssistant001_SSLv2Get'
                if label == args.quest_fixture_source: fixture_env['HYRULE_TEST_QUEST_SOURCE'] = '1'
            spawn(label, [sys.executable, '-u', str(ROOT / 'CrossPlatform/milkbar_launcher.py'), 'launch'],
                  dict(os.environ, MILKBAR_DATA_DIR=str(client), HYRULE_TEST_TELEMETRY=str(run / f'{label}.jsonl'),
                       HYRULE_TEST_SPAWN_DELAY_MS=str(args.spawn_delay_ms), **fixture_env))
        print(f'Artifacts: {run}', flush=True)
        for label in ('a', 'b'):
            wait_until(pads[label].connected.is_set, 30, f'{label} DSU subscription', alive)
        check('independent_virtual_gamepads', passed=True, ports=[pads[x].port for x in ('a', 'b')])
        # Cemu calibrates on its first input read and masks baseline buttons.
        # Keep both pads neutral until native telemetry runs continuously; sending
        # A as soon as DSU subscribes can permanently calibrate A as held.
        wait_until(lambda: all(readiness_result(records(run/f'{label}.jsonl', 'readiness'),
                         time.time()*1000, require_unpaused=False) for label in ('a', 'b')),
                   args.boot_timeout, 'neutral controller warmup', alive)
        check('neutral_controller_warmup', passed=True)
        # Start title navigation only after the neutral warmup.
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
        # Actor creation and cached coordinates can precede the loading fade.
        def playable():
            now = time.time()*1000
            return all(readiness_result(records(run/f'{label}.jsonl', 'readiness'), now)
                       and live(run/f'{label}.jsonl') for label in ('a', 'b'))
        # Checking A then waiting for B can leave A's earlier readiness stale.
        wait_until(playable, 60, 'both clients simultaneously ready', alive)
        check('both_games_playable', passed=True)
        if args.inventory_drops:
            unsolicited = [row for label in ('a', 'b')
                for row in records(run/f'{label}.jsonl', 'item_published')
                if row['name'].startswith('Weapon_')]
            check('load_equipment_excluded_from_shared_drops', passed=not unsolicited,
                  unexpected_publications=unsolicited)
        if args.one_handed_fixture:
            for label in ('a', 'b'):
                def fixture_key(button):
                    action(label, .2, buttons=[button]); action(label, .8)
                for button in ('plus', 'l', 'l', 'r'): fixture_key(button)
                for _ in range(7):
                    action(label, .18, rx=-1); action(label, .4)
                for button in ('up', 'left'):
                    for _ in range(5): fixture_key(button)
                for _ in range(4): fixture_key('right')
                fixture_key('a')
                # Item context menus can remember Drop from an earlier action.
                # Clamp to their first entry (Equip) before confirming.
                for _ in range(3): fixture_key('up')
                for button in ('a', 'plus', 'b'): fixture_key(button)
                def one_handed():
                    rows = records(run/f'{label}.jsonl', 'local', time.time()*1000 - 2000)
                    return any(row['equipment'][0] == 1 and row['equipment'][2] != 0
                               and row['equipment_state'] == 0 for row in rows)
                wait_until(one_handed, 15, f'{label} one-handed weapon and shield equipped', alive)
            wait_until(playable, 60, 'both clients ready after equipment fixture', alive)
            check('one_handed_weapon_and_shield_fixture', passed=True,
                  scope='One-handed equipment prerequisites; visual attachment timing is a separate check.')
        if args.inventory_drops:
            start = time.time()*1000
            def inventory_button(button, settle=1.2):
                action('a', .2, buttons=[button])
                action('a', settle)  # Menus animate even while game logic is paused.
                if args.inventory_step_hold:
                    print(f'Inventory inspection: {button}', flush=True)
                    time.sleep(args.inventory_step_hold); alive()
            def open_inventory():
                inventory_button('plus')
                # Plus may reopen the Adventure Log after quest event updates.
                # The root tabs clamp at the leftmost Adventure Log: L,L,R
                # therefore selects Inventory regardless of the previous tab.
                for button in ('l', 'l', 'r'):
                    inventory_button(button, .5)
            open_inventory()
            for _ in range(7):
                action('a', .18, rx=-1)
                action('a', .4)
            for button in ('up', 'left'):
                for _ in range(5):
                    action('a', .1, buttons=[button])
            inventory_button('a')
            inventory_button('down')
            inventory_button('a')
            inventory_button('plus')
            open_inventory()
            for _ in range(5):
                action('a', .18, rx=1)
                action('a', .6)
            for button in ('x', 'a', 'b', 'a'):
                inventory_button(button)
            time.sleep(2); alive()
            check('local_inventory_drop_capture', **inventory_drop_result(
                records(run/'a.jsonl', 'item_factory_request'), start, time.time()*1000))
            def replicated_drops():
                source = records(run/'a.jsonl', 'item_published', start)
                peer = records(run/'b.jsonl', 'item_spawned', start)
                return replicated_drop_names(source, peer)
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline and not {
                'Weapon_Spear_030', 'Obj_FireWoodBundle'}.issubset(replicated_drops()):
                time.sleep(.5); alive()
            check('peer_inventory_drop_spawn', passed={
                'Weapon_Spear_030', 'Obj_FireWoodBundle'}.issubset(replicated_drops()),
                names=sorted(replicated_drops()), scope='Peer actor creation for the same published item IDs.')

            peer_requests = records(run/'b.jsonl', 'item_factory_request', start)
            def peer_metadata(name):
                for row in peer_requests:
                    if row['name'] != name: continue
                    for pack in row['packs']:
                        if pack['register'] == 7:
                            yield {p['key']: p for p in pack['params']}
            spear_original = inventory_drop_result(records(run/'a.jsonl', 'item_factory_request'), start, time.time()*1000)['captured'].get('Weapon_Spear_030')
            source_params = {p['key']: p for p in spear_original['packs'][0]['params']} if spear_original else {}
            weapon_metadata = bool(source_params) and any(all(params.get(key) == source_params.get(key) for key in (
                'Life', 'AddParam', 'AddSpecialFlag', 'IsWeaponCreateByRawLife'))
                for params in peer_metadata('Weapon_Spear_030'))
            material_metadata = any('@I' not in params and params.get('@M', {}).get('type') == 7
                for params in peer_metadata('Obj_FireWoodBundle'))
            check('peer_inventory_drop_metadata', passed=weapon_metadata and material_metadata,
                  weapon_metadata=weapon_metadata, material_metadata=material_metadata,
                  scope='Peer game factory receives matching weapon durability/modifiers and a world material transform.')
            time.sleep(5); alive()
            source_live = records(run/'a.jsonl', 'item_live', time.time()*1000 - 2000)
            peer_live = records(run/'b.jsonl', 'item_live', time.time()*1000 - 2000)
            nearby = replicated_drop_names(source_live, peer_live, live=True)
            check('peer_inventory_drop_persistence', passed={
                'Weapon_Spear_030', 'Obj_FireWoodBundle'}.issubset(nearby), names=sorted(nearby),
                scope='Both live actors persist with matching positions, five seconds after spawning.')
            if args.material_drop_fixture:
                material_start = time.time()*1000
                open_inventory()
                # D-pad left can cross inventory categories, rather than clamp
                # the grid. Normalize to Weapons and advance four tabs to Materials.
                for _ in range(7):
                    action('a', .18, rx=-1); action('a', .4)
                for _ in range(4):
                    action('a', .18, rx=1); action('a', .6)
                for _ in range(5): action('a', .1, buttons=['up'])
                for button in ('x', 'a', 'b', 'a'): inventory_button(button)
                time.sleep(7); alive()
                published = records(run/'a.jsonl', 'item_published', material_start)
                expected = {row['name'] for row in published if row['name'].startswith('Item_')}
                spawned = replicated_drop_names(published, records(run/'b.jsonl', 'item_spawned', material_start))
                live_names = replicated_drop_names(records(run/'a.jsonl', 'item_live', time.time()*1000-2000),
                    records(run/'b.jsonl', 'item_live', time.time()*1000-2000), live=True)
                check('inventory_material_peer_actor', passed=bool(expected) and expected.issubset(spawned & live_names),
                    names=sorted(expected), scope='Controller-dropped baseline material with matching peer actor identity and persistent live position.')
            if args.item_motion_fixture:
                def motion_readback():
                    return item_motion_result(records(run/'a.jsonl', 'item_motion_fixture'),
                        records(run/'a.jsonl', 'item_live'), records(run/'b.jsonl', 'item_live'),
                        records(run/'b.jsonl', 'item_pose_applied'), time.time()*1000)
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline and not motion_readback()['passed']:
                    time.sleep(.5); alive()
                check('shared_item_motion_readback', **motion_readback())
            if args.one_handed_fixture:
                published = [row for label in ('a', 'b') for row in records(run/f'{label}.jsonl', 'item_published')
                             if row['name'] == 'Weapon_Sword_004']
                check('equipped_weapon_excluded_from_shared_drops', passed=not published,
                      unexpected_publications=published)
            if args.inventory_pickups:
                pickup_start = time.time()*1000
                if args.inventory_pickup_client == 'b':
                    action('b', .2, buttons=['b']); action('b', 1)
                for _ in range(4 if args.inventory_pickup_client == 'b' else 2):
                    action(args.inventory_pickup_client, .2, buttons=['a']); action(args.inventory_pickup_client, 2)
                if args.inventory_pickup_client == 'b':
                    action('b', .2, buttons=['b']); action('b', 1)
                deadline = time.monotonic() + 8
                pickup_ids = set()
                while time.monotonic() < deadline:
                    local_erased = records(run/f'{args.inventory_pickup_client}.jsonl', 'item_erased', pickup_start)
                    peer_label = 'b' if args.inventory_pickup_client == 'a' else 'a'
                    peer_erased = records(run/f'{peer_label}.jsonl', 'item_erased', pickup_start)
                    pickup_ids = pickup_cleanup_ids(local_erased, peer_erased)
                    if pickup_ids: break
                    time.sleep(.5); alive()
                check('shared_inventory_pickup_cleanup', passed=bool(pickup_ids), ids=sorted(pickup_ids),
                      scope='Local pickup removes the same item actor on the peer after server confirmation.')
        if args.scenario_window:
            print(f'Scenario input ready: {run / "input.jsonl"}', flush=True)
            deadline = time.monotonic() + args.scenario_window
            processed = 0
            while time.monotonic() < deadline:
                alive()
                input_path = run/'input.jsonl'
                if input_path.exists():
                    lines = input_path.read_text().splitlines()
                    for line in lines[processed:]:
                        if time.monotonic() >= deadline:
                            break
                        command = json.loads(line)
                        label, duration = command['client'], float(command['duration'])
                        if label not in pads or not math.isfinite(duration) or not 0 <= duration <= 10:
                            raise ValueError('Invalid scenario command')
                        action(label, duration, **command['state'])
                        processed += 1
                time.sleep(.1)
        if args.quest_fixture:
            quest_source = run/f'{args.quest_fixture_source}.jsonl'
            quest_peer = run/('b.jsonl' if args.quest_fixture_source == 'a' else 'a.jsonl')
            def quest_replicated():
                return fixture_readback(records(quest_source, 'quest_fixture_source'),
                                        records(quest_peer, 'quest_fixture_readback'), 'quest')
            wait_until(quest_replicated, 45, 'synthetic quest flag readback on peer', alive)
            check('synthetic_quest_replication', passed=True,
                  source=records(quest_source, 'quest_fixture_source')[-1],
                  peer=quest_replicated())
            def prerequisite_replicated():
                return fixture_readback(records(quest_source, 'quest_prerequisite_source'),
                    records(quest_peer, 'quest_prerequisite_readback'), 'quest')
            wait_until(prerequisite_replicated, 45, 'quest prerequisite readback on peer', alive)
            check('synthetic_quest_prerequisite_replication', passed=True,
                  peer=prerequisite_replicated(), scope='Quest dependency flag readback; journal appearance remains a separate UI check.')
        if args.enemy_fixture:
            def enemy_replicated():
                if args.enemy_concurrent_fixture:
                    sources = [records(run/f'{label}.jsonl', 'enemy_fixture_source') for label in ('a', 'b')]
                    clients = [records(run/f'{label}.jsonl', 'enemy_live') for label in ('a', 'b')]
                    return combined_damage_readback(sources, clients, time.time()*1000)
                return fixture_readback(records(run/'a.jsonl', 'enemy_fixture_source'),
                                        records(run/'b.jsonl', 'enemy_live'), 'enemy')
            wait_until(enemy_replicated, 45, 'synthetic enemy damage readback on peer', alive)
            check('synthetic_enemy_damage_replication', passed=True,
                  source=records(run/'a.jsonl', 'enemy_fixture_source')[-1], peer=enemy_replicated(),
                  other_source=records(run/'b.jsonl', 'enemy_fixture_source')[-1] if args.enemy_concurrent_fixture else None)
        actor_check_start = time.time()*1000
        for label, other in (('a', 'b'), ('b', 'a')):
            wait_until(playable, 60, f'both clients ready before {label} movement', alive)
            start = time.time() * 1000
            action(label,2,ly=-1)
            end = time.time() * 1000
            time.sleep(3); alive()
            check(f'{label}_to_{other}_movement', **movement_result(records(run/f'{label}.jsonl','local'),
                  records(run/f'{other}.jsonl','received'), records(run/f'{other}.jsonl','applied'), start,end))
            if args.inspection_hold:
                print(f'Visual inspection: client {label} movement finished', flush=True)
                time.sleep(args.inspection_hold); alive()
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
            if args.one_handed_fixture:
                check(f'{label}_to_{other}_sword_shield_attachment_flags',
                      **equipment_attachment_result(remote,
                          records(run/f'{other}.jsonl', 'npc_equipment'), start, time.time()*1000))
            action(label,.3,buttons=['b']); time.sleep(2)
            action(label,.6,rx=1,ry=.35)
            start = time.time()*1000
            action(label,.3,buttons=['zr']); time.sleep(.5)
            for attempt in range(4):
                action(label,args.bow_hold,buttons=['zr']); time.sleep(4); alive()
                observed = records(run/f'{label}.jsonl','local',start)
                if len(arrow_result(observed, observed)['local_arrow_ids']) >= 2:
                    break
            local = records(run/f'{label}.jsonl','local',start)
            remote = records(run/f'{other}.jsonl','received',start)
            check(f'{label}_to_{other}_arrow_packets', **arrow_result(local, remote))
            observed = arrow_result(local, remote)['local_arrow_ids']
            bound = {r['arrow_id'] for r in records(run/f'{other}.jsonl', 'projectile_applied', start)}
            check(f'{label}_to_{other}_arrow_actor_updates', passed=len(observed)>=2 and set(observed).issubset(bound),
                  local_arrow_ids=observed, applied_arrow_ids=sorted(bound))
            action(label,.3,buttons=['b']); time.sleep(1)
        for label in ('a', 'b'):
            check(f'{label}_single_remote_actor', **actor_lifecycle_result(
                records(run/f'{label}.jsonl'), actor_check_start, time.time()*1000))
        if args.spawn_delay_ms:
            for label in ('a', 'b'):
                deferred = records(run/f'{label}.jsonl', 'spawn_deferred')
                created = records(run/f'{label}.jsonl', 'actor_create')
                elapsed = created[0]['time_ms'] - deferred[0]['time_ms'] if deferred and created else 0
                check(f'{label}_delayed_spawn_fixture', passed=len(deferred)==1 and elapsed>=args.spawn_delay_ms,
                      observed_delay_ms=elapsed, requested_delay_ms=args.spawn_delay_ms)
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
