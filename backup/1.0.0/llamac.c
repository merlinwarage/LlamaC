/*
 * llamac.c - native Win32 / pure C client for llama-server (llama.cpp WebUI clone)
 * Build (x64 Native Tools Command Prompt for VS 2022):   cl /O2 /W3 /utf-8 llamac.c
 */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

/* Unity build: the modules below are included in dependency order and share one translation unit. */
#include "src/util.h"   /* String builder and string helpers (portable) */
#include "src/json.h"   /* Tiny JSON DOM parser (portable) */
#include "src/config.h"   /* Settings struct, field table, serialisation (portable) */
#include "src/chat_tree.h"   /* Conversation tree: nodes, attachments, paths (portable) */
#include "src/request.h"   /* llama-server request body builder (portable) */
#include "src/chat_io.h"   /* WebUI-compatible chat export/import (portable) */
#include "src/win_base.h"   /* Windows includes, link pragmas, IDs, global UI state */
#include "src/win_util.h"   /* Win32 helpers: UTF-8/UTF-16, file IO, base64 */
#include "src/theme.h"   /* Theme colours, DWM frame, owner-drawn buttons/list */
#include "src/richtext.h"   /* RichEdit output primitives */
#include "src/highlight.h"   /* Syntax highlighter and language tables */
#include "src/codeblocks.h"   /* Code blocks with Copy/Download links, link hit-testing */
#include "src/markdown.h"   /* Markdown-ish inline/table/block renderer */
#include "src/render.h"   /* Thinking blocks, streaming flush, render_all */
#include "src/storage.h"   /* Data dir, settings and chat persistence */
#include "src/conv_list.h"   /* Sidebar conversation list */
#include "src/attachments.h"   /* Pending attachments and file loading */
#include "src/http.h"   /* WinHTTP URL parsing and requests */
#include "src/worker.h"   /* Streaming worker thread (SSE) */
#include "src/text_window.h"   /* Read-only text window (Jinja template view) */
#include "src/actions.h"   /* Chat actions: send, regenerate, edit, delete, branch */
#include "src/settings_ui.h"   /* Settings dialog */
#include "src/main_window.h"
