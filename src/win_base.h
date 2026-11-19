/* win_base.h - Windows includes, link pragmas, IDs, global UI state */
#ifndef LLAMAC_WIN_BASE_H
#define LLAMAC_WIN_BASE_H

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS /* C-style COM calls (WIC, used to paste clipboard images as PNG) */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <winhttp.h>
#include <commdlg.h>
#include <shellapi.h>
#include <richedit.h>
#include <wincrypt.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <mmsystem.h>
#include <objbase.h>
#include <wincodec.h>
#include <shobjidl.h> /* folder picker (IFileOpenDialog) in Settings */

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
/* /MANIFESTUAC is not allowed in a #pragma (LNK4229) - build.bat passes it, with /MANIFEST:EMBED */

#define BULLET "\xE2\x80\xA2"
#define HBAR "\xE2\x94\x80"
#define VBAR "\xE2\x94\x82"
#define CROSS "\xE2\x94\xBC"
enum
{
    IDC_SEARCH = 101,
    IDC_LIST,
    IDC_CHAT,
    IDC_INPUT,
    IDC_STATUS,
    IDC_MODEL,
    IDC_NEW = 110,
    IDC_DELC,
    IDC_EXP,
    IDC_IMP,
    IDC_SET,
    IDC_REFRESH,
    IDC_ATTACH = 120,
    IDC_REGEN,
    IDC_EDIT,
    IDC_DELM,
    IDC_PREV,
    IDC_NEXT,
    IDC_COPY,
    IDC_SEND,
    IDC_FIND = 130,
    IDC_SIDEBAR,
    IDC_TEMPLATE,
    IDC_RESET,
    IDC_MCPEDIT,
    IDC_MCPRELOAD,
    IDC_SYSLIST,
    IDC_SYSSAVE,
    IDC_SYSDEL,
    IDC_EDITREPLY,
    IDC_QUEUE,
    IDC_CONTEXT,
    IDC_TOOLSET,
    IDC_BROWSE_FS,
    IDC_BROWSE_KV,
    IDC_MCPBTN,
    IDC_TOOLSBTN,
    IDC_TOOLSDLG,
    IDC_THINKBTN, /* 148 */
    IDC_CP_COPY,  /* copilot panel header: copy the last reply, new chat, open the main window, hide */
    IDC_CP_NEW,
    IDC_CP_OPEN,
    IDC_CP_HIDE
};
#define WM_APP_MSG (WM_APP + 1)
#define WM_APP_INIT (WM_APP + 2)
/* WM_APP + 3 = WM_APP_ASK (tools_extra.h) */
#define WM_APP_TRAY (WM_APP + 4) /* tray icon callback (copilot mode) */

typedef struct
{
    int kind;
    char *text;
} Msg; /* 0 content,1 reasoning,2 stats,3 error,4 done,5 tool_calls,6 tool result,8 tools finished,9 status */

static HWND hMain, hSearch, hList, hChat, hInput, hStatus, hModel, hHot;
static HFONT hFont, hFontB, hFontT;
static HFONT hFontS;   /* 13 px: the actions row */
static HFONT hFontI;   /* 17 px semibold: composer glyphs (+, T, M) */
static HFONT hFontXS;  /* 10 px semibold: count badges */
static HFONT hFontSym; /* Segoe UI Symbol: arrows and the stop square */
static HACCEL hAcc;
static HBRUSH hbrBg, hbrPanel, hbrSurf;
static COLORREF cBg, cPanel, cSurf, cBorder, cFg, cDim, cAccent, cUser, cAsst, cCode, cCodeBg, cErr = RGB(248, 113, 113);
static COLORREF cKw, cStr, cCom, cNum, cFn, cType, cCodeHdr;
static Cfg G;
static char srvdef[NFIELDS][32];
static char *g_tmpl;
static Conv **convs;
static int nconv;
static Conv *cur;
static int *lmap;
static int nmap;
static Att pend[16];
static int npend;
static volatile LONG busy, g_stop;
static volatile HINTERNET g_req;
static Conv *genConv;
static int genNode, genPhase;
static wchar_t dataDir[MAX_PATH];
static int sbVisible = 1, g_colw = 800;
static RECT rcSide, rcSearch, rcHead, rcBox, rcTitle;
static RECT rcGauge; /* context gauge right of the status line (click: context window) */
static RECT rcTools; /* composer: the + / T / M button group */
static int g_ydock;  /* y of the divider between the chat and the bottom dock */
static int g_actdiv[2]; /* x of the dividers in the actions row */
static int g_actdiv_y;
static HWND g_mtt;   /* tooltips of the main window's buttons */
static WNDPROC oldInput;
static const wchar_t *FACE = L"Segoe UI", *MONO = L"Consolas";
static int g_nctx;   /* context window from /props, 0 = unknown */
static int g_vision; /* the model accepts images (/props modalities) */
static int g_nslots; /* server slots (/props total_slots), 0 = unknown */
static int g_last_prompt_n, g_last_pred_n;
static int g_compacting;
#define DOCK_H 48           /* composer row: buttons, input (1 line) and Send are this tall */
static int g_boxH = DOCK_H; /* input box height: grows with the text, up to 6 lines */
static int g_lineH = 20;
static HWND hPanel; /* copilot panel (copilot.h); NULL until first shown */
static HICON g_icon, g_icon_sm; /* app icon (resource 1, res/llamac.rc), big and small */
static int g_cp_on; /* the composer and the chat view are hosted by the copilot panel, not the main window */
#define PT 10

#endif /* LLAMAC_WIN_BASE_H */
