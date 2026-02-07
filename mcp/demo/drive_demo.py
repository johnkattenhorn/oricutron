#!/usr/bin/env python3
"""
Drive the Oricutron MCP demo capture.

This script talks to the RUNNING MCP server via its internal socket connection
by importing the server module and using its connection object.

Actually, since the MCP server holds the socket, we instead generate a sequence
of MCP tool calls as a script for Claude to execute. But for efficiency,
we'll do it differently: this script outputs shell commands that call
the MCP tools via a small helper.

Instead, let's just use this as a simple orchestrator called from within
the Claude conversation, using subprocess to call a helper.
"""

# This script is meant to be sourced/used as reference for the demo sequence.
# The actual capture is driven by Claude via MCP tool calls.

DEMO_SEQUENCE = [
    # (action, args, hold_frames, description)
    ("reset", None, 10, "Boot screen"),
    ("snap", None, 5, "Hold on boot screen"),
    ("type", '10 PRINT CHR$(27);CHR$(68);" ORICUTRON MCP DEMO"\\r', 4, "Line 10"),
    ("type", "20 PRINT\\r", 3, "Line 20"),
    ("type", "30 FOR I=1 TO 7\\r", 3, "Line 30"),
    ("type", "35 IF I=3 THEN 60\\r", 3, "Line 35"),
    ("type", '50 PRINT CHR$(27);CHR$(64+I);" CLAUDE IS TYPING THIS!"\\r', 4, "Line 50"),
    ("type", "60 NEXT I\\r", 3, "Line 60"),
    ("type", "70 PRINT\\r", 3, "Line 70"),
    ("type", '90 PRINT CHR$(27);CHR$(68);" CONTROLLED VIA UNIX SOCKET"\\r', 4, "Line 90"),
    ("snap", None, 5, "Pause before RUN"),
    ("type", "RUN\\r", 15, "Run program"),
    ("type", "LIST\\r", 12, "List program"),
    ("snap", None, 8, "Final hold"),
]

if __name__ == "__main__":
    print("Demo sequence has", len(DEMO_SEQUENCE), "steps")
    total_frames = sum(step[2] for step in DEMO_SEQUENCE)
    print(f"Total frames: ~{total_frames}")
    print(f"At 5 fps, video will be ~{total_frames/5:.1f} seconds")
