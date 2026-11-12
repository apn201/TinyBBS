#pragma once
// The shared BBS core: everything a caller sees, on any transport and any
// machine. Both builds include this one file after src/platform/platform.h:
// the ESP32 firmware (src/main.cpp) and the server (server/, see
// docs/APACHE_PORT.md). Order matters: each module may use anything
// included before it.
//
// Nothing in here may touch hardware directly. Use the platform calls listed
// in src/platform/platform.h; transports (web, Telnet) and hardware (LCD,
// WiFi) live outside the core and call into it.
#include "bbs/cp437.h"
#include "core/globals.h"
#include "core/util.h"
#include "core/storage.h"
#include "core/ini.h"
#include "core/clock.h"
#include "core/users.h"
#include "core/messagebase.h"
#include "core/history.h"
#include "core/session.h"
#include "core/stream.h"
#include "core/reader.h"
#include "bbs/settings.h"
#include "bbs/menus.h"
#include "bbs/files.h"
#include "bbs/zip.h"
#include "bbs/viewers.h"
#include "doors/volamud.h"
#include "doors/legend.h"
#include "doors/dice.h"
#include "doors/doors.h"
#include "bbs/sysop.h"
#include "bbs/board.h"
#include "bbs/chat.h"
#include "bbs/messages.h"
#include "bbs/admin.h"
#include "bbs/uploads.h"
#include "bbs/cmd_login.h"
#include "bbs/cmd_main.h"
#include "bbs/cmd_sysop.h"
#include "bbs/user_editor.h"
#include "bbs/cmd_files.h"
#include "bbs/cmd_messages.h"
#include "bbs/cmd_doors.h"
#include "bbs/commands.h"
#include "bbs/runtime.h"
