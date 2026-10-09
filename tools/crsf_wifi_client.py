"""Offline protocol checks and explicitly requested bench demo for CRWF v1."""
import argparse
import secrets
import socket
import struct
import time

HEADER = struct.Struct('<4sBBHII')
RC, RELEASE, TELEMETRY = 1, 2, 3


def crc8(data):
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = ((value << 1) ^ (0xD5 if value & 0x80 else 0)) & 0xFF
    return value


def channels_frame(channels):
    if len(channels) != 16 or any(not 0 <= x <= 2047 for x in channels):
        raise ValueError('Exactly sixteen integer channels in 0..2047 required')
    packed = sum(value << (11 * index) for index, value in enumerate(channels))
    body = bytes([0x16]) + packed.to_bytes(22, 'little')
    return bytes([0xEA, 24]) + body + bytes([crc8(body)])


def encode(kind, session, sequence, frame=b''):
    if kind not in (RC, RELEASE, TELEMETRY) or not 1 <= session <= 0xFFFFFFFF:
        raise ValueError('Invalid kind or session')
    if not 0 <= sequence <= 0xFFFFFFFF:
        raise ValueError('Invalid sequence')
    packet = HEADER.pack(b'CRWF', 1, kind, 0, session, sequence) + frame
    decode(packet)
    return packet


def decode(packet):
    if len(packet) < HEADER.size:
        raise ValueError('Truncated header')
    magic, version, kind, reserved, session, sequence = HEADER.unpack_from(packet)
    if magic != b'CRWF' or version != 1 or reserved or not session:
        raise ValueError('Invalid header')
    frame = packet[HEADER.size:]
    if kind == RELEASE:
        if frame:
            raise ValueError('Release payload forbidden')
    elif kind in (RC, TELEMETRY):
        if len(frame) < 4 or len(frame) > 64 or frame[1] + 2 != len(frame):
            raise ValueError('Invalid CRSF length')
        if crc8(frame[2:-1]) != frame[-1]:
            raise ValueError('Invalid CRSF CRC')
        if kind == RC and (len(frame) != 26 or frame[0] not in (0xEA, 0xC8)
                           or frame[2] != 0x16):
            raise ValueError('Invalid RC frame')
    else:
        raise ValueError('Unknown kind')
    return kind, session, sequence, frame


def selftest():
    frame = channels_frame([992] * 16)
    assert len(frame) == 26
    packet = encode(RC, 123, 0xFFFFFFFF, frame)
    assert len(packet) == 42
    assert decode(packet) == (RC, 123, 0xFFFFFFFF, frame)
    assert decode(encode(RELEASE, 123, 0))[3] == b''
    assert decode(encode(TELEMETRY, 123, 1, frame))[3] == frame
    for invalid in (packet[:15], packet + b'\0', packet[:-1] + bytes([packet[-1] ^ 1]),
                    b'BAD!' + packet[4:], packet[:6] + b'\1' + packet[7:]):
        try:
            decode(invalid)
        except ValueError:
            continue
        raise AssertionError('Malformed packet accepted')
    print('CRWF offline checks passed; no network packets sent.')


def bench_demo(host, port, seconds, rate, values):
    session = secrets.randbelow(0xFFFFFFFF) + 1
    sequence = 0
    frame = channels_frame(values)
    destination = (host, port)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('', 0))
        sock.setblocking(False)
        deadline = time.monotonic() + seconds
        try:
            while time.monotonic() < deadline:
                sock.sendto(encode(RC, session, sequence, frame), destination)
                sequence = (sequence + 1) & 0xFFFFFFFF
                while True:
                    try:
                        packet, peer = sock.recvfrom(256)
                    except BlockingIOError:
                        break
                    if peer != destination:
                        continue
                    kind, remote_session, _, telemetry = decode(packet)
                    if kind == TELEMETRY and remote_session == session:
                        print('Telemetry:', telemetry.hex())
                time.sleep(1 / rate)
        finally:
            sock.sendto(encode(RELEASE, session, sequence), destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--send', metavar='IP', help='Explicitly send bench input')
    parser.add_argument('--port', type=int, default=8888)
    parser.add_argument('--seconds', type=float, default=2)
    parser.add_argument('--rate', type=int, default=50)
    parser.add_argument('--channels', default=','.join(['992'] * 16),
                        help='Sixteen raw CRSF values; defaults to all neutral')
    args = parser.parse_args()
    if not args.send:
        selftest()
        return
    if not 1 <= args.port <= 65535 or not 1 <= args.rate <= 100 or not 0 < args.seconds <= 30:
        parser.error('Invalid port, rate (1..100), or duration (0..30 seconds)')
    try:
        socket.inet_pton(socket.AF_INET, args.send)
        values = [int(value) for value in args.channels.split(',')]
        channels_frame(values)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    bench_demo(args.send, args.port, args.seconds, args.rate, values)


if __name__ == '__main__':
    main()
