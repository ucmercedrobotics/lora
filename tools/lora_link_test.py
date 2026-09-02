#!/usr/bin/env python3
"""Send or receive one frame on the modem's host port, to prove two boards are
actually talking over the air (not just alive individually).

Implements the same wire format as src/frame.h and src/cobs.h:
    COBS(length || body || crc8(body)) || 0x00
    body: [rssi:int16le][snr:int8][payload]   modem -> host
          [payload]                           host  -> modem
CRC-8/ATM: poly 0x07, init 0x00, no reflection, no final XOR.

Usage:
    python3 lora_link_test.py send /dev/cu.usbmodemXXXX "hello"
    python3 lora_link_test.py recv /dev/cu.usbmodemYYYY

Run recv on the far board first, then send on the near board. A decoded
frame on the recv side, with a real rssi/snr, is proof the packet made the
round trip over RF -- those numbers come off the radio, not the USB link.
"""
import sys
import time

import serial

BAUD = 115200


def crc8(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    out = bytearray()
    code_index = 0
    out.append(0)  # placeholder for first code byte
    code = 1
    for byte in data:
        if byte == 0x00:
            out[code_index] = code
            code_index = len(out)
            out.append(0)
            code = 1
        else:
            out.append(byte)
            code += 1
            if code == 0xFF:
                out[code_index] = code
                code_index = len(out)
                out.append(0)
                code = 1
    out[code_index] = code
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        code = data[i]
        if code == 0x00:
            raise ValueError("zero code byte inside frame")
        i += 1
        block_end = i + code - 1
        if block_end > n:
            raise ValueError("code byte overruns frame")
        out.extend(data[i:block_end])
        i = block_end
        if code != 0xFF and i < n:
            out.append(0x00)
    return bytes(out)


def build_frame(payload: bytes) -> bytes:
    if len(payload) > 252:
        raise ValueError("payload exceeds 252 bytes")
    body = payload
    logical = bytes([len(body)]) + body + bytes([crc8(body)])
    return cobs_encode(logical) + b"\x00"


def send(port: str, payload: bytes):
    with serial.Serial(port, BAUD, timeout=1) as ser:
        time.sleep(2)  # let USB CDC settle after open
        frame = build_frame(payload)
        ser.write(frame)
        ser.flush()
        print(f"sent {len(payload)}-byte payload: {payload!r}")
        print(f"wire bytes: {frame.hex()}")


def recv(port: str):
    with serial.Serial(port, BAUD, timeout=1) as ser:
        print(f"listening on {port} at {BAUD} baud, Ctrl+C to stop")
        buf = bytearray()
        while True:
            chunk = ser.read(256)
            for byte in chunk:
                if byte == 0x00:
                    if buf:
                        handle_frame(bytes(buf))
                        buf.clear()
                else:
                    buf.append(byte)


def handle_frame(raw: bytes):
    try:
        logical = cobs_decode(raw)
    except ValueError as e:
        print(f"[dropped] cobs error: {e}")
        return
    if len(logical) < 2:
        print("[dropped] frame too short")
        return
    length = logical[0]
    body = logical[1:-1]
    crc_rx = logical[-1]
    if len(body) != length:
        print(f"[dropped] length mismatch: header says {length}, got {len(body)}")
        return
    if crc8(body) != crc_rx:
        print(f"[dropped] crc mismatch: expected {crc8(body):#04x}, got {crc_rx:#04x}")
        return
    if len(body) < 3:
        print(f"[dropped] body too short for rssi/snr header: {body!r}")
        return
    rssi = int.from_bytes(body[0:2], "little", signed=True)
    snr = int.from_bytes(body[2:3], "little", signed=True) / 4.0
    payload = body[3:]
    print(f"RECEIVED payload={payload!r} rssi={rssi} dBm snr={snr:.2f} dB")


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("send", "recv"):
        print(__doc__)
        sys.exit(1)
    mode, port = sys.argv[1], sys.argv[2]
    if mode == "send":
        payload = (sys.argv[3] if len(sys.argv) > 3 else "ping").encode()
        send(port, payload)
    else:
        try:
            recv(port)
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
