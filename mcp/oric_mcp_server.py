#!/usr/bin/env python3
"""
Oricutron MCP Server - Control the Oricutron Oric Atmos emulator via MCP.

Connects to Oricutron's Unix domain socket (launched with --mcp flag)
and exposes tools for typing, reading the screen, screenshots, etc.

Usage:
    claude mcp add --transport stdio oricutron -- python /path/to/oric_mcp_server.py
"""

import base64
import io
import os
import socket
import struct
from contextlib import asynccontextmanager
from dataclasses import dataclass, field

from mcp.server.fastmcp import FastMCP

SOCKET_PATH = "/tmp/oricutron-mcp.sock"


@dataclass
class OricutronConnection:
    """Manages Unix socket connection to Oricutron."""

    path: str = SOCKET_PATH
    sock: socket.socket | None = field(default=None, repr=False)
    _buf: bytes = field(default=b"", repr=False)

    def connect(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.path)
        self.sock.settimeout(5.0)

    def disconnect(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def send_command(self, cmd: str) -> str:
        """Send a text command and return the response up to END marker."""
        if not self.sock:
            raise ConnectionError("Not connected to Oricutron")
        self.sock.sendall((cmd + "\n").encode())
        return self._read_response()

    def send_command_binary(self, cmd: str) -> tuple[str, bytes]:
        """Send a command that returns binary data (OKBIN header + raw bytes)."""
        if not self.sock:
            raise ConnectionError("Not connected to Oricutron")
        self.sock.sendall((cmd + "\n").encode())
        return self._read_binary_response()

    def _recv_more(self):
        data = self.sock.recv(65536)
        if not data:
            raise ConnectionError("Connection closed")
        self._buf += data

    def _read_response(self) -> str:
        """Read text response until END marker."""
        result = b""
        while True:
            self._recv_more()
            # Check for END\n in buffer
            end_idx = self._buf.find(b"END\n")
            if end_idx >= 0:
                result = self._buf[:end_idx]
                self._buf = self._buf[end_idx + 4:]
                return result.decode("utf-8", errors="replace").strip()

    def _read_binary_response(self) -> tuple[str, bytes]:
        """Read OKBIN response: header line + binary data + END."""
        # Read until we get the header line
        while b"\n" not in self._buf:
            self._recv_more()

        newline_idx = self._buf.index(b"\n")
        header = self._buf[:newline_idx].decode()
        self._buf = self._buf[newline_idx + 1:]

        if header.startswith("ERR"):
            # Consume rest
            while b"END\n" not in self._buf and b"\n" not in self._buf:
                try:
                    self._recv_more()
                except Exception:
                    break
            self._buf = b""
            raise RuntimeError(header)

        # Parse "OKBIN <width> <height> <nbytes>"
        parts = header.split()
        if len(parts) < 4 or parts[0] != "OKBIN":
            raise RuntimeError(f"Unexpected response: {header}")

        nbytes = int(parts[3])

        # Read exactly nbytes of binary data
        while len(self._buf) < nbytes:
            self._recv_more()

        binary_data = self._buf[:nbytes]
        self._buf = self._buf[nbytes:]

        # Skip trailing \nEND\n
        while b"END\n" not in self._buf:
            self._recv_more()
        end_idx = self._buf.index(b"END\n")
        self._buf = self._buf[end_idx + 4:]

        return header, binary_data


# Global connection
conn = OricutronConnection()


@asynccontextmanager
async def app_lifespan(server):
    conn.connect()
    try:
        yield
    finally:
        conn.disconnect()


mcp = FastMCP("oricutron", lifespan=app_lifespan)


@mcp.tool()
def oric_type(text: str) -> str:
    """Type text on the Oric keyboard. Use \\r for RETURN key.

    Examples:
        oric_type('10 PRINT "HELLO"\\r')
        oric_type('RUN\\r')
        oric_type('CLOAD""\\r')
    """
    # Process escape sequences - MCP sends literal \r not CR byte
    text = text.replace("\\r", "\r").replace("\\n", "\n")
    encoded = base64.b64encode(text.encode()).decode()
    response = conn.send_command(f"QUEUEKEYS {encoded}")
    if response.startswith("ERR"):
        return f"Error: {response}"
    return "OK - keys queued"


@mcp.tool()
def oric_paste(text: str) -> str:
    """Paste text into the Oric, like clipboard paste. Newlines become RETURN key.

    Use this to enter multi-line BASIC programs in a single call.
    Tabs become spaces, newlines become RETURN, control chars are stripped.

    Example:
        oric_paste('10 PRINT "HELLO"\\n20 GOTO 10\\n')
    """
    # Process common escape sequences from MCP
    text = text.replace("\\n", "\n").replace("\\r", "\r").replace("\\t", "\t")
    encoded = base64.b64encode(text.encode()).decode()
    response = conn.send_command(f"PASTE {encoded}")
    if response.startswith("ERR"):
        return f"Error: {response}"
    return "OK - text pasted"


@mcp.tool()
def oric_read_screen() -> str:
    """Read the Oric text-mode screen as 28 lines of 40 characters.

    Returns the current text screen contents. Useful for reading
    BASIC listings, program output, error messages, etc.
    """
    response = conn.send_command("READSCREEN")
    if response.startswith("ERR"):
        return f"Error: {response}"
    # Strip the leading "OK\n"
    if response.startswith("OK\n"):
        return response[3:]
    return response


def _capture_screenshot():
    """Internal: capture screenshot and return PIL Image."""
    from PIL import Image

    header, rgb_data = conn.send_command_binary("SCREENSHOT")
    parts = header.split()
    width = int(parts[1])
    height = int(parts[2])

    img = Image.frombytes("RGB", (width, height), rgb_data)
    # Scale 3x for better visibility
    return img.resize((width * 3, height * 3), Image.NEAREST)


@mcp.tool()
def oric_screenshot() -> list:
    """Capture the Oric display as a PNG image (240x224, scaled 3x to 720x672).

    Returns the screenshot as an embedded image that Claude can see.
    Works in both text and graphics modes.
    """
    img = _capture_screenshot()

    buf = io.BytesIO()
    img.save(buf, format="PNG")
    png_bytes = buf.getvalue()

    return [
        {
            "type": "image",
            "data": base64.b64encode(png_bytes).decode(),
            "mimeType": "image/png",
        }
    ]


@mcp.tool()
def oric_save_screenshot(path: str) -> str:
    """Save a screenshot of the Oric display to a PNG file on disk.

    Args:
        path: Absolute path where the PNG file will be saved.

    Use this for capturing sequences of frames for video creation.
    """
    img = _capture_screenshot()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, format="PNG")
    return f"Saved: {path}"


@mcp.tool()
def oric_load_tape(path: str, auto_run: bool = False) -> str:
    """Load a .tap tape image file into the Oric.

    Args:
        path: Absolute path to the .tap file
        auto_run: If True, automatically type CLOAD"" and RETURN to load
    """
    response = conn.send_command(f"LOADTAPE {path}")
    if response.startswith("ERR"):
        return f"Error: {response}"

    result = "Tape loaded successfully"
    if auto_run:
        encoded = base64.b64encode(b'CLOAD""\r').decode()
        conn.send_command(f"QUEUEKEYS {encoded}")
        result += " - CLOAD queued"

    return result


@mcp.tool()
def oric_reset() -> str:
    """Soft reset the Oric emulator. Equivalent to pressing the reset button."""
    response = conn.send_command("RESET")
    if response.startswith("ERR"):
        return f"Error: {response}"
    return "Emulator reset"


@mcp.tool()
def oric_read_memory(address: int, length: int = 256) -> str:
    """Read a hex dump of Oric RAM/ROM.

    Args:
        address: Start address (0-65535)
        length: Number of bytes to read (max 65536)

    Common addresses:
        0x0000-0x00FF: Zero page
        0x0400-0x07FF: BASIC program area start
        0xBB80-0xBFDF: Text screen memory (40x28)
        0xA000-0xBF3F: HIRES screen memory
    """
    response = conn.send_command(f"READMEM {address} {length}")
    if response.startswith("ERR"):
        return f"Error: {response}"
    # Strip OK prefix
    hex_data = response
    if hex_data.startswith("OK\n"):
        hex_data = hex_data[3:]

    # Format as readable hex dump
    lines = []
    for i in range(0, len(hex_data), 32):
        chunk = hex_data[i : i + 32]
        addr = address + i // 2
        # Insert spaces between byte pairs
        spaced = " ".join(chunk[j : j + 2] for j in range(0, len(chunk), 2))
        # ASCII representation
        ascii_repr = ""
        for j in range(0, len(chunk), 2):
            byte_val = int(chunk[j : j + 2], 16)
            ascii_repr += chr(byte_val) if 32 <= byte_val < 127 else "."
        lines.append(f"${addr:04X}: {spaced:<48s} {ascii_repr}")

    return "\n".join(lines)


@mcp.tool()
def oric_get_state() -> str:
    """Get the current emulator state: mode, machine type, video mode, tape status."""
    response = conn.send_command("GETSTATE")
    if response.startswith("ERR"):
        return f"Error: {response}"
    if response.startswith("OK\n"):
        return response[3:]
    return response


if __name__ == "__main__":
    mcp.run()
