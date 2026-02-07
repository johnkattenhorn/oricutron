# Oricutron MCP Demo Recording

Record a demo video of Claude controlling an Oric Atmos emulator via MCP.

## Prerequisites

Oricutron must be running with `--mcp` flag, and the `oricutron` MCP server must be registered in Claude Code.

## Steps

### 1. Clear previous frames

```bash
rm -f /home/john/Code/oricutron/mcp/demo/frames/frame_*.png
```

### 2. Reset the emulator

Call `oric_reset()` to get a clean boot screen. Wait 2 seconds for it to boot, then capture 5 frames of the boot screen:

```
oric_reset()
# wait 2 seconds
for i in 0..4: oric_save_screenshot(f"/home/john/Code/oricutron/mcp/demo/frames/frame_{i:04d}.png")
```

### 3. Paste the BASIC program

Use `oric_paste()` to enter the entire program in a single call:

```
oric_paste('10 PRINT CHR$(27);"T ORICUTRON MCP DEMO"\n20 PRINT\n30 FOR I=1 TO 7\n40 INK I\n50 PRINT " CLAUDE IS TYPING THIS!"\n60 NEXT I\n70 INK 3\n80 PRINT\n90 PRINT " CONTROLLED VIA UNIX SOCKET"\n')
```

After pasting, wait 3 seconds for all keys to process, then capture 5 frames showing the typed listing.

### 4. Run the program

```
oric_type('RUN\r')
```

Wait 2 seconds, then capture 10 frames of the output.

### 5. List the program

```
oric_type('LIST\r')
```

Wait 2 seconds, then capture 8 frames of the listing.

### 6. Final hold

Capture 5 more frames of the final screen.

### 7. Build the video

```bash
cd /home/john/Code/oricutron/mcp/demo
python capture_demo.py --build --fps 5
```

This creates `oricutron_mcp_demo.mp4`.

## Frame Capture Pattern

For each batch of N frames, use a loop calling `oric_save_screenshot()` with sequential frame numbers. Keep a running frame counter across all steps so frames are numbered continuously (frame_0000.png, frame_0001.png, ...).

## Total

~33 frames at 5 fps = ~6.6 second video.
