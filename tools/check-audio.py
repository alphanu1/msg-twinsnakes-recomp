#!/usr/bin/env python3
"""Judge a run's mixed audio: gaps, discontinuities, clipping, level.

WHY THIS EXISTS. Every audio measurement in this port was a COUNT - AX
frames, voice-mixes, starves - and a count cannot say whether the result
sounds right. Both audio faults that actually mattered were reported by ear
first: a soundtrack drifting slower (the clock forgiving its arrears,
HANDOFF F282) and words breaking into pieces (the device running dry,
F284). The counters said the mixer was busy throughout.

So this measures the artefacts those faults produce, in the samples
themselves:

  GAPS            runs of exact silence in the middle of sound. A voice that
                  stops for a refill leaves one; so does a starved device.
  DISCONTINUITIES a jump between neighbouring samples larger than any real
                  waveform at this rate would make. This is what a splice
                  sounds like - the click in "At t tac ck".
  CLIPPING        samples pinned at the rails, which is distortion.
  LEVEL           peak and RMS, to catch "technically fine but inaudible".

usage: twin-snakes ... MGS_AUDIO_WAV=out.pcm
       check-audio.py out.pcm [--rate 32000]
"""
import struct, sys

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    rate = 32000
    for i, a in enumerate(sys.argv):
        if a == '--rate':
            rate = int(sys.argv[i + 1])
            args = [x for x in args if x != sys.argv[i + 1]]
    if not args:
        print(__doc__); return 2

    raw = open(args[0], 'rb').read()
    n = len(raw) // 4                       # 16-bit stereo frames
    if n == 0:
        print("no audio"); return 1
    s = struct.unpack('<%dh' % (n * 2), raw[:n * 4])
    left = s[0::2]

    secs = n / float(rate)
    peak = max(max(left), -min(left), 1)
    rms = (sum(v * v for v in left) / float(n)) ** 0.5
    clipped = sum(1 for v in s if v >= 32767 or v <= -32768)

    # Silence runs of at least 1 ms, counted only between non-silent audio,
    # because leading and trailing silence is not a gap.
    first = next((i for i, v in enumerate(left) if v), None)
    last = next((i for i in range(n - 1, -1, -1) if left[i]), None)
    gaps = []
    if first is not None and last is not None and last > first:
        run = 0
        for i in range(first, last + 1):
            if left[i] == 0:
                run += 1
            else:
                if run >= rate // 1000:
                    gaps.append(run)
                run = 0

    # DISCONTINUITIES, BY SIZE - because one threshold cannot tell a splice
    # from a cymbal. At 32 kHz a full-scale 4 kHz tone steps about 25,000
    # between neighbouring samples, so "more than a quarter of full scale"
    # flags ordinary treble: it read 189 a second on audio whose genuinely
    # impossible jumps had already been fixed, and would have sent anyone
    # reading it hunting a fault that was not there.
    #
    # A step larger than full scale itself cannot come from a waveform at
    # all - it is a splice, a clamp, or a decode fault - so that is what the
    # verdict is based on.
    tiers = []
    for frac in (0.25, 0.5, 0.9, 1.5):
        t = int(32768 * frac)
        tiers.append((frac, sum(1 for i in range(1, n)
                                if abs(left[i] - left[i - 1]) > t)))
    disc = tiers[3][1]

    print(f"  {n} frames, {secs:.1f}s at {rate} Hz")
    print(f"  level:          peak {peak} ({100.0*peak/32767:.0f}% FS), "
          f"rms {rms:.0f} ({100.0*rms/32767:.1f}% FS)")
    print(f"  clipping:       {clipped} samples ({100.0*clipped/(n*2):.2f}%)")
    print(f"  gaps >=1ms:     {len(gaps)}"
          + (f", total {sum(gaps)/float(rate)*1000:.0f} ms, "
             f"longest {max(gaps)/float(rate)*1000:.0f} ms" if gaps else ""))
    print("  jumps between neighbouring samples:")
    for frac, c in tiers:
        note = "  <- impossible in real audio" if frac >= 1.5 else ""
        print(f"     > {int(frac*100):3d}% FS: {c:8d}  ({c/secs:7.1f}/s){note}")
    bad = []
    if len(gaps) > secs:            bad.append("gaps")
    if disc / max(secs, 1) > 1:     bad.append("splices")
    if clipped * 200 > n:           bad.append("clipping")
    print("  VERDICT: " + ("clean" if not bad else "PROBLEMS: " + ", ".join(bad)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
