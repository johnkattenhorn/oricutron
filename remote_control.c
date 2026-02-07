/*
**  Oricutron
**  Copyright (C) 2009-2014 Peter Gordon
**
**  This program is free software; you can redistribute it and/or
**  modify it under the terms of the GNU General Public License
**  as published by the Free Software Foundation, version 2
**  of the License.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**
**  MCP remote control interface via Unix domain socket
*/

#if !defined(_WIN32) && !defined(WWW)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "system.h"
#include "6502.h"
#include "via.h"
#include "8912.h"
#include "gui.h"
#include "disk.h"
#include "monitor.h"
#include "6551.h"
#include "machine.h"
#include "tape.h"
#include "remote_control.h"

extern Uint8 oricpalette[];

static int listen_fd = -1;
static int client_fd = -1;
static char socket_path_copy[256];

/* Receive buffer */
#define RECV_BUF_SIZE 4096
static char recv_buf[RECV_BUF_SIZE];
static int recv_buf_len = 0;

/* Send helpers */
static SDL_bool send_all(int fd, const void *buf, int len)
{
  const char *p = (const char *)buf;
  while(len > 0)
  {
    int n = send(fd, p, len, MSG_NOSIGNAL);
    if(n <= 0) return SDL_FALSE;
    p += n;
    len -= n;
  }
  return SDL_TRUE;
}

static void send_str(int fd, const char *str)
{
  send_all(fd, str, strlen(str));
}

static void send_ok(int fd)
{
  send_str(fd, "OK\nEND\n");
}

static void send_err(int fd, const char *msg)
{
  send_str(fd, "ERR ");
  send_str(fd, msg);
  send_str(fd, "\n");
}

/* Base64 decoding */
static int b64_decode_char(char c)
{
  if(c >= 'A' && c <= 'Z') return c - 'A';
  if(c >= 'a' && c <= 'z') return c - 'a' + 26;
  if(c >= '0' && c <= '9') return c - '0' + 52;
  if(c == '+') return 62;
  if(c == '/') return 63;
  return -1;
}

static int b64_decode(const char *in, char *out, int max_out)
{
  int i = 0, o = 0;
  int len = strlen(in);

  while(i < len && o < max_out)
  {
    int a = -1, b = -1, c = -1, d = -1;

    while(i < len && (a = b64_decode_char(in[i])) < 0 && in[i] != '=') i++;
    if(i >= len) break;
    if(in[i] == '=') break;
    i++;

    while(i < len && (b = b64_decode_char(in[i])) < 0 && in[i] != '=') i++;
    if(i >= len || in[i] == '=') break;
    i++;

    if(o < max_out) out[o++] = (a << 2) | (b >> 4);

    while(i < len && (c = b64_decode_char(in[i])) < 0 && in[i] != '=') i++;
    if(i >= len || in[i] == '=') { i++; break; }
    i++;

    if(o < max_out) out[o++] = ((b & 0x0f) << 4) | (c >> 2);

    while(i < len && (d = b64_decode_char(in[i])) < 0 && in[i] != '=') i++;
    if(i >= len || in[i] == '=') { i++; break; }
    i++;

    if(o < max_out) out[o++] = ((c & 0x03) << 6) | d;
  }
  return o;
}

/* Command handlers */

static void cmd_queuekeys(int fd, const char *args)
{
  char decoded[2048];
  int len;

  if(!args || !args[0])
  {
    send_err(fd, "QUEUEKEYS requires base64 argument");
    return;
  }

  len = b64_decode(args, decoded, sizeof(decoded) - 1);
  decoded[len] = '\0';
  queuekeys(decoded);
  send_ok(fd);
}

static void cmd_paste(int fd, const char *args)
{
  char *decoded;
  int len, b64len, i;

  if(!args || !args[0])
  {
    send_err(fd, "PASTE requires base64 argument");
    return;
  }

  b64len = strlen(args);
  decoded = malloc(b64len);  /* decoded always <= encoded length */
  if(!decoded)
  {
    send_err(fd, "Out of memory");
    return;
  }

  len = b64_decode(args, decoded, b64len);

  /* Clipboard-style sanitization (same as gui_x11.c:clipboard_paste) */
  for(i = 0; i < len; i++)
  {
    if(decoded[i] == '\t')       decoded[i] = ' ';
    else if(decoded[i] == '\n')  decoded[i] = '\r';
    else if(decoded[i] < 0x20 || (unsigned char)decoded[i] >= 0x7f) decoded[i] = ' ';
  }
  decoded[len] = '\0';

  queuekeys(decoded);
  free(decoded);
  send_ok(fd);
}

static void cmd_readscreen(int fd, struct machine *oric)
{
  int line, col, i;
  char text[40 * 28 + 28 + 1];
  unsigned char *vidmem;

  /* Use vid_addr (normally 0xBB80 in text mode) */
  vidmem = &oric->mem[oric->vid_addr];

  for(i = 0, line = 0; line < 28; line++)
  {
    for(col = 0; col < 40; col++)
    {
      unsigned char c = vidmem[line * 40 + col];

      if(c > 127)
        c -= 128;

      if(c < ' ' || c == 127)
        text[i++] = ' ';
      else
        text[i++] = (char)c;
    }
    text[i++] = '\n';
  }
  text[i] = '\0';

  send_str(fd, "OK\n");
  send_str(fd, text);
  send_str(fd, "END\n");
}

static void cmd_screenshot(int fd, struct machine *oric)
{
  /* oric->scr is 240x224 bytes, each byte is a palette index 0-7 */
  int width = 240, height = 224;
  int rgb_size = width * height * 3;
  unsigned char *rgb;
  char header[64];
  int x, y;

  if(!oric->scr)
  {
    send_err(fd, "Framebuffer not available");
    return;
  }

  rgb = (unsigned char *)malloc(rgb_size);
  if(!rgb)
  {
    send_err(fd, "Out of memory");
    return;
  }

  for(y = 0; y < height; y++)
  {
    for(x = 0; x < width; x++)
    {
      int idx = oric->scr[y * width + x] & 7;
      int off = (y * width + x) * 3;
      rgb[off + 0] = oricpalette[idx * 3 + 0];
      rgb[off + 1] = oricpalette[idx * 3 + 1];
      rgb[off + 2] = oricpalette[idx * 3 + 2];
    }
  }

  sprintf(header, "OKBIN %d %d %d\n", width, height, rgb_size);
  send_str(fd, header);
  send_all(fd, rgb, rgb_size);
  send_str(fd, "\nEND\n");
  free(rgb);
}

static void cmd_loadtape(int fd, struct machine *oric, const char *args)
{
  if(!args || !args[0])
  {
    send_err(fd, "LOADTAPE requires a file path");
    return;
  }

  if(tape_load_tap(oric, (char *)args))
    send_ok(fd);
  else
    send_err(fd, "Failed to load tape");
}

static void cmd_reset(int fd, struct machine *oric)
{
  swapmach(oric, NULL, (0xffff << 16) | oric->type);
  send_ok(fd);
}

static void cmd_readmem(int fd, struct machine *oric, const char *args)
{
  unsigned int addr, len, i;
  char hex[4];

  if(!args || sscanf(args, "%u %u", &addr, &len) != 2)
  {
    send_err(fd, "READMEM requires <addr> <len>");
    return;
  }

  if(addr + len > oric->memsize)
  {
    send_err(fd, "Address out of range");
    return;
  }

  if(len > 65536) len = 65536;

  send_str(fd, "OK\n");
  for(i = 0; i < len; i++)
  {
    sprintf(hex, "%02X", oric->mem[addr + i]);
    send_str(fd, hex);
  }
  send_str(fd, "\nEND\n");
}

static void cmd_getstate(int fd, struct machine *oric)
{
  char buf[256];
  const char *mode_str;
  const char *mach_str;

  switch(oric->emu_mode)
  {
    case EM_PAUSED:     mode_str = "paused"; break;
    case EM_RUNNING:    mode_str = "running"; break;
    case EM_DEBUG:      mode_str = "debug"; break;
    case EM_MENU:       mode_str = "menu"; break;
    case EM_PLEASEQUIT: mode_str = "quitting"; break;
    default:            mode_str = "unknown"; break;
  }

  switch(oric->type)
  {
    case MACH_ORIC1:    mach_str = "oric1"; break;
    case MACH_ORIC1_16K: mach_str = "oric1-16k"; break;
    case MACH_ATMOS:    mach_str = "atmos"; break;
    case MACH_TELESTRAT: mach_str = "telestrat"; break;
    case MACH_PRAVETZ:  mach_str = "pravetz"; break;
    default:            mach_str = "unknown"; break;
  }

  sprintf(buf, "OK\n"
    "emu_mode=%s\n"
    "machine=%s\n"
    "vid_mode=%s\n"
    "tape_loaded=%s\n"
    "tape_motor=%s\n"
    "END\n",
    mode_str,
    mach_str,
    (oric->vid_addr == oric->vidbases[2]) ? "text" : "hires",
    (oric->tapebuf != NULL) ? "yes" : "no",
    oric->tapemotor ? "on" : "off");

  send_str(fd, buf);
}

/* Command dispatcher */
static void handle_command(struct machine *oric, int fd, char *line)
{
  char *args;

  /* Split command and arguments */
  args = strchr(line, ' ');
  if(args)
  {
    *args = '\0';
    args++;
    /* Trim trailing whitespace/newline */
    int len = strlen(args);
    while(len > 0 && (args[len-1] == '\n' || args[len-1] == '\r' || args[len-1] == ' '))
      args[--len] = '\0';
  }

  if(strcasecmp(line, "QUEUEKEYS") == 0)
    cmd_queuekeys(fd, args);
  else if(strcasecmp(line, "PASTE") == 0)
    cmd_paste(fd, args);
  else if(strcasecmp(line, "READSCREEN") == 0)
    cmd_readscreen(fd, oric);
  else if(strcasecmp(line, "SCREENSHOT") == 0)
    cmd_screenshot(fd, oric);
  else if(strcasecmp(line, "LOADTAPE") == 0)
    cmd_loadtape(fd, oric, args);
  else if(strcasecmp(line, "RESET") == 0)
    cmd_reset(fd, oric);
  else if(strcasecmp(line, "READMEM") == 0)
    cmd_readmem(fd, oric, args);
  else if(strcasecmp(line, "GETSTATE") == 0)
    cmd_getstate(fd, oric);
  else if(strcasecmp(line, "PING") == 0)
    send_ok(fd);
  else
    send_err(fd, "Unknown command");
}

/* Public API */

SDL_bool remote_control_init(const char *path)
{
  struct sockaddr_un addr;

  strncpy(socket_path_copy, path, sizeof(socket_path_copy) - 1);
  socket_path_copy[sizeof(socket_path_copy) - 1] = '\0';

  /* Remove stale socket */
  unlink(path);

  listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if(listen_fd < 0)
  {
    fprintf(stderr, "MCP: Failed to create socket: %s\n", strerror(errno));
    return SDL_FALSE;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  if(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
  {
    fprintf(stderr, "MCP: Failed to bind socket: %s\n", strerror(errno));
    close(listen_fd);
    listen_fd = -1;
    return SDL_FALSE;
  }

  if(listen(listen_fd, 1) < 0)
  {
    fprintf(stderr, "MCP: Failed to listen: %s\n", strerror(errno));
    close(listen_fd);
    unlink(path);
    listen_fd = -1;
    return SDL_FALSE;
  }

  /* Set non-blocking */
  fcntl(listen_fd, F_SETFL, O_NONBLOCK);

  printf("MCP: Listening on %s\n", path);
  return SDL_TRUE;
}

void remote_control_poll(struct machine *oric, SDL_bool *needrender)
{
  if(listen_fd < 0) return;

  /* Accept new connection */
  if(client_fd < 0)
  {
    client_fd = accept(listen_fd, NULL, NULL);
    if(client_fd >= 0)
    {
      fcntl(client_fd, F_SETFL, O_NONBLOCK);
      recv_buf_len = 0;
      printf("MCP: Client connected\n");
    }
  }

  if(client_fd < 0) return;

  /* Read available data */
  while(recv_buf_len < RECV_BUF_SIZE - 1)
  {
    int n = recv(client_fd, recv_buf + recv_buf_len, RECV_BUF_SIZE - 1 - recv_buf_len, 0);
    if(n > 0)
    {
      recv_buf_len += n;
    }
    else if(n == 0)
    {
      /* Client disconnected */
      printf("MCP: Client disconnected\n");
      close(client_fd);
      client_fd = -1;
      recv_buf_len = 0;
      return;
    }
    else
    {
      /* EAGAIN/EWOULDBLOCK = no more data */
      break;
    }
  }

  /* Process complete lines */
  while(recv_buf_len > 0)
  {
    char *newline = memchr(recv_buf, '\n', recv_buf_len);
    if(!newline) break;

    *newline = '\0';
    int line_len = newline - recv_buf;

    /* Strip \r if present */
    if(line_len > 0 && recv_buf[line_len - 1] == '\r')
      recv_buf[line_len - 1] = '\0';

    if(recv_buf[0] != '\0')
      handle_command(oric, client_fd, recv_buf);

    /* Shift remaining data */
    int remaining = recv_buf_len - (line_len + 1);
    if(remaining > 0)
      memmove(recv_buf, newline + 1, remaining);
    recv_buf_len = remaining;

    /* Check if client was closed during command handling */
    if(client_fd < 0) return;
  }

  (void)needrender;
}

void remote_control_shutdown(void)
{
  if(client_fd >= 0)
  {
    close(client_fd);
    client_fd = -1;
  }
  if(listen_fd >= 0)
  {
    close(listen_fd);
    listen_fd = -1;
  }
  if(socket_path_copy[0])
  {
    unlink(socket_path_copy);
    socket_path_copy[0] = '\0';
  }
  printf("MCP: Shut down\n");
}

#else /* _WIN32 or WWW */

#include "system.h"
#include "6502.h"
#include "via.h"
#include "8912.h"
#include "gui.h"
#include "disk.h"
#include "monitor.h"
#include "6551.h"
#include "machine.h"
#include "remote_control.h"

SDL_bool remote_control_init(const char *path)
{
  (void)path;
  fprintf(stderr, "MCP: Remote control not available on this platform\n");
  return SDL_FALSE;
}

void remote_control_poll(struct machine *oric, SDL_bool *needrender)
{
  (void)oric;
  (void)needrender;
}

void remote_control_shutdown(void)
{
}

#endif
