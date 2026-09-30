"""Loopback DSU gamepad for automated Cemu integration tests.

Wire layout follows Cemu's src/input/api/DSU/DSUMessages.h.
No OS input injection or physical controller is required.
"""
import socket
import struct
import threading
import time
import zlib
import xml.etree.ElementTree as ET

BUTTONS = dict(minus=0, lclick=1, rclick=2, plus=3, up=4, right=5,
               down=6, left=7, zl=8, zr=9, l=10, r=11, x=12, a=13, b=14, y=15)


def packet(kind, body=b''):
    data = bytearray(struct.pack('<4sHHIII', b'DSUS', 1001, 4 + len(body), 0, 0x48595255, kind) + body)
    struct.pack_into('<I', data, 8, zlib.crc32(data))
    return bytes(data)


def valid_request(data):
    if len(data) < 20 or data[:4] != b'DSUC':
        return False
    version, length, checksum = struct.unpack_from('<HHI', data, 4)
    check = bytearray(data)
    check[8:12] = bytes(4)
    return version <= 1001 and length == len(data) - 16 and zlib.crc32(check) == checksum


def port_info(index=0):
    return struct.pack('<BBBB6sBB', index, 2 if index == 0 else 0, 2, 1,
                       b'\x02HYRUL', 5, int(index == 0))


def pad_packet(sequence, buttons=(), lx=0, ly=0, rx=0, ry=0):
    bits = sum(1 << BUTTONS[b] for b in set(buttons))
    axis = lambda x: round((max(-1, min(1, x)) + 1) * 127.5)
    body = bytearray(68)
    struct.pack_into('<IBBBB4B', body, 0, sequence, bits & 255, bits >> 8, 0, 0,
                     axis(lx), axis(ly), axis(rx), axis(ry))
    for i, name in enumerate(('left', 'down', 'right', 'up', 'y', 'b', 'a', 'x', 'r', 'l', 'zr', 'zl')):
        body[12 + i] = 255 if name in buttons else 0
    struct.pack_into('<Q6f', body, 36, time.monotonic_ns() // 1000, 0, 0, 1, 0, 0, 0)
    return packet(0x100002, port_info() + body)


class Gamepad:
    def __init__(self, port=0):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', port))
        self.socket.setblocking(False)
        self.port = self.socket.getsockname()[1]
        self.peers = {}
        self.lock = threading.Lock()
        self.state = {}
        self.stop = threading.Event()
        self.connected = threading.Event()
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def set(self, **state):
        # Validate immediately instead of failing silently in the sender thread.
        pad_packet(0, **state)
        with self.lock:
            self.state = state

    def hold(self, seconds, **state):
        self.set(**state)
        try:
            time.sleep(seconds)
        finally:
            self.set()

    def press(self, button, seconds=0.2):
        self.hold(seconds, buttons=[button])
        time.sleep(0.3)

    def _serve(self):
        sequence = 0
        while not self.stop.is_set():
            while True:
                try:
                    data, peer = self.socket.recvfrom(2048)
                except BlockingIOError:
                    break
                if not valid_request(data):
                    continue
                kind, = struct.unpack_from('<I', data, 16)
                if kind == 0x100000:
                    self.socket.sendto(packet(kind, struct.pack('<H2x', 1001)), peer)
                elif kind == 0x100001 and len(data) >= 24:
                    count, = struct.unpack_from('<I', data, 20)
                    for index in data[24:24 + min(count, 4)]:
                        self.socket.sendto(packet(kind, port_info(index)), peer)
                elif kind == 0x100002 and len(data) >= 28:
                    flags, index = data[20:22]
                    if not flags or (flags & 1 and index == 0) or (flags & 2 and data[22:28] == b'\x02HYRUL'):
                        self.peers[peer] = time.monotonic()
                        self.connected.set()
            with self.lock:
                output = pad_packet(sequence, **self.state)
            for peer, seen in list(self.peers.items()):
                if time.monotonic() - seen > 10:
                    del self.peers[peer]
                else:
                    self.socket.sendto(output, peer)
            sequence = (sequence + 1) & 0xffffffff
            self.stop.wait(1 / 120)

    def close(self):
        self.set()
        time.sleep(0.03)
        self.stop.set()
        self.thread.join(timeout=2)
        self.socket.close()


def write_profile(path, port):
    root = ET.Element('emulated_controller')
    ET.SubElement(root, 'type').text = 'Wii U Pro Controller'
    ET.SubElement(root, 'profile').text = 'Hyrule automated test'
    controller = ET.SubElement(root, 'controller')
    for key, value in dict(api='DSUController', uuid='0', display_name='Hyrule Test Gamepad', ip='127.0.0.1', port=str(port), motion='false').items():
        ET.SubElement(controller, key).text = value
    for group in ('axis', 'rotation', 'trigger'):
        node = ET.SubElement(controller, group)
        ET.SubElement(node, 'deadzone').text = '0.15'
        ET.SubElement(node, 'range').text = '1'
    mappings = ET.SubElement(controller, 'mappings')
    # Cemu Controller.h: button 0..31, axes begin at 38 (positive), 44 (negative).
    mapping = {1:13, 2:14, 3:12, 4:15, 5:10, 6:11, 7:8, 8:9, 9:3, 10:0,
               12:4, 13:6, 14:7, 15:5, 16:1, 17:2,
               18:39, 19:45, 20:44, 21:38, 22:41, 23:47, 24:46, 25:40}
    for key, value in mapping.items():
        node = ET.SubElement(mappings, 'entry')
        ET.SubElement(node, 'mapping').text = str(key)
        ET.SubElement(node, 'button').text = str(value)
    ET.ElementTree(root).write(path, encoding='utf-8', xml_declaration=True)
