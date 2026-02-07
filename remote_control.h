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

#ifndef __REMOTE_CONTROL_H__
#define __REMOTE_CONTROL_H__

#include "system.h"

struct machine;

SDL_bool remote_control_init(const char *socket_path);
void remote_control_poll(struct machine *oric, SDL_bool *needrender);
void remote_control_shutdown(void);

#endif /* __REMOTE_CONTROL_H__ */
