/* win_base.h - Windows includes, link pragmas, IDs, global UI state */
#ifndef LLAMAC_WIN_BASE_H
#define LLAMAC_WIN_BASE_H

#define WIN32_LEAN_AND_MEAN
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
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define BULLET "\xE2\x80\xA2"
#define HBAR "\xE2\x94\x80"
#define VBAR "\xE2\x94\x82"
#define CROSS "\xE2\x94\xBC"
enum { IDC_SEARCH = 101, IDC_LIST, IDC_CHAT, IDC_INPUT, IDC_STATUS, IDC_MODEL,
       IDC_NEW = 110, IDC_DELC, IDC_EXP, IDC_IMP, IDC_SET, IDC_REFRESH,
       IDC_ATTACH = 120, IDC_REGEN, IDC_EDIT, IDC_DELM, IDC_PREV, IDC_NEXT, IDC_COPY, IDC_SEND,
       IDC_FIND = 130, IDC_SIDEBAR, IDC_TEMPLATE, IDC_RESET };
#define WM_APP_MSG (WM_APP + 1)
#define WM_APP_INIT (WM_APP + 2)

typedef struct { int kind; char *text; } Msg; /* 0 content,1 reasoning,2 stats,3 error,4 done */

static HWND hMain, hSearch, hList, hChat, hInput, hStatus, hModel, hHot;
static HFONT hFont, hFontB, hFontT;
static HACCEL hAcc;
static HBRUSH hbrBg, hbrPanel, hbrSurf;
static COLORREF cBg, cPanel, cSurf, cBorder, cFg, cDim, cAccent, cUser, cAsst, cCode, cCodeBg, cErr = RGB(248, 113, 113);
static COLORREF cKw, cStr, cCom, cNum, cFn, cType, cCodeHdr;
static Cfg G;
static char srvdef[NFIELDS][32];
static char *g_tmpl;
static Conv **convs; static int nconv; static Conv *cur;
static int *lmap; static int nmap;
static Att pend[16]; static int npend;
static volatile LONG busy, g_stop;
static volatile HINTERNET g_req;
static Conv *genConv; static int genNode, genPhase;
static wchar_t dataDir[MAX_PATH];
static int sbVisible = 1, g_colw = 800;
static RECT rcSide, rcSearch, rcHead, rcBox, rcTitle;
static WNDPROC oldInput;
static const wchar_t *FACE = L"Segoe UI", *MONO = L"Consolas";
#define PT 10

#endif /* LLAMAC_WIN_BASE_H */
