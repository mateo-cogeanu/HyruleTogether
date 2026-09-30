import importlib.util
from pathlib import Path
import socket
import struct
import sys
import time
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from testing.dsu import Gamepad, packet, pad_packet, valid_request
spec = importlib.util.spec_from_file_location('gameplay', Path(__file__).resolve().parents[2] / 'scripts/test-local-gameplay.py')
gameplay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gameplay)


def request(kind, body=b''):
    message = bytearray(packet(kind, body)); message[:4] = b'DSUC'; message[8:12] = bytes(4)
    struct.pack_into('<I', message, 8, zlib.crc32(message))
    return message


class AutomationTests(unittest.TestCase):
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
