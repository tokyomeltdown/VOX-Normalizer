"""Minimal WAV writer/reader for the tests (PCM 16/24/32, float 32, optional bext chunk)."""
import struct, numpy as np

def write_wav(path, data, sr, bits=24, fmt='pcm', bext_timeref=None, extra_chunks=()):
    data = np.atleast_2d(np.asarray(data, dtype=np.float64))
    if data.shape[0] > data.shape[1]:
        data = data.T
    ch, n = data.shape
    inter = data.T.reshape(-1)
    if fmt == 'float':
        bits = 32
        raw = inter.astype('<f4').tobytes()
        tag = 3
    else:
        tag = 1
        scale = 2 ** (bits - 1)
        q = np.clip(np.round(inter * scale), -scale, scale - 1).astype(np.int64)
        if bits == 16:
            raw = q.astype('<i2').tobytes()
        elif bits == 32:
            raw = q.astype('<i4').tobytes()
        elif bits == 24:
            b = q.astype('<i4').view(np.uint8).reshape(-1, 4)[:, :3]
            raw = b.tobytes()
        else:
            raise ValueError(bits)
    ba = ch * bits // 8
    fmtck = struct.pack('<HHIIHH', tag, ch, sr, sr * ba, ba, bits)
    chunks = [(b'fmt ', fmtck)]
    if bext_timeref is not None:
        bext = bytearray(602)
        bext[0:20] = b'VOX test description'
        struct.pack_into('<II', bext, 338, bext_timeref & 0xffffffff, bext_timeref >> 32)
        chunks.append((b'bext', bytes(bext)))
    chunks += list(extra_chunks)
    chunks.append((b'data', raw))
    body = b''.join(cid + struct.pack('<I', len(c)) + c + (b'\0' if len(c) % 2 else b'') for cid, c in chunks)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(body)) + b'WAVE' + body)

def read_wav(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'RIFF' and b[8:12] == b'WAVE', 'not wav'
    pos, chunks, fmt, data = 12, [], None, None
    while pos + 8 <= len(b):
        cid = b[pos:pos+4]; sz = struct.unpack('<I', b[pos+4:pos+8])[0]
        chunks.append(cid.decode('latin1'))
        c = b[pos+8:pos+8+sz]
        if cid == b'fmt ':
            fmt = struct.unpack('<HHIIHH', c[:16])
            if fmt[0] == 0xFFFE:
                sub = struct.unpack('<H', c[24:26])[0]
                fmt = (sub,) + fmt[1:]
        elif cid == b'data':
            data = c
        elif cid == b'bext':
            lo, hi = struct.unpack('<II', c[338:346])
            chunks[-1] += '(timeref=%d)' % (lo | (hi << 32))
        pos += 8 + sz + (sz & 1)
    tag, ch, sr, _, _, bits = fmt
    if tag == 3:
        x = np.frombuffer(data, '<f4').astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(data, '<i2') / 32768.0
    elif bits == 24:
        u = np.frombuffer(data, np.uint8).reshape(-1, 3)
        v = (u[:, 0].astype(np.int32) | (u[:, 1].astype(np.int32) << 8) | (u[:, 2].astype(np.int32) << 16))
        v = np.where(v >= 2**23, v - 2**24, v)
        x = v / 2.0**23
    elif bits == 32:
        x = np.frombuffer(data, '<i4') / 2.0**31
    else:
        raise ValueError(bits)
    x = x.reshape(-1, ch).T
    return dict(tag=tag, ch=ch, sr=sr, bits=bits, chunks=chunks, x=x)

def db(v):
    return 20 * np.log10(max(float(v), 1e-12))
