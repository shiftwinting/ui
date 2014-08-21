// 17 july 2014

#include "winapi_windows.h"
#include "_cgo_export.h"
#include "modalqueue.h"

// note that this includes the terminating '\0'
// this also assumes WC_TABCONTROL is longer than areaWindowClass
#define NCLASSNAME (sizeof WC_TABCONTROL / sizeof WC_TABCONTROL[0])

void uimsgloop_area(MSG *msg)
{
	// don't call TranslateMessage(); we do our own keyboard handling
	DispatchMessage(msg);
}

void uimsgloop_tab(HWND active, HWND focus, MSG *msg)
{
	BOOL hasChildren;
	BOOL idm;

	// THIS BIT IS IMPORTANT: if the current tab has no children, then there will be no children left in the dialog to tab to, and IsDialogMessageW() will loop forever
	hasChildren = SendMessageW(focus, msgTabCurrentTabHasChildren, 0, 0);
	if (hasChildren)
		tabEnterChildren(focus);
	idm = IsDialogMessageW(active, msg);
	if (hasChildren)
		tabLeaveChildren(focus);
	if (idm != 0)
		return;
	TranslateMessage(msg);
	DispatchMessage(msg);
}

void uimsgloop_else(MSG *msg)
{
	TranslateMessage(msg);
	DispatchMessage(msg);
}

void uimsgloop(void)
{
	MSG msg;
	int res;
	HWND active, focus;
	WCHAR classchk[NCLASSNAME];
	BOOL dodlgmessage;

	for (;;) {
		SetLastError(0);
		res = GetMessageW(&msg, NULL, 0, 0);
		if (res < 0)
			xpanic("error calling GetMessage()", GetLastError());
		if (res == 0)		// WM_QUIT
			break;
		active = GetActiveWindow();
		if (active == NULL) {
			uimsgloop_else(&msg);
			continue;
		}

		// bit of logic involved here:
		// we don't want dialog messages passed into Areas, so we don't call IsDialogMessageW() there
		// as for Tabs, we can't have both WS_TABSTOP and WS_EX_CONTROLPARENT set at the same time, so we hotswap the two styles to get the behavior we want
		// theoretically we could use the class atom to avoid a wcscmp()
		// however, raymond chen advises against this - http://blogs.msdn.com/b/oldnewthing/archive/2004/10/11/240744.aspx (and we're not in control of the Tab class, before you say anything)
		// we could also theoretically just send msgAreaDefocuses directly, but what DefWindowProc() does to a WM_APP message is undocumented
		focus = GetFocus();
		if (focus != NULL) {
			if (GetClassNameW(focus, classchk, NCLASSNAME) == 0)
				xpanic("error getting name of focused window class for Area check", GetLastError());
			if (wcscmp(classchk, areaWindowClass) == 0) {
				uimsgloop_area(&msg);
				continue;
			} else if (wcscmp(classchk, WC_TABCONTROL) == 0) {
				uimsgloop_tab(active, focus, &msg);
				continue;
			}
			// else fall through
		}

		if (IsDialogMessage(active, &msg) != 0)
			continue;
		uimsgloop_else(&msg);
	}
}

void issue(void *request)
{
	SetLastError(0);
	if (PostMessageW(msgwin, msgRequest, 0, (LPARAM) request) == 0)
		xpanic("error issuing request", GetLastError());
}

HWND msgwin;

#define msgwinclass L"gouimsgwin"

static BOOL CALLBACK beginEndModalAll(HWND hwnd, LPARAM lParam)
{
	if (hwnd != msgwin)
		SendMessageW(hwnd, (UINT) lParam, 0, 0);
	return TRUE;
}

static LRESULT CALLBACK msgwinproc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT shared;
	size_t i;

	if (sharedWndProc(hwnd, uMsg, wParam, lParam, &shared))
		return shared;
	switch (uMsg) {
	case WM_CREATE:
		// initial
		makeCheckboxImageList(hwnd);
		return 0;
	// TODO respond to WM_THEMECHANGED
	case msgRequest:
		// in modal?
		if (!queueIfModal((void *) lParam))
			// nope, we can run now
			doissue((void *) lParam);
		return 0;
	case msgBeginModal:
		beginModal();
		EnumThreadWindows(GetCurrentThreadId(), beginEndModalAll, msgBeginModal);
		return 0;
	case msgEndModal:
		endModal();
		EnumThreadWindows(GetCurrentThreadId(), beginEndModalAll, msgEndModal);
		return 0;
	default:
		return DefWindowProcW(hwnd, uMsg, wParam, lParam);
	}
	xmissedmsg("message-only", "msgwinproc()", uMsg);
	return 0;		// unreachable
}

DWORD makemsgwin(char **errmsg)
{
	WNDCLASSW wc;

	ZeroMemory(&wc, sizeof (WNDCLASSW));
	wc.lpfnWndProc = msgwinproc;
	wc.hInstance = hInstance;
	wc.hIcon = hDefaultIcon;
	wc.hCursor = hArrowCursor;
	wc.hbrBackground = (HBRUSH) (COLOR_BTNFACE + 1);
	wc.lpszClassName = msgwinclass;
	if (RegisterClassW(&wc) == 0) {
		*errmsg = "error registering message-only window classs";
		return GetLastError();
	}
	msgwin = CreateWindowExW(
		0,
		msgwinclass, L"package ui message-only window",
		0,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		HWND_MESSAGE, NULL, hInstance, NULL);
	if (msgwin == NULL) {
		*errmsg = "error creating message-only window";
		return GetLastError();
	}
	return 0;
}
