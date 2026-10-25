"""Reduce an Android btsnoop capture to the Tribit control channel, safe to share.

Keeps only ACL packets of RFCOMM channels that carry Tribit frames (08 EE / 09 FF)
plus the RFCOMM multiplexer control channel. Everything else is dropped: all HCI
commands and events (device addresses, names, link keys), SDP, audio, other RFCOMM
channels (hands-free AT commands) and other connections.

    python scrub_btsnoop.py capture.cfa scrubbed.btsnoop transcript.txt
"""
import struct
import sys

PSM_RFCOMM = 3


def records(data):
    i = 16
    while i + 24 <= len(data):
        orig, incl, flags, drops, ts = struct.unpack('>IIIIq', data[i:i + 24])
        yield data[i:i + 24], flags, ts, data[i + 24:i + 24 + incl]
        i += 24 + incl


def acl(packet):
    """(handle, cid, payload) for a first ACL fragment, else None."""
    if len(packet) < 9 or packet[0] != 2:
        return None
    handle, = struct.unpack('<H', packet[1:3])
    if (handle >> 12) & 3 == 1:  # continuation fragment: no L2CAP header
        return None
    cid, = struct.unpack('<H', packet[7:9])
    return handle & 0xFFF, cid, packet[9:]


def rfcomm(payload):
    """(dlci, info) for a UIH frame, else (dlci, None)."""
    if len(payload) < 4:
        return None, None
    dlci, control = payload[0] >> 2, payload[1]
    if control & 0xEF != 0xEF:
        return dlci, None
    if payload[2] & 1:
        length, at = payload[2] >> 1, 3
    else:
        length, at = (payload[2] >> 1) | (payload[3] << 7), 4
    if control & 0x10 and dlci:  # credit-based flow control byte
        at += 1
    return dlci, payload[at:at + length]


def main(source, target, transcript):
    data = open(source, 'rb').read()
    if data[:8] != b'btsnoop\0':
        sys.exit('Not a btsnoop file.')
    # RFCOMM channels per connection, from L2CAP signalling: (handle, received?) -> CIDs.
    pending, channels = {}, set()
    for _, flags, _, packet in records(data):
        parsed = acl(packet)
        if not parsed or parsed[1] != 1:
            continue
        handle, _, sig = parsed
        rx = bool(flags & 1)
        while len(sig) >= 4:
            code, ident, length = sig[0], sig[1], struct.unpack('<H', sig[2:4])[0]
            body = sig[4:4 + length]
            if code == 0x02 and len(body) >= 4 and struct.unpack('<H', body[:2])[0] == PSM_RFCOMM:
                pending[(handle, ident)] = (rx, struct.unpack('<H', body[2:4])[0])
            elif code == 0x03 and len(body) >= 8 and (handle, ident) in pending:
                req_rx, scid = pending.pop((handle, ident))
                dcid, result = struct.unpack('<HH', body[:4])[0], struct.unpack('<H', body[4:6])[0]
                if result == 0:
                    # Packets towards the requester use its CID; towards the responder, the responder's.
                    channels.add((handle, not req_rx, scid))
                    channels.add((handle, req_rx, dcid))
            sig = sig[4 + length:]
    # The DLCIs that carry Tribit frames.
    tribit = set()
    for _, flags, _, packet in records(data):
        parsed = acl(packet)
        if parsed and (parsed[0], bool(flags & 1), parsed[1]) in channels:
            dlci, info = rfcomm(parsed[2])
            if info and info[:2] in (b'\x08\xee', b'\x09\xff'):
                tribit.add((parsed[0], dlci))
    handles = {h for h, _ in tribit}
    kept, lines, start = [data[:16]], [], None
    for header, flags, ts, packet in records(data):
        parsed = acl(packet)
        if not parsed or (parsed[0], bool(flags & 1), parsed[1]) not in channels or parsed[0] not in handles:
            continue
        dlci, info = rfcomm(parsed[2])
        if dlci != 0 and (parsed[0], dlci) not in tribit:
            continue
        kept.append(header + packet)
        if dlci and info:
            start = start if start is not None else ts
            lines.append(f"{(ts - start) / 1e6:9.3f}  {'speaker->phone' if flags & 1 else 'phone->speaker'}  {info.hex(' ')}")
    open(target, 'wb').write(b''.join(kept))
    with open(transcript, 'w', encoding='utf-8') as out:
        out.write(f'# Tribit RFCOMM frames from {source.replace(chr(92), "/").split("/")[-1]}\n# seconds  direction  frame\n')
        out.write('\n'.join(lines) + '\n')
    print(f'{len(kept) - 1} packets kept, {len(lines)} Tribit frames, channels {sorted(tribit)}')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
