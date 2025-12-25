import re
import time
import struct
import math

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


def _normalize_out(out) -> list[str]:
    """
    Normalize shell output to a list of clean text lines (no ANSI).
    Handles:
      - out as list[str]
      - out as str
    """
    if isinstance(out, list):
        lines = out
    else:
        lines = str(out).splitlines()

    # strip ANSI + whitespace
    clean = [_ANSI_RE.sub("", line).rstrip("\r\n") for line in lines]
    # drop empty lines
    return [l for l in clean if l.strip()]


def _as_text(out) -> str:
    return "\n".join(_normalize_out(out))


def _extract_adc_raw(out) -> int:
    text = _as_text(out)
    m = re.search(r"adc raw=(-?\d+)", text)
    assert m, f"Could not parse adc raw from output:\n{text}"
    return int(m.group(1))


def _write_pcm16_mono_wav(path: str, sample_rate_hz: int, samples):
    """Minimal RIFF/WAVE PCM 16-bit mono."""
    data = b"".join(struct.pack("<h", max(-32768, min(32767, int(s)))) for s in samples)

    fmt_chunk_size = 16
    audio_format = 1  # PCM
    num_channels = 1
    bits_per_sample = 16
    byte_rate = sample_rate_hz * num_channels * (bits_per_sample // 8)
    block_align = num_channels * (bits_per_sample // 8)

    riff_chunk_size = 4 + (8 + fmt_chunk_size) + (8 + len(data))

    with open(path, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", riff_chunk_size))
        f.write(b"WAVE")

        f.write(b"fmt ")
        f.write(struct.pack("<I", fmt_chunk_size))
        f.write(struct.pack("<H", audio_format))
        f.write(struct.pack("<H", num_channels))
        f.write(struct.pack("<I", sample_rate_hz))
        f.write(struct.pack("<I", byte_rate))
        f.write(struct.pack("<H", block_align))
        f.write(struct.pack("<H", bits_per_sample))

        f.write(b"data")
        f.write(struct.pack("<I", len(data)))
        f.write(data)


def test_wav_info_and_sine_defaults(shell):
    out = shell.exec_command("wav info")
    text = _as_text(out)
    print(text)

    assert "pipeline running=" in text
    assert "wav:" in text
    assert "sine:" in text

    out = shell.exec_command("wav sine")
    text = _as_text(out)
    assert "started sine" in text

    out = shell.exec_command("wav stop")
    text = _as_text(out)
    assert "stopped" in text


def test_sine_changes_adc(shell):
    shell.exec_command("wav stop")

    out = shell.exec_command("wav sine 1000 1.0 8000")
    assert "started sine" in _as_text(out)

    v1 = _extract_adc_raw(shell.exec_command("adc_read"))

    time.sleep(0.05)

    v2 = _extract_adc_raw(shell.exec_command("adc_read"))

    assert v1 != v2, f"ADC value did not change: v1={v1}, v2={v2}"

    shell.exec_command("wav stop")


def test_wav_load_start_adc(shell, tmp_path):
    shell.exec_command("wav stop")

    sr = 8000
    freq = 1000
    n = 800  # 0.1s
    amp = 0.9

    samples = [
        int(amp * 32767 * math.sin(2 * math.pi * freq * (i / sr)))
        for i in range(n)
    ]

    wav_path = tmp_path / "test.wav"
    _write_pcm16_mono_wav(str(wav_path), sr, samples)

    out = shell.exec_command(f"wav load {wav_path}")
    assert "loaded:" in _as_text(out)

    out = shell.exec_command("wav start")
    assert "started wav" in _as_text(out)

    out = shell.exec_command("adc_read")
    assert "adc raw=" in _as_text(out)

    out = shell.exec_command("wav stop")
    assert "stopped" in _as_text(out)
