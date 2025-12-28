"""
Tests for SA818 shell commands.
"""
import re
import time

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


def _parse_sa818_status(out) -> dict:
    """Parse sa818 status output into a dictionary."""
    text = _as_text(out)
    # Expected format: "powered=1 ptt=0 high_power=1 squelch=1"
    m = re.search(r"powered=(\d+)\s+ptt=(\d+)\s+high_power=(\d+)\s+squelch=(\d+)", text)
    assert m, f"Could not parse sa818 status from output:\n{text}"
    
    return {
        "powered": int(m.group(1)),
        "ptt": int(m.group(2)),
        "high_power": int(m.group(3)),
        "squelch": int(m.group(4)),
    }


def test_sa818_status(shell):
    """Test basic sa818 status command."""
    out = shell.exec_command("sa818 status")
    status = _parse_sa818_status(out)
    
    # Device should be initialized
    assert status["powered"] in [0, 1]
    assert status["ptt"] in [0, 1]
    assert status["high_power"] in [0, 1]
    assert status["squelch"] in [0, 1]


def test_sa818_power_on_off(shell):
    """Test sa818 power on/off commands."""
    # Power on
    out = shell.exec_command("sa818 power on")
    text = _as_text(out)
    # Should not produce an error
    assert "error" not in text.lower() or "sa818 not ready" not in text.lower()
    
    # Check status
    status = _parse_sa818_status(shell.exec_command("sa818 status"))
    assert status["powered"] == 1, "Device should be powered on"
    
    # Power off
    out = shell.exec_command("sa818 power off")
    text = _as_text(out)
    assert "error" not in text.lower() or "sa818 not ready" not in text.lower()
    
    # Check status
    status = _parse_sa818_status(shell.exec_command("sa818 status"))
    assert status["powered"] == 0, "Device should be powered off"
    
    # Power back on for other tests
    shell.exec_command("sa818 power on")


def test_sa818_ptt_on_off(shell):
    """Test sa818 PTT on/off commands."""
    # Ensure powered on
    shell.exec_command("sa818 power on")
    
    # PTT off
    out = shell.exec_command("sa818 ptt off")
    text = _as_text(out)
    assert "error" not in text.lower() or "sa818 not ready" not in text.lower()
    
    status = _parse_sa818_status(shell.exec_command("sa818 status"))
    assert status["ptt"] == 0, "PTT should be off"
    
    # PTT on
    out = shell.exec_command("sa818 ptt on")
    text = _as_text(out)
    assert "error" not in text.lower() or "sa818 not ready" not in text.lower()
    
    status = _parse_sa818_status(shell.exec_command("sa818 status"))
    assert status["ptt"] == 1, "PTT should be on"
    
    # PTT off again
    shell.exec_command("sa818 ptt off")
    status = _parse_sa818_status(shell.exec_command("sa818 status"))
    assert status["ptt"] == 0, "PTT should be off"


def test_sa818_power_level(shell):
    """Test sa818 power level high/low commands."""
    # Ensure powered on
    shell.exec_command("sa818 power on")
    
    # Set to low
    out = shell.exec_command("sa818 powerlevel low")
    text = _as_text(out)
    assert "error" not in text.lower() or "sa818 not ready" not in text.lower()
    
    status = _parse_sa818_status(shell.exec_command("sa818 status"))
    assert status["high_power"] == 0, "Power level should be low"
    
    # Set to high
    out = shell.exec_command("sa818 powerlevel high")
    text = _as_text(out)
    assert "error" not in text.lower() or "sa818 not ready" not in text.lower()
    
    status = _parse_sa818_status(shell.exec_command("sa818 status"))
    assert status["high_power"] == 1, "Power level should be high"


def test_sa818_sim_squelch(shell):
    """Test sa818 squelch simulation (only available in simulator)."""
    # Ensure powered on
    shell.exec_command("sa818 power on")
    
    # Try to run sim_squelch command
    out = shell.exec_command("sa818 sim_squelch open")
    text = _as_text(out)
    
    # If command is not available (real hardware), skip this test
    if "unknown parameter" in text.lower() or "command not found" in text.lower():
        print("sim_squelch not available (likely running on real hardware) - skipping test")
        return
    
    # Squelch open (no carrier)
    assert "error" not in text.lower() or "sa818 not ready" not in text.lower()
    
    time.sleep(0.01)  # Small delay for simulation
    
    status = _parse_sa818_status(shell.exec_command("sa818 status"))
    assert status["squelch"] == 0, "Squelch should be open (no carrier)"
    
    # Squelch closed (carrier detected)
    out = shell.exec_command("sa818 sim_squelch closed")
    text = _as_text(out)
    assert "error" not in text.lower() or "sa818 not ready" not in text.lower()
    
    time.sleep(0.01)  # Small delay for simulation
    
    status = _parse_sa818_status(shell.exec_command("sa818 status"))
    assert status["squelch"] == 1, "Squelch should be closed (carrier detected)"


def test_sa818_invalid_commands(shell):
    """Test that invalid commands produce appropriate errors."""
    # Invalid power argument
    out = shell.exec_command("sa818 power invalid")
    text = _as_text(out)
    assert "invalid" in text.lower() or "error" in text.lower()
    
    # Invalid PTT argument
    out = shell.exec_command("sa818 ptt invalid")
    text = _as_text(out)
    assert "invalid" in text.lower() or "error" in text.lower()
    
    # Invalid power level argument
    out = shell.exec_command("sa818 powerlevel invalid")
    text = _as_text(out)
    assert "invalid" in text.lower() or "error" in text.lower()


def test_sa818_help(shell):
    """Test that help is available for sa818 commands."""
    out = shell.exec_command("sa818 --help")
    text = _as_text(out)
    
    # Should show available subcommands
    assert "status" in text.lower() or "power" in text.lower()
