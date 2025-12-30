# Copyright (c) 2025 OE5XRX
# SPDX-License-Identifier: LGPL-3.0-or-later

"""
SA818 Hardware Simulator

Simulates SA818 module AT command protocol for testing.
Responds to AT commands over a PTY (pseudo-terminal) interface.
"""

import os
import re
import select
import threading
from dataclasses import dataclass
from typing import Optional


@dataclass
class SA818State:
    """Current state of simulated SA818 module."""
    bandwidth: int = 0  # 0=12.5kHz, 1=25kHz
    freq_tx: float = 145.500
    freq_rx: float = 145.500
    ctcss_tx: int = 0
    squelch: int = 4
    ctcss_rx: int = 0
    volume: int = 4
    pre_emphasis: bool = True
    high_pass: bool = True
    low_pass: bool = True
    rssi: int = 120  # Simulated RSSI value


class SA818Simulator:
    """
    SA818 AT command simulator.
    
    Runs in a background thread and responds to AT commands
    over a PTY interface that can be connected to Zephyr's UART.
    """
    
    def __init__(self, pty_path):
        self.master_fd: Optional[int] = None
        self.pty_path = pty_path
        self.state = SA818State()
        self.running = False
        self.thread: Optional[threading.Thread] = None
        self._rx_buffer = b""
        
    def start(self) -> None:
        """
        Start the simulator.

        Opens the PTY at ``self.pty_path`` and starts a background thread
        that processes AT commands.
        """
        self.master_fd = os.open(self.pty_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        
        # Start background thread
        self.running = True
        self.thread = threading.Thread(target=self._run_loop, daemon=True)
        self.thread.start()
        print(f"SA818Simulator started on {self.pty_path}")
    
    def stop(self):
        """Stop the simulator and close PTY."""
        self.running = False
        if self.thread:
            self.thread.join(timeout=2.0)
        
        if self.master_fd is not None:
            os.close(self.master_fd)
        print(f"SA818Simulator stopped on {self.pty_path}")
    
    def _run_loop(self):
        """Main simulator loop running in background thread."""
        while self.running:
            # Wait for data with timeout
            readable, _, _ = select.select([self.master_fd], [], [], 0.1)
            
            if not readable:
                continue
            
            try:
                # Read data from master
                data = os.read(self.master_fd, 256)
                if not data:
                    continue
                
                self._rx_buffer += data
                
                # Process complete commands (terminated by \r or \n)
                while b'\r' in self._rx_buffer or b'\n' in self._rx_buffer:
                    # Find line terminator
                    idx_r = self._rx_buffer.find(b'\r')
                    idx_n = self._rx_buffer.find(b'\n')
                    
                    if idx_r == -1:
                        idx = idx_n
                    elif idx_n == -1:
                        idx = idx_r
                    else:
                        idx = min(idx_r, idx_n)
                    
                    # Extract command
                    cmd_line = self._rx_buffer[:idx].decode('utf-8', errors='ignore')
                    self._rx_buffer = self._rx_buffer[idx+1:]
                    
                    # Process command
                    if cmd_line.strip():
                        print("SA818Simulator received command:", cmd_line.strip())
                        response = self._process_command(cmd_line.strip())
                        if response:
                            os.write(self.master_fd, response.encode('utf-8'))
                            os.write(self.master_fd, b'\r\n')
            
            except Exception as e:
                print(f"SA818Simulator error: {e}")
    
    def _process_command(self, cmd: str) -> str:
        """
        Process an AT command and return response.
        
        Args:
            cmd: Command string (e.g., "AT+DMOSETGROUP=...")
            
        Returns:
            Response string
        """
        # AT command echo (optional)
        # Most SA818 modules don't echo, but can be enabled
        
        # DMOSETGROUP: Set frequency, CTCSS, squelch
        # Format: AT+DMOSETGROUP=BW,TXF,RXF,TXCCS,SQ,RXCCS
        m = re.match(r'AT\+DMOSETGROUP=(\d+),([\d.]+),([\d.]+),(\d+),(\d+),(\d+)', cmd, re.IGNORECASE)
        if m:
            self.state.bandwidth = int(m.group(1))
            self.state.freq_tx = float(m.group(2))
            self.state.freq_rx = float(m.group(3))
            self.state.ctcss_tx = int(m.group(4))
            self.state.squelch = int(m.group(5))
            self.state.ctcss_rx = int(m.group(6))
            return "+DMOSETGROUP:0"
        
        # DMOSETVOLUME: Set volume level
        # Format: AT+DMOSETVOLUME=N (1-8)
        m = re.match(r'AT\+DMOSETVOLUME=(\d+)', cmd, re.IGNORECASE)
        if m:
            volume = int(m.group(1))
            if 1 <= volume <= 8:
                self.state.volume = volume
                return "+DMOSETVOLUME:0"
            else:
                return "+DMOSETVOLUME:1"  # Error
        
        # SETFILTER: Set audio filters
        # Format: AT+SETFILTER=PRE,HPF,LPF
        m = re.match(r'AT\+SETFILTER=(\d+),(\d+),(\d+)', cmd, re.IGNORECASE)
        if m:
            self.state.pre_emphasis = bool(int(m.group(1)))
            self.state.high_pass = bool(int(m.group(2)))
            self.state.low_pass = bool(int(m.group(3)))
            return "+DMOSETFILTER:0"
        
        # RSSI?: Read signal strength
        m = re.match(r'RSSI\?', cmd, re.IGNORECASE)
        if m:
            return f"RSSI={self.state.rssi}"
        
        # DMOCONNECT: Connection handshake
        m = re.match(r'AT\+DMOCONNECT', cmd, re.IGNORECASE)
        if m:
            return "+DMOCONNECT:0"
        
        # VERSION: Read firmware version
        m = re.match(r'AT\+VERSION', cmd, re.IGNORECASE)
        if m:
            return "SA818_V4.2"
        
        # Generic AT test
        if cmd.upper() == "AT":
            return "+DMOCONNECT:0"
        
        # Unknown command
        return "ERROR"
    
    def set_rssi(self, value: int):
        """Update simulated RSSI value."""
        self.state.rssi = value
    
    def get_state(self) -> SA818State:
        """Get current simulator state."""
        return self.state
