/*
 * llamac.c - native Win32 / pure C client for llama-server (llama.cpp WebUI clone)
 * Build (x64 Native Tools Command Prompt for VS 2022):   build.bat   (or: cl /O2 /W3 /utf-8 llamac.c)
 */
#define LLAMAC_VERSION L"1.6.0"
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <wchar.h>

#include "src/util.h"
#include "src/json.h"
#include "src/config.h"
#include "src/chat_tree.h"
#include "src/regex.h"   /* portable: regex + glob for the grep / glob tools */
#include "src/textops.h" /* portable: line numbers, edit snippets, grep core */
#include "src/tools_core.h" /* only needs SB and J, used by request.h and worker.h */
#include "src/request.h"
#include "src/chat_io.h"
#include "src/win_base.h"
#include "src/win_util.h"
#include "src/theme.h"
#include "src/richtext.h"
#include "src/highlight.h"
#include "src/codeblocks.h"
#include "src/markdown.h"
#include "src/render.h"
#include "src/storage.h"
#include "src/conv_list.h"
#include "src/attachments.h"
#include "src/http.h"
#include "src/worker.h"
#include "src/text_window.h"
#include "src/actions.h"
#include "src/tools.h" /* tool registry, approval dialog, runner thread */
#include "src/memory.h"
#include "src/compact.h"
#include "src/mcp.h"
#include "src/mcp_stdio.h"
#include "src/mcp_http.h"
#include "src/tools_fs.h"
#include "src/settings_ui.h" /* after the tool modules */
#include "src/main_window.h"
