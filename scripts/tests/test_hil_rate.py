import pathlib
import random
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from hil_rate import evaluate, fit_rate


def _bursty(rate: float, seconds: float, period_s: float, jitter_s: float, seed: int = 1):
    """Reads that return one period of frames each, with arrival jitter."""
    rng = random.Random(seed)
    points, t, frames = [], 1000.0, 0
    while t < 1000.0 + seconds:
        t += period_s
        frames = int(round((t - 1000.0) * rate))
        points.append((t + rng.uniform(-jitter_s, jitter_s), frames))
    return points


def test_fit_rate_exact_line():
    pts = [(10.0 + i, 8000 * i) for i in range(10)]
    assert fit_rate(pts) == pytest.approx(8000.0)


def test_fit_rate_recovers_msis_offset_from_jittery_bursts():
    # the measured pre-fix value on .211: 8052.06 frames/s
    pts = _bursty(8052.06, 60.0, 0.01, 0.004)
    assert fit_rate(pts) == pytest.approx(8052.06, abs=0.05)


def test_fit_rate_is_offset_independent():
    pts = [(t, f + 123456) for t, f in _bursty(8000.0, 30.0, 0.01, 0.0)]
    assert fit_rate(pts) == pytest.approx(8000.0, abs=0.05)


def test_fit_rate_rejects_degenerate_input():
    with pytest.raises(ValueError):
        fit_rate([(1.0, 0)])
    with pytest.raises(ValueError):
        fit_rate([(1.0, 0), (1.0, 8)])


def test_evaluate_pass_within_tolerance():
    ok, line = evaluate(8000.3, 8000, 0.5)
    assert ok and line.startswith("PASS")
    assert "+37.5 ppm" in line


def test_evaluate_fails_on_msis_rate():
    ok, line = evaluate(8052.06, 8000, 0.5)
    assert not ok and line.startswith("FAIL")
    assert "+0.6508 %" in line
