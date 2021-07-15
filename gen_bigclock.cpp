/**
 * Extended by Sebastian Pipping <webmaster@hartwork.org>
 */

/**
 * NxS BigClock Winamp Plugin for Winamp 2.x/5.x
 * Copyright (C) 2004 Nicolai Syvertsen <saivert@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/* NxS BigClock plugin for Winamp 2.x/5.x
 * Author: Saivert
 * Homepage: http://saivert.com/
 * E-Mail: saivert@gmail.com
 *
 * BUILD NOTES:
 *  Before building this project, please check the post-build step in the project settings.
 *  You must make sure the path that the file is copied to is referring to where you have your
 *  Winamp plugins directory.
 */

//#define USE_COMCTL_DRAWSHADOWTEXT

#define _WIN32_WINNT 0x0501
#include "windows.h"
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <strsafe.h>

#include "gen.h"
#include "wa_ipc.h"
#include "wa_msgids.h"
#include <../gen_ml/ml.h>
#include <../gen_ml/ml_ipc_0313.h>
#include <../nu/servicebuilder.h>
#include "wa_hotkeys.h"
#define WA_DLG_IMPLEMENT
#include "wa_dlg.h"
#include "embedwnd.h"
#include "api.h"

#include "resource.h"

/* global data */
static const wchar_t szAppName[] = L"NxS BigClock";
#define PLUGIN_INISECTION szAppName
#define PLUGIN_VERSION "1.03"

/* Metrics
   Note: Sizes must be in increments of 25x29
*/
#define WND_HEIGHT      116
#define WND_WIDTH       275

// Menu ID's
UINT WINAMP_NXS_BIG_CLOCK_MENUID = (ID_GENFF_LIMIT+101);

// Display mode constants
#define NXSBCDM_DISABLED 0
#define NXSBCDM_ELAPSEDTIME 1
#define NXSBCDM_REMAININGTIME 2
#define NXSBCDM_PLELAPSED 3
#define NXSBCDM_PLREMAINING 4
#define NXSBCDM_TIMEOFDAY 5
#define NXSBCDM_MAX 5

/* BigClock window */
static HWND g_BigClockWnd;
static wchar_t g_BigClockClassName[] = L"NxSBigClockWnd",
			   pluginTitleW[256] = {0};
typedef HWND (*embedWindow_t)(embedWindowState *);
LRESULT CALLBACK BigClockWndProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
static HMENU g_hPopupMenu = 0;
static LPARAM ipc_bigclockinit = -1;
wchar_t *ini_file = 0;
static genHotkeysAddStruct genhotkey = {0};
static ATOM wndclass = 0;
static HWND hWndBigClock = NULL;
static embedWindowState embed = {0};
static int no_uninstall = 1;
static HBRUSH hbrBkGnd = NULL;
static HPEN hpenVis = NULL;
static HFONT hfDisplay = NULL, hfMode = NULL;
static LOGFONT lfDisplay = {0}, lfMode = {0};

void DrawVisualization(HDC hdc, RECT r);
void DrawAnalyzer(HDC hdc, RECT r, char *sadata);

BOOL GetFormattedTime(LPTSTR lpszTime, UINT size, int iPos);

DWORD_PTR CALLBACK ConfigDlgProc(HWND,UINT,WPARAM,LPARAM);

/* subclass of Winamp's main window */
WNDPROC lpWinampWndProcOld;
LRESULT CALLBACK WinampSubclass(HWND,UINT,WPARAM,LPARAM);

/* subclass of skinned frame window (GenWnd) */
LRESULT CALLBACK GenWndSubclass(HWND,UINT,WPARAM,LPARAM);
WNDPROC lpGenWndProcOld;

/* Menu item functions */
void InsertMenuItemInWinamp();
void RemoveMenuItemFromWinamp();

#define NXSBCVM_OSC 1
#define NXSBCVM_SPEC 2

/* configuration items */
static int config_shadowtext=TRUE;
static int config_showdisplaymode=TRUE;
static int config_vismode=TRUE;
static int config_displaymode=NXSBCDM_ELAPSEDTIME;
static int config_centi=1;
static int config_freeze=0;

/* plugin function prototypes */
void config(void);
void quit(void);
int init(void);

winampGeneralPurposePlugin plugin = { GPPHDR_VER_U, "", init, config, quit, 0, 0 };

api_service* WASABI_API_SVC = NULL;
api_application *WASABI_API_APP = NULL;
api_language *WASABI_API_LNG = NULL;
// these two must be declared as they're used by the language api's
// when the system is comparing/loading the different resources
HINSTANCE WASABI_API_LNG_HINST = NULL, WASABI_API_ORIG_HINST = NULL;


// this is used to identify the skinned frame to allow for embedding/control by modern skins if needed
// {DF6F9C93-155C-4d01-BF5A-17612EDBFC4C}
static const GUID embed_guid = 
{ 0xdf6f9c93, 0x155c, 0x4d01, { 0xbf, 0x5a, 0x17, 0x61, 0x2e, 0xdb, 0xfc, 0x4c } };


/* Macros to read a value from an INI file with the same name as the varible itself */
#define INI_READ_INT(x) x = GetPrivateProfileInt(PLUGIN_INISECTION, L#x, x, ini_file);
/* Macros to write the value of a variable to an INI key with the same name as the variable itself */
#define INI_WRITE_INT(x) WritePrivateProfileInt(PLUGIN_INISECTION, L#x, x, ini_file);

LRESULT sendMlIpc(int msg, WPARAM param)
{
	static LRESULT IPC_GETMLWINDOW;
	if (!IPC_GETMLWINDOW)
	{
		IPC_GETMLWINDOW = SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&"LibraryGetWnd", IPC_REGISTER_WINAMP_IPCMESSAGE);
	}
	HWND mlwnd = (HWND)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETMLWINDOW);

	if ((param == 0) && (msg == 0))
	{
		return (LRESULT)mlwnd;
	}

	if (IsWindow(mlwnd))
	{
		return SendMessage(mlwnd, WM_ML_IPC, param, msg);
	}

	return 0;
}


BOOL Menu_TrackPopup(HMENU hMenu, UINT fuFlags, int x, int y, HWND hwnd)
{
	if (hMenu == NULL)
	{
		return NULL;
	}

	if (IsWindow((HWND)sendMlIpc(0, 0)))
	{
		MLSKINNEDPOPUP popup = {sizeof(MLSKINNEDPOPUP)};
		popup.hmenu = hMenu;
		popup.fuFlags = fuFlags;
		popup.x = x;
		popup.y = y;
		popup.hwnd = hwnd;
		popup.skinStyle = SMS_USESKINFONT;
		return (INT)sendMlIpc(ML_IPC_TRACKSKINNEDPOPUPEX, (WPARAM)&popup);
	}
	return TrackPopupMenu(hMenu, fuFlags, x, y, 0, hwnd, NULL);
}


void UpdateSkinParts() {
	WADlg_init(plugin.hwndParent);

	if (hbrBkGnd) {
		DeleteObject(hbrBkGnd);
	}
	hbrBkGnd = CreateSolidBrush(WADlg_getColor(WADLG_ITEMBG));

	if (hpenVis) {
		DeleteObject(hpenVis);
	}
	hpenVis = CreatePen(PS_SOLID, 2, WADlg_getColor(WADLG_ITEMFG));

	if (hfDisplay) {
		DeleteObject(hfDisplay);
	}

	hfDisplay = CreateFontIndirect(&lfDisplay);

	if (hfMode) {
		DeleteObject(hfMode);
	}
	hfMode = CreateFontIndirect(&lfMode);
}


bool ProcessMenuResult(UINT command, HWND parent) {
	switch (LOWORD(command)) {
		case ID_CONTEXTMENU_DISABLED:
		case ID_CONTEXTMENU_ELAPSED:
		case ID_CONTEXTMENU_REMAINING:
		case ID_CONTEXTMENU_PLAYLISTELAPSED:
		case ID_CONTEXTMENU_PLAYLISTREMAINING:
		case ID_CONTEXTMENU_TIMEOFDAY:
			config_displaymode = LOWORD(command)-ID_CONTEXTMENU_DISABLED;
			INI_WRITE_INT(config_displaymode);
			break;
		case ID_CONTEXTMENU_SHOWDISPLAYMODE:
			config_showdisplaymode = !config_showdisplaymode;
			INI_WRITE_INT(config_showdisplaymode);
			break;
		case ID_CONTEXTMENU_SHADOWEDTEXT:
			config_shadowtext = !config_shadowtext;
			INI_WRITE_INT(config_shadowtext);
			break;
		case ID_CONTEXTMENU_SHOWOSC:
			config_vismode ^= NXSBCVM_OSC;
			INI_WRITE_INT(config_vismode);
			break;
		case ID_CONTEXTMENU_SHOWSPEC:
			config_vismode ^= NXSBCVM_SPEC;
			INI_WRITE_INT(config_vismode);
			break;
		case ID_CONTEXTMENU_CENTI:
			config_centi = !config_centi;
			INI_WRITE_INT(config_centi);
			break;
		case ID_CONTEXTMENU_RESETFONTS:
			{
				if (MessageBox(parent, WASABI_API_LNGSTRINGW(IDS_RESET_FONTS),
							   (LPWSTR)plugin.description, MB_YESNO) == IDNO) {
					break;
				}
			}
		case ID_CONTEXTMENU_MAINTEXTFONT:
		case ID_CONTEXTMENU_DISPLAYMODEFONT:
		{
			LOGFONT *lf = 0;
			int reset = (LOWORD(command) == ID_CONTEXTMENU_RESETFONTS), update = reset,
				mode = (LOWORD(command) == ID_CONTEXTMENU_DISPLAYMODEFONT);

			if (LOWORD(command) != ID_CONTEXTMENU_RESETFONTS) {
				CHOOSEFONT cf = {0};
				cf.lStructSize = sizeof(cf);
				cf.hwndOwner = parent;
				lf = cf.lpLogFont = (!mode ? &lfDisplay : &lfMode);
				cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
				update = ChooseFont(&cf);
			}

			if (update) {
				if (!reset) {
					HFONT *hf = (!mode ? &hfDisplay : &hfMode);
					if (*hf) {
						DeleteObject(*hf);
					}
					*hf = CreateFontIndirect(lf);
				} else {
					memset(&lfDisplay, 0, sizeof(lfDisplay));
					lfDisplay.lfHeight = -50;
					lstrcpyn(lfDisplay.lfFaceName, L"Arial", LF_FACESIZE);

					memset(&lfMode, 0, sizeof(lfMode));
					lfMode.lfHeight = -13;
					lstrcpyn(lfMode.lfFaceName, L"Arial", LF_FACESIZE);

					UpdateSkinParts();
				}

reparse:
				wchar_t buf[32] = {0};
				StringCchPrintf(buf, 32, L"%d", (!mode ? lfDisplay.lfHeight : lfMode.lfHeight));
				WritePrivateProfileString(PLUGIN_INISECTION, (!mode ? L"df_h" : L"mf_h"), buf, ini_file);

				StringCchPrintf(buf, 32, L"%d", (!mode ? lfDisplay.lfItalic : lfMode.lfItalic));
				WritePrivateProfileString(PLUGIN_INISECTION, (!mode ? L"df_i" : L"mf_i"), buf, ini_file);

				StringCchPrintf(buf, 32, L"%d", (!mode ? lfDisplay.lfWeight : lfMode.lfWeight));
				WritePrivateProfileString(PLUGIN_INISECTION, (!mode ? L"df_b" : L"mf_b"), buf, ini_file);

				WritePrivateProfileString(PLUGIN_INISECTION, (!mode ? L"df" : L"mf"),
										  (!mode ? lfDisplay.lfFaceName : lfMode.lfFaceName), ini_file);

				// dirty way to get main and display fonts correctly reset
				if (reset && !mode) {
					mode = 1;
					goto reparse;
				}
			}
			break;
		}
		case ID_CONTEXTMENU_FREEZE:
			config_freeze = !config_freeze;
			INI_WRITE_INT(config_freeze);
			break;
		case ID_CONTEXTMENU_ABOUT:
		{
			wchar_t message[2048] = {0};
			StringCchPrintf(message, ARRAYSIZE(message), WASABI_API_LNGSTRINGW(IDS_ABOUT_STRING), TEXT(__DATE__));
			MessageBox(plugin.hwndParent, message, (LPWSTR)plugin.description, 0);
			break;
		}
		default:
			return false;
	}
	return true;
}


void config() {
	HMENU hMenu = WASABI_API_LOADMENUW(IDR_CONTEXTMENU);
	HMENU popup = GetSubMenu(hMenu, 0);
	RECT r = {0};

	MENUITEMINFO i = {sizeof(i), MIIM_ID | MIIM_STATE | MIIM_TYPE, MFT_STRING, MFS_UNCHECKED | MFS_DISABLED, 1};
	i.dwTypeData = pluginTitleW;
	InsertMenuItem(popup, 0, TRUE, &i);

	i.fType = MFT_SEPARATOR;
	InsertMenuItem(popup, 1, TRUE, &i);

	HWND list =	FindWindowEx(GetParent(GetFocus()), 0, L"SysListView32", 0);
	ListView_GetItemRect(list, ListView_GetSelectionMark(list), &r, LVIR_BOUNDS);
	ClientToScreen(list, (LPPOINT)&r);

	ProcessMenuResult(TrackPopupMenu(popup, TPM_RETURNCMD | TPM_LEFTBUTTON, r.left, r.top, 0, list, NULL), list);
	DestroyMenu(hMenu);
}


void quit() {

	if (no_uninstall)
	{
		/* Update position and size */
		DestroyEmbeddedWindow(&embed);
	}

	if (IsWindow(hWndBigClock))
	{
		DestroyWindow(hWndBigClock);
		DestroyWindow(g_BigClockWnd); /* delete our window */
	}

	if (hbrBkGnd) {
		DeleteObject(hbrBkGnd);
		hbrBkGnd = NULL;
	}

	if (hpenVis) {
		DeleteObject(hpenVis);
		hpenVis = NULL;
	}

	if (hfDisplay) {
		DeleteObject(hfDisplay);
		hfDisplay = NULL;
	}

	if (hfMode) {
		DeleteObject(hfMode);
		hfMode = NULL;
	}

	UnregisterClass(g_BigClockClassName, plugin.hDllInstance);

	ServiceRelease(WASABI_API_SVC, WASABI_API_LNG, languageApiGUID);
	ServiceRelease(WASABI_API_SVC, WASABI_API_APP, applicationApiServiceGuid);

	if ((LONG)WinampSubclass == GetWindowLongPtr(plugin.hwndParent, GWLP_WNDPROC)) {
		SetWindowLongPtr(plugin.hwndParent, GWLP_WNDPROC, (LONG)lpWinampWndProcOld);
	}
}


int init() {
	/*WASABI_API_SVC = GetServiceAPIPtr();/*/
	// load all of the required wasabi services from the winamp client
	WASABI_API_SVC = reinterpret_cast<api_service*>(SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_API_SERVICE));
	if (WASABI_API_SVC == reinterpret_cast<api_service*>(1)) WASABI_API_SVC = NULL;/**/
	if (WASABI_API_SVC != NULL)
	{
		ServiceBuild(WASABI_API_SVC, WASABI_API_APP, applicationApiServiceGuid);
		ServiceBuild(WASABI_API_SVC, WASABI_API_LNG, languageApiGUID);
		// TODO add to lang.h
		WASABI_API_START_LANG(plugin.hDllInstance, embed_guid);

		StringCchPrintf(pluginTitleW, ARRAYSIZE(pluginTitleW), WASABI_API_LNGSTRINGW(IDS_PLUGIN_NAME), TEXT(PLUGIN_VERSION));
		plugin.description = (char*)pluginTitleW;

		WNDCLASSEX wcex = {0};
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.lpszClassName = g_BigClockClassName;
		wcex.hInstance = plugin.hDllInstance;
		wcex.lpfnWndProc = BigClockWndProc;
		wndclass = RegisterClassEx(&wcex);

		if (!wndclass) {
			MessageBox(plugin.hwndParent, L"Error: Could not register window class!", szAppName, MB_OK | MB_ICONERROR);
			return GEN_INIT_FAILURE;
		}

		/* Subclass Winamp's main window */
		lpWinampWndProcOld = (WNDPROC)SetWindowLongPtr(plugin.hwndParent, GWLP_WNDPROC, (LONG)WinampSubclass);

		// wParam must have something provided else it returns 0
		// and then acts like a IPC_GETVERSION call... not good!
		ipc_bigclockinit = SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&"NXSBC_IPC", IPC_REGISTER_WINAMP_IPCMESSAGE);
		PostMessage(plugin.hwndParent, WM_WA_IPC, 0, ipc_bigclockinit);

		return GEN_INIT_SUCCESS;
	}
	return GEN_INIT_FAILURE;
}


LRESULT CALLBACK WinampSubclass(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_WA_IPC:
		/* Init time */
		if (lParam==ipc_bigclockinit) {
			ini_file = (wchar_t *)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETINIFILEW);

			INI_READ_INT(config_showdisplaymode);
			INI_READ_INT(config_shadowtext);
			INI_READ_INT(config_vismode);
			INI_READ_INT(config_displaymode);
			INI_READ_INT(config_centi);
			INI_READ_INT(config_freeze);

			lfDisplay.lfHeight = GetPrivateProfileInt(PLUGIN_INISECTION, L"df_h", -50, ini_file);
			lfMode.lfHeight = GetPrivateProfileInt(PLUGIN_INISECTION, L"mf_h", -13, ini_file);

			lfDisplay.lfItalic = (BYTE)GetPrivateProfileInt(PLUGIN_INISECTION, L"df_i", 0, ini_file);
			lfMode.lfItalic = (BYTE)GetPrivateProfileInt(PLUGIN_INISECTION, L"mf_i", 0, ini_file);

			lfDisplay.lfWeight = GetPrivateProfileInt(PLUGIN_INISECTION, L"df_b", 0, ini_file);
			lfMode.lfWeight = GetPrivateProfileInt(PLUGIN_INISECTION, L"mf_b", 0, ini_file);

			GetPrivateProfileString(PLUGIN_INISECTION, L"df", L"Arial", lfDisplay.lfFaceName, LF_FACESIZE, ini_file);
			GetPrivateProfileString(PLUGIN_INISECTION, L"mf", L"Arial", lfMode.lfFaceName, LF_FACESIZE, ini_file);


			// for the purposes of this example we will manually create an accelerator table so
			// we can use IPC_REGISTER_LOWORD_COMMAND to get a unique id for the menu items we
			// will be adding into Winamp's menus. using this api will allocate an id which can
			// vary between Winamp revisions as it moves depending on the resources in Winamp.
			WINAMP_NXS_BIG_CLOCK_MENUID = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_REGISTER_LOWORD_COMMAND);

			// then we show the embedded window which will cause the child window to be
			// sized into the frame without having to do any thing ourselves. also this will
			// only show the window if Winamp was not minimised on close and the window was
			// open at the time otherwise it will remain hidden
			old_visible = visible = GetPrivateProfileInt(PLUGIN_INISECTION, L"config_show", TRUE, ini_file);

			// finally we add menu items to the main right-click menu and the views menu
			// with Modern skins which support showing the views menu for accessing windows
			AddEmbeddedWindowToMenus(TRUE, WINAMP_NXS_BIG_CLOCK_MENUID, WASABI_API_LNGSTRINGW(IDS_NXS_BIG_CLOCK), -1);

			// now we will attempt to create an embedded window which adds its own main menu entry
			// and related keyboard accelerator (like how the media library window is integrated)
			hWndBigClock = CreateEmbeddedWindow(&embed, embed_guid);

			/* Subclass skinned window frame */
			lpGenWndProcOld = (WNDPROC)SetWindowLong(hWndBigClock, GWL_WNDPROC, (LONG)GenWndSubclass);

			// once the window is created we can then specify the window title and menu integration
			SetWindowText(hWndBigClock, WASABI_API_LNGSTRINGW(IDS_NXS_BIG_CLOCK));

			g_BigClockWnd = CreateWindowEx(0, (LPCTSTR)wndclass, szAppName, WS_CHILD|WS_VISIBLE,
				0, 0, 0, 0, hWndBigClock, NULL, plugin.hDllInstance, NULL);

			// Winamp can report if it was started minimised which allows us to control our window
			// to not properly show on startup otherwise the window will appear incorrectly when it
			// is meant to remain hidden until Winamp is restored back into view correctly
			if ((SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_INITIAL_SHOW_STATE) == SW_SHOWMINIMIZED))
			{
				SetEmbeddedWindowMinimizedMode(hWndBigClock, TRUE);
			}
			else
			{
				// only show on startup if under a classic skin and was set
				if (visible)
				{
					ShowWindow(hWndBigClock, SW_SHOW);
				}
			}

			/* Get message value */
			UINT genhotkeys_add_ipc = SendMessage(plugin.hwndParent, WM_WA_IPC,
				(WPARAM)"GenHotkeysAdd", IPC_REGISTER_WINAMP_IPCMESSAGE);

			/* Set up the genHotkeysAddStruct */
			genhotkey.name = (char*)_wcsdup(WASABI_API_LNGSTRINGW(IDS_GHK_STRING));
			genhotkey.flags = HKF_NOSENDMSG | HKF_UNICODE_NAME;
			genhotkey.id = "NxSBigClockToggle";
			// get this to send a WM_COMMAND message so we don't have to do anything specific
			genhotkey.uMsg = WM_COMMAND;
			genhotkey.wParam = MAKEWPARAM(WINAMP_NXS_BIG_CLOCK_MENUID, 0);
			genhotkey.lParam = 0;
			genhotkey.wnd = g_BigClockWnd;
			PostMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&genhotkey, genhotkeys_add_ipc);
		}
		break;
	}

	// this will handle the message needed to be caught before the original window
	// proceedure of the subclass can process it. with multiple windows then this
	// would need to be duplicated for the number of embedded windows your handling
	HandleEmbeddedWindowWinampWindowMessages(hWndBigClock, WINAMP_NXS_BIG_CLOCK_MENUID,
											 &embed, TRUE, hwnd, uMsg, wParam, lParam);

	LRESULT ret = CallWindowProc(lpWinampWndProcOld, hwnd, uMsg, wParam, lParam);

	// this will handle the message needed to be caught after the original window
	// proceedure of the subclass can process it. with multiple windows then this
	// would need to be duplicated for the number of embedded windows your handling
	HandleEmbeddedWindowWinampWindowMessages(hWndBigClock, WINAMP_NXS_BIG_CLOCK_MENUID,
											 &embed, FALSE, hwnd, uMsg, wParam, lParam);

	return ret;
}


int GetFormattedTime(LPWSTR lpszTime, UINT size, int iPos) {

	wchar_t szFmt[] = L"%d:%.2d:%.2d\0";

	double time_s = (iPos > 0 ? iPos*0.001 : 0);
	int hours = (int)(time_s / 60 / 60) % 60;
	time_s -= (hours*60*60);
	int minutes = (int)(time_s/60);
	time_s -= (minutes*60);
	int seconds = (int)(time_s);
	time_s -= seconds;
	int dsec = (int)(time_s*100);

	if (hours > 0) {
		StringCchPrintf(lpszTime, size, szFmt, hours, minutes, seconds);
	} else {
		StringCchPrintf(lpszTime, size, szFmt+3, minutes, seconds);
	}

	if (config_centi) {
		wchar_t szMsFmt[] = L".%.2d";
		int offset = lstrlen(lpszTime);
		StringCchPrintf(lpszTime + offset, size - offset, szMsFmt, dsec);
	}

	return lstrlen(lpszTime);
}


LRESULT CALLBACK BigClockWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

	// if you need to do other message handling then you can just place this first and
	// process the messages you need to afterwards. note this is passing the frame and
	// its id so if you have a few embedded windows you need to do this with each child
	if (HandleEmbeddedWindowChildMessages(hWndBigClock, WINAMP_NXS_BIG_CLOCK_MENUID,
										  hWnd, uMsg, wParam, lParam))
	{
		return 0;
	}

	switch (uMsg) {
	case WM_CREATE:
		{
			UpdateSkinParts();
			SetTimer(hWnd, 1, 10, NULL);

			HACCEL accel = WASABI_API_LOADACCELERATORSW(IDR_ACCELERATOR_WND);
			if (accel) {
				WASABI_API_APP->app_addAccelerators(hWnd, &accel, 1, TRANSLATE_MODE_NORMAL);
			}
			return 1;
		}
	case WM_CLOSE:
		// prevent closing the skinned frame from destroying this window
		return 1;
	case WM_DESTROY:
		DestroyMenu(g_hPopupMenu);
		KillTimer(hWnd, 1);
		break;
	case WM_WA_IPC:
		// make sure we catch all appropriate skin changes
		if (lParam == IPC_SKIN_CHANGED ||
			lParam == IPC_CB_RESETFONT ||
			lParam == IPC_FF_ONCOLORTHEMECHANGED) {
			UpdateSkinParts();
		} else {
			break;
		}
	case WM_DISPLAYCHANGE:
		if (wParam==0 && lParam==0) {
			UpdateSkinParts();
		}
		break;
	case WM_LBUTTONUP:
		if (config_displaymode>=NXSBCDM_MAX)
			config_displaymode = 0;
		else
			++config_displaymode;
		INI_WRITE_INT(config_displaymode);
		return 0;
	case WM_COMMAND:	// for what's handled from the accel table
		if (ProcessMenuResult(wParam, hWnd))
		{
			break;
		}
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_CHAR:
	case WM_MOUSEWHEEL:
		PostMessage(plugin.hwndParent, uMsg, wParam, lParam);
		break;
	case WM_CONTEXTMENU:
		{
			if ((HWND)wParam!=hWnd) break;

			POINT pt = {0};
			if (LOWORD(lParam) == -1 && HIWORD(lParam) == -1) {
				pt.x = pt.y = 1;
				ClientToScreen(hWnd, &pt);
			} else {
				GetCursorPos(&pt);
			}

			if (!g_hPopupMenu) {
				g_hPopupMenu = GetSubMenu(WASABI_API_LOADMENUW(IDR_CONTEXTMENU), 0);
			}

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWDISPLAYMODE,
				MF_BYCOMMAND|(config_showdisplaymode?MF_CHECKED:MF_UNCHECKED));

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHADOWEDTEXT,
				MF_BYCOMMAND|(config_shadowtext?MF_CHECKED:MF_UNCHECKED));

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWOSC,
				MF_BYCOMMAND|((config_vismode&NXSBCVM_OSC)==NXSBCVM_OSC?MF_CHECKED:MF_UNCHECKED));

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWSPEC,
				MF_BYCOMMAND|((config_vismode&NXSBCVM_SPEC)==NXSBCVM_SPEC?MF_CHECKED:MF_UNCHECKED));

			CheckMenuRadioItem(g_hPopupMenu, ID_CONTEXTMENU_DISABLED, ID_CONTEXTMENU_TIMEOFDAY,
				ID_CONTEXTMENU_DISABLED+config_displaymode, MF_BYCOMMAND|MF_CHECKED);

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_CENTI,
				MF_BYCOMMAND|(config_centi?MF_CHECKED:MF_UNCHECKED));

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_FREEZE,
				MF_BYCOMMAND|(config_freeze?MF_CHECKED:MF_UNCHECKED));

			CheckMenuRadioItem(g_hPopupMenu, ID_CONTEXTMENU_DISABLED, ID_CONTEXTMENU_TIMEOFDAY,
				ID_CONTEXTMENU_DISABLED+config_displaymode, MF_BYCOMMAND|MF_CHECKED);
			Menu_TrackPopup(g_hPopupMenu, TPM_LEFTBUTTON, pt.x, pt.y, hWnd);
		}
		return 1;
	case WM_TIMER:
		if (wParam==1) {
			InvalidateRect(hWnd, NULL, FALSE);
			return 1;
		}
		break;
	case WM_PAINT:
		{
			PAINTSTRUCT ps = {0};
			RECT r = {0};
			wchar_t szTime[256] = {0};

			int pos = 0; // The position we display in our big clock
			int dwDisplayMode = 0;

			/* Only draw visualization if Winamp is playing music and one of the visualization modes are on */
			BOOL bShouldDrawVis = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_ISPLAYING) >= 1 &&
				((config_vismode & NXSBCVM_OSC) == NXSBCVM_OSC || (config_vismode & NXSBCVM_SPEC) == NXSBCVM_SPEC);				

			GetClientRect(hWnd, &r);

			// Create double-buffer
			HDC hdc = CreateCompatibleDC(NULL);
			HDC hdcwnd = GetDC(hWnd);
			HBITMAP hbm = CreateCompatibleBitmap(hdcwnd, r.right, r.bottom);
			ReleaseDC(hWnd, hdcwnd);
			HBITMAP holdbm = (HBITMAP)SelectObject(hdc, hbm);

			// Paint the background
			SetBkColor(hdc, WADlg_getColor(WADLG_ITEMBG));
			SetTextColor(hdc, WADlg_getColor(WADLG_ITEMFG));
			SetBkMode(hdc, TRANSPARENT);
			
			FillRect(hdc, &r, hbrBkGnd);

			HFONT holdfont = (HFONT)SelectObject(hdc, hfDisplay);

			switch (config_displaymode) {
			case NXSBCDM_DISABLED:
				dwDisplayMode = 0;
				break;
			case NXSBCDM_ELAPSEDTIME:
				pos = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETOUTPUTTIME);
				dwDisplayMode = IDS_ELAPSED;
				break;
			case NXSBCDM_REMAININGTIME:
				pos = (SendMessage(plugin.hwndParent, WM_WA_IPC, 1, IPC_GETOUTPUTTIME)*1000)-
					SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETOUTPUTTIME);
				dwDisplayMode = IDS_REMAINING;
				break;
			case NXSBCDM_PLELAPSED:
				{
					static int prevplpos=-1;
					int plpos;
					static UINT pltime;
					basicFileInfoStruct bfi = {0};

					plpos = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS);

					// Only do this when song has changed, since the code encapsulated by this
					// if clause is very time consuming if the playlist is large.
					if (plpos != prevplpos) {
						prevplpos = plpos;

						// Get combined duration of all songs up to (but not including) the current song
						pltime = 0;
						for (int i=0;i<plpos;i++) {
							bfi.filename = (char*)SendMessage(plugin.hwndParent, WM_WA_IPC, i, IPC_GETPLAYLISTFILE);
							SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&bfi, IPC_GET_BASIC_FILE_INFO);
							pltime += bfi.length;
						}
						pltime *= 1000; // s -> ms
					}
					// Add elapsed time of current song and store result in pos
					pos = pltime+SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETOUTPUTTIME);

					dwDisplayMode = IDS_PLAYLIST_ELAPSED;
				}
				break;
			case NXSBCDM_PLREMAINING:
				{
					static int prevplpos=-1;
					int plpos;
					UINT pllen;
					static UINT pltime;
					UINT i;
					basicFileInfoStruct bfi;

					bfi.quickCheck = 0;
					bfi.title = 0;
					bfi.titlelen = 0;
				
					pllen = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTLENGTH);
					plpos = SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS);

					// Only do this when song has changed, since the code encapsulated by this
					// if clause is very time consuming if the playlist is large.
					if (plpos != prevplpos) {
						prevplpos = plpos;

						// Get combined duration of all songs from and including the current song to end of list
						pltime = 0;
						for (i=plpos;i<pllen;i++) {
							bfi.filename = (char*)SendMessage(plugin.hwndParent, WM_WA_IPC, i, IPC_GETPLAYLISTFILE);
							SendMessage(plugin.hwndParent, WM_WA_IPC, (WPARAM)&bfi, IPC_GET_BASIC_FILE_INFO);
							pltime += bfi.length;
						}
						pltime *= 1000; // s -> ms
					}

					// Subtract elapsed time of current song and store result in pos
					pos = (pltime-SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETOUTPUTTIME));
					dwDisplayMode = IDS_PLAYLIST_REMAINING;
				}
				break;
			case NXSBCDM_TIMEOFDAY:
				{
					SYSTEMTIME st = {0};
					GetLocalTime(&st);
					pos = ((st.wHour*60*60)+(st.wMinute*60)+st.wSecond)*1000+st.wMilliseconds;
					dwDisplayMode = IDS_TIME_OF_DAY;
				}
				break;
			}
			
			if (config_displaymode != NXSBCDM_DISABLED) {
				int len = GetFormattedTime(szTime, ARRAYSIZE(szTime), pos);

				if (config_shadowtext) {
// Using COMCTL32.DLL's DrawShadowText function is slow,
// that's why I have an ifdef for it.
#ifdef USE_COMCTL_DRAWSHADOWTEXT
					DrawShadowText(hdc, szTime, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE,
							WADlg_getColor(WADLG_ITEMFG), 0x00808080, 5, 5);
#else
					// Draw text's "shadow"
					r.left += 5;
					r.top += 5;
					SetTextColor(hdc, 0x00808080);
					DrawText(hdc, szTime, len, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

					// Draw text
					r.left -= 5;
					r.top -= 5;
					SetTextColor(hdc, WADlg_getColor(WADLG_ITEMFG));
					DrawText(hdc, szTime, len, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
#endif
				} else {
					DrawText(hdc, szTime, len, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
				}
			}

			SelectObject(hdc, holdfont);

			if (bShouldDrawVis) {
				DrawVisualization(hdc, r);
			}

			if (config_showdisplaymode && dwDisplayMode > 0) {
				holdfont = (HFONT)SelectObject(hdc, hfMode);

				SIZE s = {0};
				LPCWSTR lpszDisplayMode = WASABI_API_LNGSTRINGW(dwDisplayMode);
				int len = lstrlen(lpszDisplayMode);
				GetTextExtentPoint32(hdc, lpszDisplayMode, len, &s);
				TextOut(hdc, 0, r.bottom-s.cy, lpszDisplayMode, len);

				SelectObject(hdc, holdfont);
			}

			hdcwnd = BeginPaint(hWnd, &ps);

			// Copy double-buffer to screen
			BitBlt(hdcwnd, r.left, r.top, r.right, r.bottom, hdc, 0, 0, SRCCOPY);

			EndPaint(hWnd, &ps);

			// Destroy double-buffer
			SelectObject(hdc, holdbm);
			DeleteObject(hbm);
			DeleteDC(hdc);
		}
		return 0;
	}

	return CallWindowProc(DefWindowProc, hWnd, uMsg, wParam, lParam);
}

void DrawVisualization(HDC hdc, RECT r)
{
	BLENDFUNCTION bf={0,0,80,0};
	RECT rVis;

	static char* (*export_sa_get)(void)=NULL;
	static void (*export_sa_setreq)(int)=NULL;

	/* Get function pointers from Winamp */
	if (!export_sa_get)
		export_sa_get = (char* (*)(void))SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GETSADATAFUNC);
	if (!export_sa_setreq)
		export_sa_setreq = (void (*)(int))SendMessage(plugin.hwndParent, WM_WA_IPC, 1, IPC_GETSADATAFUNC);

	HDC hdcVis = CreateCompatibleDC(NULL);
	HBITMAP hbmVis = CreateCompatibleBitmap(hdc, 75, 40);
	HBITMAP holdbmVis = (HBITMAP)SelectObject(hdcVis, hbmVis);

	/* Create the pen for the line drawings */
	HPEN holdpenVis = (HPEN)SelectObject(hdcVis, hpenVis);

	/* Specify, that we want both spectrum and oscilloscope data */
	export_sa_setreq(1); /* Pass 0 (zero) and get spectrum data only */
	char *sadata = export_sa_get(); // Visualization data

	/* Clear background */
	FillRect(hdcVis, &r, hbrBkGnd);

	/* Render the oscilloscope */
	if ((config_vismode & NXSBCVM_OSC) == NXSBCVM_OSC) {
		MoveToEx(hdcVis, r.left-1, r.top + 20, NULL);
		for (int x = 0; x < 75; x++)
			LineTo(hdcVis, r.left+x, (r.top+20) + sadata[75+x]);
	}

	if ((config_vismode & NXSBCVM_SPEC) == NXSBCVM_SPEC) {
		rVis.top = 0;
		rVis.left = 0;
		rVis.right = 75;
		rVis.bottom = 40;
		DrawAnalyzer(hdcVis, rVis, sadata);
	}

	SelectObject(hdcVis, holdpenVis);

	// Blit to screen
	GdiAlphaBlend(hdc, r.left, r.top, r.right, r.bottom, hdcVis, 0, 0, 75, 40, bf);

	// Destroy vis bitmap/DC
	SelectObject(hdcVis, holdbmVis);
	DeleteObject(hbmVis);
	DeleteDC(hdcVis);
}

void DrawAnalyzer(HDC hdc, RECT r, char *sadata)
{
	static char sapeaks[150];
	static char safalloff[150];
	static char sapeaksdec[150];

	for (int x = 0; x < 75; x++)
	{
		/* Fix peaks & falloff */
		if (sadata[x] > sapeaks[x])
		{
			sapeaks[x] = sadata[x];
			sapeaksdec[x] = 0;
		}
		else
		{
			sapeaks[x] = sapeaks[x] - 1;

			if (sapeaksdec[x] >= 8)
				sapeaks[x] = (char)(int)(sapeaks[x] - 0.3 * (sapeaksdec[x] - 8));

			if (sapeaks[x] < 0)
				sapeaks[x] = 0;
			else
				sapeaksdec[x] = sapeaksdec[x] + 1;
		}

		if (sadata[x] > safalloff[x]) safalloff[x] = sadata[x];
		else safalloff[x] = safalloff[x] - 2;

		MoveToEx(hdc, r.left+x, r.bottom, NULL);
		LineTo(hdc, r.left+x, r.bottom - safalloff[x]);

		// Draw peaks
		MoveToEx(hdc, r.left+x, r.bottom - safalloff[x], NULL);
		LineTo(hdc, r.left+x, r.bottom - safalloff[x]);
	}
}

LRESULT CALLBACK GenWndSubclass(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_LBUTTONDOWN:
		{
			if (!config_freeze) {
				break;
			}

			// Ctrl down?
			if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
				break;
			}

			RECT r = {0};
			GetWindowRect(hwnd, &r);
			int y = HIWORD(lParam);
			int tx = LOWORD(lParam) + 275 - (r.right - r.left);
			int ty = HIWORD(lParam) + 116 - (r.bottom - r.top);
				
			if (((tx >= 256) && (tx <= 275) && (ty >= 102) && (ty <= 116)) // Size corner
					|| ((tx >= 264) && (tx <= 273) && (y >= 3) && (y <= 12)) // Close
					|| ((tx >= 267) && (tx <= 275) && (ty >= 96) && (ty <= 116))) { // Size corner
				break;
			}
		}
		return 0;
	}
	return CallWindowProc(lpGenWndProcOld, hwnd, uMsg, wParam, lParam);
}

void InsertMenuItemInWinamp()
{
	int i;
	HMENU WinampMenu;
	UINT id;

	// get main menu
	WinampMenu = (HMENU)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_HMENU);

	// find menu item "main window"
	for (i=GetMenuItemCount(WinampMenu); i>=0; i--)
	{
		if (GetMenuItemID(WinampMenu, i) == 40258)
		{
			// find the separator and return if menu item already exists
			do {
				id=GetMenuItemID(WinampMenu, ++i);
				if (id==WINAMP_NXS_BIG_CLOCK_MENUID) return;
			} while (id != 0xFFFFFFFF);

			// insert menu just before the separator
			InsertMenu(WinampMenu, i-1, MF_BYPOSITION|MF_STRING, WINAMP_NXS_BIG_CLOCK_MENUID, szAppName);
			break;
		}
	}
}

void RemoveMenuItemFromWinamp()
{
	HMENU WinampMenu;
	WinampMenu = (HMENU)SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_HMENU);
	RemoveMenu(WinampMenu, WINAMP_NXS_BIG_CLOCK_MENUID, MF_BYCOMMAND);
}


/* Creates a URL file and executes it.
   Returns 1 if everything was okey! */
int ExecuteURL(wchar_t *url)
{
	HANDLE hf;
	wchar_t szTmp[4096];
	DWORD written;
	int hinst;
	wchar_t szName[MAX_PATH+1];
	wchar_t szTempPath[MAX_PATH+1];
	wchar_t *p;

	GetTempPath(MAX_PATH, szTempPath);
	GetTempFileName(szTempPath, L"lnk", 0, szName);
	DeleteFile(szName);
	/* We got a temporary filename, change the extension to ".URL" */
	p = szName;
	while (*p) ++p; /*go to end*/
	while (p >= szName && *p != L'.') --p;
	*p = 0;
	StringCchCat(szName, ARRAYSIZE(szName), L".URL");


	hf = CreateFile(szName, /*GENERIC_READ|*/GENERIC_WRITE,
		FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	StringCchPrintf(szTmp, ARRAYSIZE(szTmp), L"[InternetShortcut]\r\nurl=%s\r\n", url);
	WriteFile(hf, szTmp, lstrlen(szTmp), &written, NULL);
	CloseHandle(hf);
	hinst = (int)ShellExecute(0, L"open", szName, NULL, NULL, SW_SHOWNORMAL);
	DeleteFile(szName);
	return (written && hinst>32);
}


extern "C" __declspec (dllexport) winampGeneralPurposePlugin * winampGetGeneralPurposePlugin() {
	return &plugin;
}


extern "C" __declspec(dllexport) int winampUninstallPlugin(HINSTANCE hDllInst, HWND hwndDlg, int param)
{
	// prompt to remove our settings with default as no (just incase)
	if (MessageBox(hwndDlg, WASABI_API_LNGSTRINGW(IDS_DO_YOU_ALSO_WANT_TO_REMOVE_SETTINGS),
				   (LPWSTR)plugin.description, MB_YESNO | MB_DEFBUTTON2) == IDYES)
	{
		WritePrivateProfileString(PLUGIN_INISECTION, 0, 0, ini_file);
		no_uninstall = 0;
	}

	// as we're doing too much in subclasses, etc we cannot allow for on-the-fly removal so need to do a normal reboot
	return GEN_PLUGIN_UNINSTALL_REBOOT;
}


/* makes a smaller DLL file */

#ifndef _DEBUG
BOOL WINAPI _DllMainCRTStartup(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
	}
	return TRUE;
}
#endif
