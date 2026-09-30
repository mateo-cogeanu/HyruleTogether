import importlib.util
from pathlib import Path
import socket
import struct
import sys
import time
import tempfile
import xml.etree.ElementTree as ET
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from testing.dsu import Gamepad, packet, pad_packet, valid_request, write_profile
spec = importlib.util.spec_from_file_location('gameplay', Path(__file__).resolve().parents[2] / 'scripts/test-local-gameplay.py')
gameplay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gameplay)


def request(kind, body=b''):
    message = bytearray(packet(kind, body)); message[:4] = b'DSUC'; message[8:12] = bytes(4)
    struct.pack_into('<I', message, 8, zlib.crc32(message))
    return message


class AutomationTests(unittest.TestCase):
    def test_fixture_requires_the_same_entity_and_a_readback_after_the_write(self):
        source = [dict(time_ms=100, before=0, id='V1365')]
        peer = dict(time_ms=101, id='V1365', value=1)
        self.assertEqual(peer, gameplay.fixture_readback(source, [peer], 'quest'))
        for invalid in (dict(peer, time_ms=99), dict(peer, id='V0'), dict(peer, value=0)):
            self.assertIsNone(gameplay.fixture_readback(source, [invalid], 'quest'))
        self.assertIsNone(gameplay.fixture_readback([dict(source[0], before=1)], [peer], 'quest'))
        source = [dict(time_ms=100, before=13, after=7, slot=42)]
        peer = dict(time_ms=101, slot=42, health=7)
        self.assertEqual(peer, gameplay.fixture_readback(source, [peer], 'enemy'))
        for invalid in (dict(peer, time_ms=99), dict(peer, slot=41), dict(peer, health=13)):
            self.assertIsNone(gameplay.fixture_readback(source, [invalid], 'enemy'))
        self.assertIsNone(gameplay.fixture_readback([], [peer], 'enemy'))

    def test_neutral_warmup_allows_title_pause_but_requires_fresh_samples(self):
        now = 10000
        rows = [dict(time_ms=t, paused=True) for t in range(7100, 10001, 100)]
        self.assertFalse(gameplay.readiness_result(rows, now))
        self.assertTrue(gameplay.readiness_result(rows, now, require_unpaused=False))
        self.assertFalse(gameplay.readiness_result(rows, now + 1000, require_unpaused=False))
        self.assertFalse(gameplay.readiness_result([], now, require_unpaused=False))

    def test_primary_gamepad_profile_uses_vpad_button_and_axis_ids(self):
        # IDs follow Cemu VPADController.h; Pro Controller has Home at ID 11,
        # shifting its directional and stick mappings by one.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'controller0.xml'
            write_profile(path, 34567)
            root = ET.parse(path).getroot()
        self.assertEqual('Wii U GamePad', root.findtext('type'))
        controller = root.find('controller')
        self.assertEqual('DSUController', controller.findtext('api'))
        self.assertEqual('127.0.0.1', controller.findtext('ip'))
        self.assertEqual('34567', controller.findtext('port'))
        mapping = {int(e.findtext('mapping')): int(e.findtext('button'))
                   for e in controller.findall('mappings/entry')}
        self.assertEqual(13, mapping[1])  # A
        self.assertEqual(4, mapping[11])  # D-pad up, not Home
        self.assertEqual(1, mapping[15])  # Left stick click
        self.assertEqual((39, 45, 44, 38), tuple(mapping[i] for i in range(17, 21)))
        self.assertEqual((41, 47, 46, 40), tuple(mapping[i] for i in range(21, 25)))
        self.assertNotIn(25, mapping)  # VPAD ID 25 is microphone, not a stick axis.

    def test_actor_lifecycle_rejects_ignored_duplicates_and_stale_adoption(self):
        def event(kind, at, actor):
            return dict(kind=kind, time_ms=at, actor=actor, slot=1)
        rows = [event('actor_create', 100, 1), event('actor_erase', 200, 1),
                event('actor_create', 300, 2)]
        applied = [event('applied', at, 2) for at in range(400, 2001, 100)]
        def passed(events, samples=applied):
            return gameplay.actor_lifecycle_result(events + samples, 400, 2000)['passed']
        self.assertTrue(passed(rows))
        # Repeated notification of the same address is not another actor.
        self.assertTrue(passed(rows + [event('actor_create', 600, 2)]))
        self.assertFalse(passed(rows + [event('actor_create', 600, 3)]))
        self.assertFalse(passed(rows + [event('actor_create', 310, 3), event('actor_erase', 350, 3)]))
        # Even a duplicate erased later must fail while both were present.
        self.assertFalse(passed(rows + [event('actor_create', 600, 3), event('actor_erase', 800, 3)]))
        self.assertFalse(passed([r for r in rows if r['kind'] != 'actor_erase']))
        self.assertFalse(passed(rows + [event('actor_erase', 600, 2)]))
        self.assertFalse(passed(rows, [event('applied', r['time_ms'], 1) for r in applied]))
        self.assertFalse(passed([], applied))
        self.assertFalse(passed(rows, []))
        self.assertFalse(gameplay.actor_lifecycle_result(rows + applied, 400, 4000)['passed'])
        # The write before an erase in the same millisecond belongs to the old actor.
        ordered = rows + [event('applied', 400, 2), event('actor_erase', 400, 2),
                          event('actor_create', 400, 4)]
        ordered += [event('applied', at, 4) for at in range(500, 2001, 100)]
        self.assertTrue(gameplay.actor_lifecycle_result(ordered, 400, 2000)['passed'])

    def test_wire_crc_buttons_axes_and_size(self):
        data = pad_packet(7, buttons=['a','zr'], lx=-1, ly=1)
        self.assertEqual(100, len(data))
        self.assertEqual(b'DSUS', data[:4])
        self.assertEqual(7, struct.unpack_from('<I', data, 32)[0])
        self.assertEqual((0, 34), tuple(data[36:38]))
        self.assertEqual((0,255,128,128), tuple(data[40:44]))
        checksum = struct.unpack_from('<I',data,8)[0]
        check=bytearray(data);check[8:12]=bytes(4)
        self.assertEqual(checksum,zlib.crc32(check))
        self.assertTrue(valid_request(request(0x100002,bytes(8))))
        bad=request(0x100002,bytes(8));bad[-1]=1
        self.assertFalse(valid_request(bad))

    def test_udp_subscription_is_independent_and_release_is_transmitted(self):
        a,b=Gamepad(),Gamepad()
        sockets=[]
        try:
            for pad in (a,b):
                client=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);client.settimeout(2)
                sockets.append(client);client.sendto(request(0x100002,bytes(8)),('127.0.0.1',pad.port))
                self.assertTrue(pad.connected.wait(2))
            a.set(buttons=['a'])
            def state(client,wanted):
                deadline=time.monotonic()+2
                while time.monotonic()<deadline:
                    data,_=client.recvfrom(2048)
                    if data[37]==wanted:return
                self.fail('Expected controller state never arrived')
            state(sockets[0],32);state(sockets[1],0)
            a.set();state(sockets[0],0)
        finally:
            a.close();b.close()
            for client in sockets:client.close()

    def test_arrows_require_two_distinct_airborne_generations_and_peer_receipt(self):
        def shot(identifier, position):
            return dict(arrow_id=identifier, arrow_type=0, arrow_active=True, arrow_position=position, position=[0.,0.,0.])
        nocked = [shot(1,[0.,1.,0.]),shot(2,[0.,1.,0.])]
        self.assertFalse(gameplay.arrow_result(nocked,nocked)['passed'])
        flying = [shot(1,[10.,1.,0.]),shot(2,[10.,1.,0.])]
        self.assertTrue(gameplay.arrow_result(flying,flying)['passed'])
        self.assertFalse(gameplay.arrow_result(flying[:1],flying)['passed'])
        self.assertFalse(gameplay.arrow_result(flying,flying[:1])['passed'])
        self.assertFalse(gameplay.arrow_result(flying,[dict(r,arrow_type=4) for r in flying])['passed'])
        self.assertFalse(gameplay.arrow_result([shot(1,[0.,0.,0.]),flying[1]],flying)['passed'])
        self.assertFalse(gameplay.arrow_result([shot(1,[None,1.,0.]),flying[1]],flying)['passed'])

    def test_readiness_requires_fresh_continuous_unpaused_samples(self):
        rows = [dict(time_ms=i*100, paused=False) for i in range(31)]
        self.assertTrue(gameplay.readiness_result(rows, 3000))
        self.assertFalse(gameplay.readiness_result([], 3000))
        self.assertFalse(gameplay.readiness_result(rows[-10:], 3000))
        self.assertFalse(gameplay.readiness_result(rows, 6000))
        self.assertFalse(gameplay.readiness_result([dict(r, paused=r['time_ms']==1500) for r in rows], 3000))
        self.assertFalse(gameplay.readiness_result(rows[:10]+rows[20:], 3000))

    def test_checker_rejects_no_movement_missing_application_and_delayed_packets(self):
        rows=[dict(time_ms=i*100,position=[i*.2,20.,30.]) for i in range(30)]
        self.assertTrue(gameplay.movement_result(rows,rows,rows,0,2900)['passed'])
        stationary=[dict(r,position=[1.,20.,30.]) for r in rows]
        self.assertFalse(gameplay.movement_result(stationary,stationary,stationary,0,2900)['passed'])
        falling=[dict(r,position=[1.,float(i),30.]) for i,r in enumerate(rows)]
        self.assertFalse(gameplay.movement_result(falling,falling,falling,0,2900)['passed'])
        self.assertFalse(gameplay.movement_result(rows,rows,[],0,2900)['passed'])
        stale=[dict(r,time_ms=r['time_ms']+5000) for r in rows]
        self.assertFalse(gameplay.movement_result(rows,stale,rows,0,2900)['passed'])
        invalid=[dict(r,position=[None,20,30]) for r in rows]
        self.assertFalse(gameplay.movement_result(invalid,invalid,invalid,0,2900)['passed'])


if __name__=='__main__':unittest.main()
