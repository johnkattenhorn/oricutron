#!/usr/bin/env python3
"""Build a GIF demo video from captured frames."""
import os
from PIL import Image

DEMO_DIR = os.path.dirname(os.path.abspath(__file__))
FRAME_DIR = os.path.join(DEMO_DIR, "frames")

# (filename, duration_ms)
sequence = [
    ("frame_01_boot.png", 2500),
    ("frame_02_line10.png", 1000),
    ("frame_03_lines.png", 1000),
    ("frame_04_alllines.png", 1500),
    ("frame_05_run.png", 3000),
    ("frame_06_list.png", 3000),
]

frames = []
durations = []
for fname, dur in sequence:
    path = os.path.join(FRAME_DIR, fname)
    img = Image.open(path).convert("RGB")
    frames.append(img)
    durations.append(dur)

output_path = os.path.join(DEMO_DIR, "oricutron_mcp_demo.gif")
frames[0].save(
    output_path,
    save_all=True,
    append_images=frames[1:],
    duration=durations,
    loop=0,
)
print(f"Saved: {output_path}")
print(f"Frames: {len(frames)}, total duration: {sum(durations)/1000:.1f}s")
