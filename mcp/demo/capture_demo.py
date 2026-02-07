#!/usr/bin/env python3
"""
Capture demo: connects to Oricutron MCP socket, drives a demo, captures frames, builds video.

Usage: First stop any existing MCP client (exit Claude Code), then run:
    python capture_demo.py

Or use the --from-frames flag to just build video from existing frames:
    python capture_demo.py --from-frames
"""

import base64
import os
import subprocess
import sys

DEMO_DIR = os.path.dirname(os.path.abspath(__file__))
FRAME_DIR = os.path.join(DEMO_DIR, "frames")


def save_frame(frame_num, b64_png):
    """Save a base64-encoded PNG to the frames directory."""
    os.makedirs(FRAME_DIR, exist_ok=True)
    path = os.path.join(FRAME_DIR, f"frame_{frame_num:04d}.png")
    with open(path, "wb") as f:
        f.write(base64.b64decode(b64_png))
    return path


def build_video(framerate=5):
    """Build MP4 video from saved frames."""
    output_path = os.path.join(DEMO_DIR, "oricutron_mcp_demo.mp4")
    # Count frames
    frames = sorted(f for f in os.listdir(FRAME_DIR) if f.startswith("frame_") and f.endswith(".png"))
    if not frames:
        print("No frames found!")
        return None
    print(f"Building video from {len(frames)} frames at {framerate} fps...")
    subprocess.run([
        "ffmpeg", "-y",
        "-framerate", str(framerate),
        "-i", os.path.join(FRAME_DIR, "frame_%04d.png"),
        "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2",
        "-c:v", "libx264",
        "-pix_fmt", "yuv420p",
        "-crf", "18",
        output_path
    ], check=True)
    print(f"Video saved to: {output_path}")
    return output_path


if __name__ == "__main__":
    if "--from-frames" in sys.argv:
        build_video()
    elif "--save-frame" in sys.argv:
        # Usage: --save-frame <num> <base64data>
        idx = sys.argv.index("--save-frame")
        num = int(sys.argv[idx + 1])
        b64 = sys.argv[idx + 2]
        path = save_frame(num, b64)
        print(f"Saved frame {num}: {path}")
    elif "--build" in sys.argv:
        fps = 5
        if "--fps" in sys.argv:
            fps = int(sys.argv[sys.argv.index("--fps") + 1])
        build_video(fps)
    else:
        print("Usage:")
        print("  capture_demo.py --from-frames    Build video from existing frames")
        print("  capture_demo.py --build --fps N  Build video at N fps")
        print("  capture_demo.py --save-frame NUM BASE64DATA  Save a frame")
