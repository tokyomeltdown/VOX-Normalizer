#!/usr/bin/env python3
"""Regression tests for VOX Normalizer's processing.

Synthesises test files, runs them through tests/build/harness (the real
processor code) and checks the exported audio numerically.

    bash build.sh && bash tests/build_harness.sh && python3 tests/run_tests.py [TEST ...]

Needs Python 3 with numpy. Exit status is the number of failed checks.

Note: like the app, the processor writes its audio-device settings to
~/Library/Application Support/VOX Normalizer.settings when it is created.
"""
import os, re, shutil, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from wavtools import write_wav, read_wav, db

HARNESS = os.path.join(HERE, 'build', 'harness')
W = os.path.join(HERE, 'build', 'work')
SR = 48000
CEILING = -1.0                     # the app's default Peak Ceiling
rng = np.random.default_rng(1)
failures = []


def run(inp, out, *opts):
    r = subprocess.run([HARNESS, inp, out, *map(str, opts)], capture_output=True, text=True)
    return r.stdout + r.stderr


def check(name, ok, detail=''):
    print(('PASS  ' if ok else 'FAIL  ') + name + ('  ::  ' + detail if detail else ''))
    if not ok:
        failures.append(name)


def known(name, detail):
    print('KNOWN ' + name + '  ::  ' + detail)


def phrase(sec, rms_db, sr=SR):
    """Noise shaped a little like a sung phrase, at an exact RMS."""
    n = int(sec * sr)
    x = np.convolve(rng.standard_normal(n), np.ones(8) / 8, mode='same')
    x *= 0.6 + 0.4 * np.sin(np.linspace(0, 6 * np.pi, n)) ** 2
    return x * 10 ** (rms_db / 20) / np.sqrt(np.mean(x ** 2))


def sil(sec, sr=SR, floor_db=-90):
    return rng.standard_normal(int(sec * sr)) * 10 ** (floor_db / 20)


def peak_db(path):
    return db(np.max(np.abs(read_wav(path)['x'])))


def clips_from(log):
    return [(int(s), int(l), 10 ** (float(g) / 20))
            for s, l, g in re.findall(r'start=(\d+) len=(\d+) .*?gain=([+-][\d.]+)', log)]


def gain_never_exceeds_own_region(name, p_in, p_out, log):
    """Every sample's gain must be at most the gain of the clip (or gap = 1.0) it belongs to.
    This is what guarantees Peak Ceiling through the boundary ramps."""
    x = read_wav(p_in)['x'][0]
    y = read_wav(p_out)['x'][0]
    own = np.ones_like(x)
    clips = clips_from(log)
    for s, l, g in clips:
        own[s:s + l] = g
    m = np.abs(x) > 1e-3
    ratio = np.zeros_like(x)
    ratio[m] = y[m] / x[m]
    over = m & (ratio > own * 1.002 + 1e-9)
    check(name + ': gain never exceeds its own region', not over.any(), '%d samples over' % over.sum())
    for (s1, l1, g1), (s2, l2, g2) in zip(clips, clips[1:]):
        if s1 + l1 == s2:          # touching clips: the level must not dip or bump at the join
            w = slice(s2 - 480, s2 + 480)
            r = ratio[w][m[w]]
            if r.size:
                ok = (r >= min(g1, g2) * 0.998).all() and (r <= max(g1, g2) * 1.002).all()
                check(name + ': no dip or bump where clips touch', ok, 'gain %.3f..%.3f' % (r.min(), r.max()))


# ---------------------------------------------------------------------------
def test_basic():
    """Five phrases at different levels, one with a plosive."""
    parts = [sil(0.5)]
    for r in (-30, -20, -12, -26, -16):
        parts += [phrase(1.0, r), sil(0.4)]
    x = np.concatenate(parts)
    i4 = int((0.5 + 3 * 1.4 + 0.5) * SR)
    x[i4:i4 + 20] = 0.79                             # plosive in phrase 4 (-2 dBFS)
    p_in, p_out = W + '/basic.wav', W + '/basic_out.wav'
    write_wav(p_in, x, SR, 24)
    log = run(p_in, p_out)
    o = read_wav(p_out)
    check('basic: format kept', (o['tag'], o['bits'], o['sr'], o['ch']) == (1, 24, SR, 1))
    check('basic: peak <= Peak Ceiling', peak_db(p_out) <= CEILING + 1e-3, '%.3f dBFS' % peak_db(p_out))
    n = int(0.4 * SR)
    check('basic: audio outside the clips is bit exact', np.array_equal(read_wav(p_in)['x'][0][:n], o['x'][0][:n]))
    gain_never_exceeds_own_region('basic', p_in, p_out, log)

    log = run(p_in, W + '/basic_whole.wav', '--whole')
    y = read_wav(W + '/basic_whole.wav')['x'][0]
    g = clips_from(log)[0][2]
    seg = np.r_[0:720, len(x) - 720:len(x)]
    check('whole file: no fade at the head or tail', np.allclose(y[seg], read_wav(p_in)['x'][0][seg] * g, atol=2e-7))


def test_hot_onset():
    """A loud phrase turned down, with its peak in the first 10 ms (v1.2 left it near 0 dBFS)."""
    start = 48000
    x = np.concatenate([sil(1.0), phrase(1.0, -10), sil(1.0)])
    x[start + 5:start + 8] = 0.97
    p_in, p_out = W + '/onset.wav', W + '/onset_out.wav'
    write_wav(p_in, x, SR, 24)
    log = run(p_in, p_out)
    check('hot onset: peak <= Peak Ceiling', peak_db(p_out) <= CEILING + 1e-3, '%.3f dBFS' % peak_db(p_out))
    gain_never_exceeds_own_region('hot onset', p_in, p_out, log)


def test_touching_clips():
    """A quiet clip (+12 dB) dragged against a loud one (v1.2 clipped at 0 dBFS)."""
    x = np.concatenate([sil(0.5), phrase(1.0, -34), sil(0.5), phrase(1.0, -14), sil(0.5)])
    ls = int(2.0 * SR)
    x[ls + 10:ls + 14] = 0.6
    p_in = W + '/touch.wav'
    write_wav(p_in, x, SR, 24)
    p_out = W + '/touch_up.wav'
    log = run(p_in, p_out, '--move', 0, 'end', ls + 100000)    # clamped to the next clip's start
    full = int(np.sum(np.abs(read_wav(p_out)['x']) >= 0.99999))
    check('touching (quiet then loud): no clipping', full == 0 and peak_db(p_out) <= CEILING + 1e-3,
          '%.3f dBFS, %d full-scale samples' % (peak_db(p_out), full))
    gain_never_exceeds_own_region('touching (quiet then loud)', p_in, p_out, log)
    p_out = W + '/touch_down.wav'
    log = run(p_in, p_out, '--move', 1, 'start', 0)              # clamped to the previous clip's end
    gain_never_exceeds_own_region('touching (loud then quiet)', p_in, p_out, log)


def test_metadata():
    """A WAV's BWF time stamp survives; a plain WAV gains no chunks."""
    x = np.concatenate([sil(0.3), phrase(1.0, -24), sil(0.3)])
    p_in, p_out = W + '/bwf.wav', W + '/bwf_out.wav'
    write_wav(p_in, x, SR, 24, bext_timeref=123456789)
    run(p_in, p_out)
    check('BWF time stamp kept', 'bext(timeref=123456789)' in read_wav(p_out)['chunks'], str(read_wav(p_out)['chunks']))
    log = run(p_in, W + '/bwf_out.aiff')
    check('WAV with metadata exported as AIFF', 'export=ok' in log and 'Assertion' not in log)
    write_wav(W + '/plain.wav', x, SR, 24)
    run(W + '/plain.wav', W + '/plain_out.wav')
    check('plain WAV gains no chunks', read_wav(W + '/plain_out.wav')['chunks'] == ['JUNK', 'fmt ', 'data'],
          str(read_wav(W + '/plain_out.wav')['chunks']))


def test_formats():
    x = np.concatenate([sil(0.3), phrase(1.0, -24), sil(0.3)])
    write_wav(W + '/f32.wav', x, SR, fmt='float')
    run(W + '/f32.wav', W + '/f32_out.wav')
    o = read_wav(W + '/f32_out.wav')
    check('32-bit float stays 32-bit float', (o['tag'], o['bits']) == (3, 32))
    y = x.copy()
    y[int(0.5 * SR)] = 1.41                                      # +3 dBFS over in a float file
    write_wav(W + '/f32_over.wav', y, SR, fmt='float')
    run(W + '/f32_over.wav', W + '/f32_over_out.wav')
    check('float source with an over: peak <= Peak Ceiling', peak_db(W + '/f32_over_out.wav') <= CEILING + 1e-3)
    write_wav(W + '/i32.wav', x, SR, 32)
    run(W + '/i32.wav', W + '/i32_out.wav')
    o = read_wav(W + '/i32_out.wav')
    known('32-bit integer WAV is written as 32-bit float', 'tag=%d bits=%d (JUCE default writer format)' % (o['tag'], o['bits']))

    sr = 44100
    st = np.vstack([np.concatenate([sil(0.3, sr), phrase(1.0, r, sr), sil(0.3, sr)]) for r in (-26, -20)])
    write_wav(W + '/st16.wav', st, sr, 16)
    run(W + '/st16.wav', W + '/st16_out.wav')
    o = read_wav(W + '/st16_out.wav')
    check('stereo 16-bit 44.1 kHz kept', (o['tag'], o['bits'], o['sr'], o['ch']) == (1, 16, sr, 2))
    six = np.vstack([np.concatenate([sil(0.3), phrase(1.0, -20 - k), sil(0.3)]) for k in range(6)])
    write_wav(W + '/six.wav', six, SR, 24)
    run(W + '/six.wav', W + '/six_out.wav')
    check('6 channels kept', read_wav(W + '/six_out.wav')['ch'] == 6)


def test_edge_cases():
    write_wav(W + '/empty.wav', np.zeros((1, 0)), SR, 24)
    check('empty file is refused', 'LOAD FAILED' in run(W + '/empty.wav', W + '/empty_out.wav'))
    write_wav(W + '/zeros.wav', np.zeros(SR), SR, 24)
    check('digital silence: no clips, no export', 'export=FAILED' in run(W + '/zeros.wav', W + '/zeros_out.wav'))
    sq = np.sign(np.sin(np.linspace(0, 400 * np.pi, 2 * SR)))
    write_wav(W + '/square.wav', sq, SR, 24)
    run(W + '/square.wav', W + '/square_out.wav')
    check('fully clipped source: peak <= Peak Ceiling', peak_db(W + '/square_out.wav') <= CEILING + 1e-3)


def test_writing():
    x = np.concatenate([sil(0.3), phrase(1.0, -24), sil(0.3)])
    write_wav(W + '/w.wav', x, SR, 24)
    ro = W + '/readonly'
    os.makedirs(ro, exist_ok=True)
    os.chmod(ro, 0o755)
    write_wav(ro + '/existing.wav', x * 0.5, SR, 24)
    before = open(ro + '/existing.wav', 'rb').read()
    os.chmod(ro, 0o555)
    try:
        log = run(W + '/w.wav', ro + '/existing.wav')
        after = open(ro + '/existing.wav', 'rb').read()
        left = [f for f in os.listdir(ro) if 'voxtmp' in f]
    finally:
        os.chmod(ro, 0o755)
    check('read-only folder: export fails, original intact, no temp file',
          'export=FAILED' in log and before == after and not left)
    shutil.copy(W + '/w.wav', W + '/self.wav')
    log = run(W + '/self.wav', W + '/self.wav')
    check('overwrite the loaded file itself', 'export=ok' in log and read_wav(W + '/self.wav')['x'].shape[1] == len(x))
    write_wav(W + '/target.wav', np.zeros(100), SR, 16)
    log = run(W + '/w.wav', W + '/target.wav')
    o = read_wav(W + '/target.wav')
    left = [f for f in os.listdir(W) if 'voxtmp' in f]
    check('overwrite another existing file', 'export=ok' in log and o['x'].shape[1] == len(x) and o['bits'] == 24 and not left)


TESTS = {'basic': test_basic, 'hot_onset': test_hot_onset, 'touching': test_touching_clips,
         'metadata': test_metadata, 'formats': test_formats, 'edge': test_edge_cases, 'writing': test_writing}

if __name__ == '__main__':
    if not os.path.exists(HARNESS):
        sys.exit('harness not found: run  bash build.sh && bash tests/build_harness.sh  first')
    os.makedirs(W, exist_ok=True)
    for t in sys.argv[1:] or TESTS:
        print('==== ' + t)
        TESTS[t]()
    print('\n%d failed' % len(failures) + (': ' + ', '.join(failures) if failures else ''))
    sys.exit(len(failures))
