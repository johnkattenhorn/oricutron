# Oricutron MCP Demo

Demonstrate Claude controlling an Oric Atmos emulator via MCP.

## Prerequisites

- Oricutron running with `--mcp` flag
- The `oricutron` MCP server registered in Claude Code

## Instructions

Start your screen recorder, then execute the following steps in order. Wait a few seconds between each step so the viewer can see what's happening.

### 1. Reset the emulator

```
oric_reset()
```

Wait 2 seconds for boot.

### 2. Paste the BASIC program

```
oric_paste('10 PRINT CHR$(27);CHR$(68);" ORICUTRON MCP DEMO"\n20 PRINT\n30 FOR I=1 TO 7\n35 IF I=3 THEN 60\n50 PRINT CHR$(27);CHR$(64+I);" CLAUDE IS TYPING THIS!"\n60 NEXT I\n70 PRINT\n90 PRINT CHR$(27);CHR$(68);" CONTROLLED VIA UNIX SOCKET"\n')
```

Wait 3 seconds for all keys to process.

### 3. Run the program

```
oric_type('RUN\r')
```

Wait 2 seconds.

### 4. List the program

```
oric_type('LIST\r')
```

Wait 2 seconds, then stop recording.
