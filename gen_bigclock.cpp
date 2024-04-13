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

#define PLUGIN_VERSION "1.15.1"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <commdlg.h>
#include <strsafe.h>
#include <math.h>

#include <winamp/gen.h>
#include <winamp/wa_cup.h>
#include <winamp/wa_msgids.h>
#include <gen_ml/ml.h>
#include <gen_ml/ml_ipc_0313.h>
#include <nu/servicebuilder.h>
#define WA_DLG_IMPORTS
#include <winamp/wa_dlg.h>
#include "embedwnd.h"
#include "api.h"
#include <loader/loader/paths.h>
#include <loader/loader/utils.h>
#include <loader/loader/ini.h>
#include "resource.h"

/* global data */
static const wchar_t szAppName[] = L"NxS BigClock";
#define PLUGIN_INISECTION szAppName

// Menu ID's
UINT WINAMP_NXS_BIG_CLOCK_MENUID = (ID_GENFF_LIMIT+101);

// Display mode constants
#define NXSBCDM_DISABLED 0
#define NXSBCDM_ELAPSEDTIME 1
#define NXSBCDM_REMAININGTIME 2
#define NXSBCDM_PLELAPSED 3
#define NXSBCDM_PLREMAINING 4
#define NXSBCDM_TIMEOFDAY 5
#define NXSBCDM_BEATSTIME 6
#define NXSBCDM_MAX 6

#define UPDATE_TIMER_ID 1
#define UPDATE_TIMER 33

/* BigClock window */
static HWND g_BigClockWnd;
static wchar_t g_BigClockClassName[] = L"NxSBigClockWnd";
LRESULT CALLBACK BigClockWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static HMENU g_hPopupMenu = 0;
static LPARAM ipc_bigclockinit = -1;
static ATOM wndclass = 0;
static HWND hWndBigClock = NULL;
static embedWindowState embed = {0};
static int upscaling = 1, dsize = 0, no_uninstall = 1;
static HPEN hpenVis = NULL;
static HFONT hfDisplay = NULL, hfMode = NULL;
static LOGFONT lfDisplay = {0}, lfMode = {0};
static HANDLE CalcThread;
static int64_t pltime;
static int prevplpos = -1, resetCalc = 0;

// just using these to track the paused and playing states
int is_paused = 0, is_playing = 0, plpos = 0, itemlen = 0;
UINT pllen = 0;

COLORREF clrBackground = RGB(0, 0, 0),
		 clrTimerText = RGB(0, 255, 0),
		 clrVisOsc = RGB(0, 255, 0),
		 clrVisSA = RGB(0, 255, 0),
		 clrTimerTextShadow = 0x00808080;

void DrawVisualization(HDC hdc, RECT r);

DWORD WINAPI CalcLengthThread(LPVOID lp);
DWORD_PTR CALLBACK ConfigDlgProc(HWND,UINT,WPARAM,LPARAM);

#ifdef NATIVE_FREEZE
/* subclass of skinned frame window (GenWnd) */
LRESULT CALLBACK GenWndSubclass(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
								UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
#endif

/* Menu item functions */
#define NXSBCVM_OSC 1
#define NXSBCVM_SPEC 2

/* configuration items */
static int config_shadowtextnew=FALSE;
static int config_showdisplaymode=TRUE;
static int config_vismode=TRUE;
static int config_displaymode=NXSBCDM_ELAPSEDTIME;
static int config_timeofdaymode=1 | 2;
static int config_centi=1;
#ifdef NATIVE_FREEZE
static int config_freeze=0;
#endif

/* plugin function prototypes */
void config(void);
void quit(void);
int init(void);


void __cdecl MessageProc(HWND hWnd, const UINT uMsg, const
						 WPARAM wParam, const LPARAM lParam);

winampGeneralPurposePlugin plugin =
{
	GPPHDR_VER_WACUP,
	(char*)L"Big Clock",
	init, config, quit,
	GEN_INIT_WACUP_HAS_MESSAGES
};

SETUP_API_LNG_VARS;

// this is used to identify the skinned frame to allow for embedding/control by modern skins if needed
// {DF6F9C93-155C-4d01-BF5A-17612EDBFC4C}
static const GUID embed_guid = 
{ 0xdf6f9c93, 0x155c, 0x4d01, { 0xbf, 0x5a, 0x17, 0x61, 0x2e, 0xdb, 0xfc, 0x4c } };

void UpdateSkinParts(void) {

	clrBackground = WADlg_getColor(WADLG_ITEMBG);
	clrTimerText = clrVisOsc = clrVisSA = WADlg_getColor(WADLG_ITEMFG);
	clrTimerTextShadow = 0x00808080;

	// get the current skin and use that as a
	// means to control the colouring used
	wchar_t szBuffer[MAX_PATH] = { 0 };
	GetCurrentSkin(szBuffer, ARRAYSIZE(szBuffer));

	// attempt to now use the skin override options
	// which are provided in a waveseek.txt within
	// the root of the skin (folder or archive).
	if (szBuffer[0])
	{
		// look for the file that classic skins could provide
		AppendOnPath(szBuffer, L"bigclock.txt");
		if (FileExists(szBuffer)) {
			clrBackground = GetPrivateProfileHex(L"colours", L"background", clrBackground, szBuffer);
			clrTimerText = GetPrivateProfileHex(L"colours", L"timertext", clrTimerText, szBuffer);
			clrTimerTextShadow = GetPrivateProfileHex(L"colours", L"timertextshadow", clrTimerTextShadow, szBuffer);
			clrVisOsc = GetPrivateProfileHex(L"colours", L"visosc", clrVisOsc, szBuffer);
			clrVisSA = GetPrivateProfileHex(L"colours", L"vissa", clrVisSA, szBuffer);
		}
#ifndef _WIN64
		else {
			// otherwise look for (if loaded) anything within the
			// modern skin configuration for it's override colours
			clrBackground = GetFFSkinColour(L"plugin.bigclock.background", clrBackground);
			clrTimerText = GetFFSkinColour(L"plugin.bigclock.timertext", clrTimerText);
			clrTimerTextShadow = GetFFSkinColour(L"plugin.bigclock.timertextshadow", clrTimerTextShadow);
			clrVisOsc = GetFFSkinColour(L"plugin.bigclock.visosc", clrVisOsc);
			clrVisSA = GetFFSkinColour(L"plugin.bigclock.vissa", clrVisSA);
		}
#endif
	}

	if (hpenVis) {
		DeleteObject(hpenVis);
	}
	hpenVis = CreatePen(PS_SOLID, (!dsize ? 1 : 2)/*(!dsize ? 2 : 4)*/, clrVisOsc);

	if (hfDisplay) {
		DeleteObject(hfDisplay);
	}

	hfDisplay = CreateFontIndirect(&lfDisplay);

	if (hfMode) {
		DeleteObject(hfMode);
	}
	hfMode = CreateFontIndirect(&lfMode);

	if (IsWindow(g_BigClockWnd))
	{
		InvalidateRect(g_BigClockWnd, NULL, FALSE);
	}
}

void ReadFontSettings(void) {
	lfDisplay.lfHeight = GetNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"df_h", -50);
	lfMode.lfHeight = GetNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"mf_h", -13);

	lfDisplay.lfItalic = (BYTE)GetNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"df_i", 0);
	lfMode.lfItalic = (BYTE)GetNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"mf_i", 0);

	lfDisplay.lfWeight = GetNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"df_b", 0);
	lfMode.lfWeight = GetNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"mf_b", 0);

	GetNativeIniString(WINAMP_INI, PLUGIN_INISECTION, L"df", L"Arial", lfDisplay.lfFaceName, LF_FACESIZE);
	GetNativeIniString(WINAMP_INI, PLUGIN_INISECTION, L"mf", L"Arial", lfMode.lfFaceName, LF_FACESIZE);
}

void CALLBACK UpdateWnTimerProc(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
	if ((idEvent == UPDATE_TIMER_ID) && IsWindowVisible(hWnd)) {
		InvalidateRect(hWnd, NULL, FALSE);
	}
}

void SaveDisplayMode(void) {
	if (config_displaymode != NXSBCDM_ELAPSEDTIME) {
		SaveNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"config_displaymode", config_displaymode);
	}
	else {
		SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, L"config_displaymode", NULL);
	}

	// for the time of day we need to ensure it's running
	// regularly otherwise it'll only be updated if playing
	if ((config_displaymode == NXSBCDM_TIMEOFDAY) ||
		(config_displaymode == NXSBCDM_BEATSTIME)) {
		if (!is_playing) {
			SetTimer(g_BigClockWnd, UPDATE_TIMER_ID, ((config_vismode ||
					 config_centi) ? UPDATE_TIMER : 1000), UpdateWnTimerProc);
		}
	}
	else {
		if (!is_playing) {
			KillTimer(g_BigClockWnd, UPDATE_TIMER_ID);
			InvalidateRect(g_BigClockWnd, NULL, FALSE);
		}

		// if changing to the playlist elapsed / remaining
		// modes then trigger a (re-)calcuation of the info
		if ((config_displaymode == NXSBCDM_PLELAPSED) ||
			(config_displaymode == NXSBCDM_PLREMAINING)) {
			prevplpos = -1;
		}
	}
}

bool ProcessMenuResult(WPARAM command, HWND parent) {
	switch (LOWORD(command)) {
		case ID_CONTEXTMENU_DISABLED:
		case ID_CONTEXTMENU_ELAPSED:
		case ID_CONTEXTMENU_REMAINING:
		case ID_CONTEXTMENU_PLAYLISTELAPSED:
		case ID_CONTEXTMENU_PLAYLISTREMAINING:
		case ID_CONTEXTMENU_TIMEOFDAY:
		case ID_CONTEXTMENU_BEATSTIME:
			config_displaymode = LOWORD(command)-ID_CONTEXTMENU_DISABLED;
			SaveDisplayMode();
			break;
		case ID_CONTEXTMENU_SHOWDISPLAYMODE:
			config_showdisplaymode = !config_showdisplaymode;
			SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, L"config_showdisplaymode",
								(config_showdisplaymode ? NULL : L"0"));
			break;
		case ID_CONTEXTMENU_SHADOWEDTEXT:
			config_shadowtextnew = !config_shadowtextnew;
			SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, L"config_shadowtextnew",
								(config_shadowtextnew ? L"1" : NULL));
			break;
		case ID_CONTEXTMENU_NONE:
			config_vismode = 0;
			SaveNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"config_vismode", config_vismode);
			break;
		case ID_CONTEXTMENU_SHOWOSC:
			config_vismode ^= NXSBCVM_OSC;
			SaveNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"config_vismode", config_vismode);
			break;
		case ID_CONTEXTMENU_SHOWSPEC:
			config_vismode ^= NXSBCVM_SPEC;
			SaveNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"config_vismode", config_vismode);
			break;
		case ID_CONTEXTMENU_CENTI:
			config_centi = !config_centi;
			SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, L"config_centi",
								(config_centi ? NULL : L"0"));
			break;
		case ID_CONTEXTMENU_SHOWSECONDSFORTIMEOFDAY:
			config_timeofdaymode ^= 1;
			SaveNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"config_timeofdaymode", config_timeofdaymode);
			break;
		case ID_CONTEXTMENU_SHOWTIMEOFDAYAS24HOURS:
			config_timeofdaymode ^= 2;
			SaveNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"config_timeofdaymode", config_timeofdaymode);
			break;
		case ID_CONTEXTMENU_USEADOTASPM:
			config_timeofdaymode ^= 4;
			SaveNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"config_timeofdaymode", config_timeofdaymode);
			break;
		case ID_CONTEXTMENU_RESETFONTS:
			{
				if (MessageBox(parent, WASABI_API_LNGSTRINGW(IDS_RESET_FONTS),
							   (LPWSTR)plugin.description, MB_YESNO |
							   MB_ICONQUESTION | MB_DEFBUTTON2) == IDNO) {
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
				CHOOSEFONT cf = { 0 };
				cf.lStructSize = sizeof(cf);
				cf.hwndOwner = parent;

				// because we're modifying things then we need to
				// set the font back to normal if we're in double
				// size mode otherwise it'll get into a big mess!
				if (upscaling && dsize) {
					lfDisplay.lfHeight /= 2;
					lfMode.lfHeight /= 2;
				}

				lf = cf.lpLogFont = (!mode ? &lfDisplay : &lfMode);
				cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
				update = PickFont(&cf);

				// this will revert the value back if the action
				// was cancelled otherwise the fonts will shrink
				// the next time a skin refresh gets received &
				// it also requires re-reading in the font style
				if (!update) {
					ReadFontSettings();

					// handle double-size mode as required by auto-scaling the font if its needed
					if (upscaling && dsize) {
						SendMessage(g_BigClockWnd, WM_USER + 0x99, (WPARAM)dsize, (LPARAM)upscaling);
					}
					else {
						UpdateSkinParts();
					}
					break;
				}
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
					StringCchCopy(lfDisplay.lfFaceName, LF_FACESIZE, L"Arial");

					memset(&lfMode, 0, sizeof(lfMode));
					lfMode.lfHeight = -13;
					StringCchCopy(lfMode.lfFaceName, LF_FACESIZE, L"Arial");
				}

reparse:
				wchar_t buf[32] = {0};
				I2WStr((!mode ? lfDisplay.lfHeight : lfMode.lfHeight), buf, ARRAYSIZE(buf));
				SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, (!mode ? L"df_h" : L"mf_h"), buf);

				I2WStr((!mode ? lfDisplay.lfItalic : lfMode.lfItalic), buf, ARRAYSIZE(buf));
				SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, (!mode ? L"df_i" : L"mf_i"), buf);

				I2WStr((!mode ? lfDisplay.lfWeight : lfMode.lfWeight), buf, ARRAYSIZE(buf));
				SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, (!mode ? L"df_b" : L"mf_b"), buf);

				SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, (!mode ? L"df" : L"mf"),
									(!mode ? lfDisplay.lfFaceName : lfMode.lfFaceName));

				// dirty way to get main and display fonts correctly reset
				if (reset && !mode) {
					mode = 1;
					goto reparse;
				}

				// handle double-size mode as required by auto-scaling the font if its needed
				if (upscaling && dsize) {
					SendMessage(g_BigClockWnd, WM_USER + 0x99, (WPARAM)dsize, (LPARAM)upscaling);
				}
				else {
					UpdateSkinParts();
				}
			}
			break;
		}
#ifdef NATIVE_FREEZE
		case ID_CONTEXTMENU_FREEZE:
			config_freeze = !config_freeze;
			SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, L"config_freeze",
								(config_freeze ? L"1" : NULL));
			if (config_freeze) {
				Subclass(hWndBigClock, GenWndSubclass);
			}
			else {
				UnSubclass(hWndBigClock, GenWndSubclass);
			}
			break;
#endif
		case ID_CONTEXTMENU_ABOUT:
		{
			wchar_t message[2048] = {0};
			StringCchPrintf(message, ARRAYSIZE(message), WASABI_API_LNGSTRINGW(IDS_ABOUT_STRING), TEXT(__DATE__));
			//MessageBox(plugin.hwndParent, message, (LPWSTR)plugin.description, 0);
			AboutMessageBox(plugin.hwndParent, message, (LPWSTR)plugin.description);
			break;
		}
		default:
			return false;
	}
	if (IsWindow(g_BigClockWnd))
	{
		InvalidateRect(g_BigClockWnd, NULL, FALSE);
	}
	return true;
}

void config(void) {
	HMENU hMenu = WASABI_API_LOADMENUW(IDR_CONTEXTMENU),
		  popup = GetSubMenu(hMenu, 0);

	AddItemToMenu2(popup, 0, (LPWSTR)plugin.description, 0, 1);
	EnableMenuItem(popup, 0, MF_BYCOMMAND | MF_GRAYED | MF_DISABLED);
	AddItemToMenu2(popup, (UINT)-1, 0, 1, 1);

	POINT pt = { 0 };
	HWND list = GetPrefsListPos(&pt);
	ProcessMenuResult(TrackPopupMenu(popup, TPM_RETURNCMD,
					  pt.x, pt.y, 0, list, NULL), list);
	DestroyMenu(hMenu);
}

void quit(void) {

	if (no_uninstall)
	{
		/* Update position and size */
		DestroyEmbeddedWindow(&embed);
	}

	// the wacup core will trigger this
	// to happen so it should all be ok
	// the main thing is the call above
	// occurs so window changes do save
	/*if (IsWindow(hWndBigClock))
	{
		DestroyWindow(hWndBigClock);
		DestroyWindow(g_BigClockWnd); /* delete our window */
	/*}*/

	//UnregisterClass(g_BigClockClassName, plugin.hDllInstance);
}

int init(void) {
	/*WASABI_API_SVC = GetServiceAPIPtr();*//*/
	// load all of the required wasabi services from the winamp client
	WASABI_API_SVC = reinterpret_cast<api_service*>(SendMessage(plugin.hwndParent, WM_WA_IPC, 0, IPC_GET_API_SERVICE));
	if (WASABI_API_SVC == reinterpret_cast<api_service*>(1)) WASABI_API_SVC = NULL;/**/
	/*if (WASABI_API_SVC != NULL)
	{*/
		/*ServiceBuild(WASABI_API_SVC, WASABI_API_APP, applicationApiServiceGuid);
		ServiceBuild(WASABI_API_SVC, WASABI_API_LNG, languageApiGUID);/*/
		/*WASABI_API_LNG = plugin.language;/**/
		// TODO add to lang.h
		/*WASABI_API_START_LANG(plugin.hDllInstance, embed_guid);

		wchar_t pluginTitleW[256] = { 0 };
		StringCchPrintf(pluginTitleW, ARRAYSIZE(pluginTitleW), WASABI_API_LNGSTRINGW(IDS_PLUGIN_NAME), TEXT(PLUGIN_VERSION));
		plugin.description = (char*)plugin.memmgr->sysDupStr(pluginTitleW);*/

		WASABI_API_START_LANG_DESC(plugin.language, plugin.hDllInstance, embed_guid,
						IDS_PLUGIN_NAME, TEXT(PLUGIN_VERSION), &plugin.description);

		// wParam must have something provided else it returns 0
		// and then acts like a IPC_GETVERSION call... not good!
		ipc_bigclockinit = RegisterIPC((WPARAM)&"NXSBC_IPC");
		PostMessage(plugin.hwndParent, WM_WA_IPC, 0, ipc_bigclockinit);

		return GEN_INIT_SUCCESS;
	/*}
	return GEN_INIT_FAILURE;*/
}

void __cdecl MessageProc(HWND hWnd, const UINT uMsg, const
						 WPARAM wParam, const LPARAM lParam)
{
	if (uMsg == WM_WA_IPC)
	{
		/* Init time */
		if (lParam == ipc_bigclockinit) {
			GetNativeIniIntParam(WINAMP_INI, PLUGIN_INISECTION, L"config_showdisplaymode", &config_showdisplaymode);
			GetNativeIniIntParam(WINAMP_INI, PLUGIN_INISECTION, L"config_shadowtextnew", &config_shadowtextnew);
			GetNativeIniIntParam(WINAMP_INI, PLUGIN_INISECTION, L"config_vismode", &config_vismode);
			GetNativeIniIntParam(WINAMP_INI, PLUGIN_INISECTION, L"config_displaymode", &config_displaymode);
			GetNativeIniIntParam(WINAMP_INI, PLUGIN_INISECTION, L"config_centi", &config_centi);
#ifdef NATIVE_FREEZE
			GetNativeIniIntParam(WINAMP_INI, PLUGIN_INISECTION, L"config_freeze", &config_freeze);
#endif
			GetNativeIniIntParam(WINAMP_INI, PLUGIN_INISECTION, L"config_timeofdaymode", &config_timeofdaymode);

			ReadFontSettings();

			dsize = GetDoubleSize(&upscaling);

			// for the purposes of this example we will manually create an accelerator table so
			// we can use IPC_REGISTER_LOWORD_COMMAND to get a unique id for the menu items we
			// will be adding into Winamp's menus. using this api will allocate an id which can
			// vary between Winamp revisions as it moves depending on the resources in Winamp.
			WINAMP_NXS_BIG_CLOCK_MENUID = RegisterCommandID(0);

			// then we show the embedded window which will cause the child window to be
			// sized into the frame without having to do any thing ourselves. also this will
			// only show the window if Winamp was not minimised on close and the window was
			// open at the time otherwise it will remain hidden
			old_visible = visible = GetNativeIniInt(WINAMP_INI, PLUGIN_INISECTION, L"config_show", TRUE);

			// finally we add menu items to the main right-click menu and the views menu
			// with Modern skins which support showing the views menu for accessing windows
			AddEmbeddedWindowToMenus(WINAMP_NXS_BIG_CLOCK_MENUID, WASABI_API_LNGSTRINGW(IDS_NXS_BIG_CLOCK_MENU), visible, -1);

			// now we will attempt to create an embedded window which adds its own main menu entry
			// and related keyboard accelerator (like how the media library window is integrated)
			embed.flags |= EMBED_FLAGS_SCALEABLE_WND;	// double-size support!
			hWndBigClock = CreateEmbeddedWindow(&embed, embed_guid);

#ifdef NATIVE_FREEZE
			/* Subclass skinned window frame but only if it's needed for the window freezing*/
			if (config_freeze) {
				Subclass(hWndBigClock, GenWndSubclass);
			}
#endif

			// once the window is created we can then specify the window title and menu integration
			SetWindowText(hWndBigClock, WASABI_API_LNGSTRINGW(IDS_NXS_BIG_CLOCK));

			WNDCLASSEX wcex = { 0 };
			wcex.cbSize = sizeof(WNDCLASSEX);
			wcex.lpszClassName = g_BigClockClassName;
			wcex.hInstance = plugin.hDllInstance;
			wcex.lpfnWndProc = BigClockWndProc;
			wcex.hCursor = GetArrowCursor(false);
			wndclass = RegisterClassEx(&wcex);
			if (wndclass)
			{
				g_BigClockWnd = CreateWindowEx(0, (LPCTSTR)wndclass, szAppName, WS_CHILD | WS_VISIBLE,
											   0, 0, 0, 0, hWndBigClock, NULL, plugin.hDllInstance, NULL);
			}

			// Note: WASABI_API_APP->app_addAccelerators(..) requires Winamp 5.53 and higher
			//       otherwise if you want to support older clients then you could use the
			//       IPC_TRANSLATEACCELERATOR callback api which works for v5.0 upto v5.52
			ACCEL accel = { FVIRTKEY | FALT, 'B', (WORD)WINAMP_NXS_BIG_CLOCK_MENUID };
			HACCEL hAccel = CreateAcceleratorTable(&accel, 1);
			if (hAccel)
			{
				plugin.app->app_addAccelerators(g_BigClockWnd, &hAccel, 1, TRANSLATE_MODE_GLOBAL);
			}

			// Winamp can report if it was started minimised which allows us to control our window
			// to not properly show on startup otherwise the window will appear incorrectly when it
			// is meant to remain hidden until Winamp is restored back into view correctly
			if (InitialShowState() == SW_SHOWMINIMIZED)
			{
				SetEmbeddedWindowMinimisedMode(hWndBigClock, MINIMISED_FLAG, TRUE);
			}
			/*else
			{
				// only show on startup if under a classic skin and was set
				if (visible)
				{
					ShowHideEmbeddedWindow(hWndBigClock, TRUE, FALSE);
				}
			}*/

			// ensure we've got current states as due to how the load
			// can happen, it's possible to not catch the play event.
			const int playing = GetPlayingState();
			is_paused = (playing == 3);
			is_playing = (playing == 1 || is_paused);

			pllen = GetPlaylistLength();
			plpos = GetPlaylistPosition();
			itemlen = GetCurrentTrackLengthMilliSeconds();

			if ((config_displaymode == NXSBCDM_TIMEOFDAY) ||
				(config_displaymode == NXSBCDM_BEATSTIME)) {
				if (!is_playing) {
					SetTimer(g_BigClockWnd, UPDATE_TIMER_ID, ((config_vismode ||
							 config_centi) ? UPDATE_TIMER : 1000), UpdateWnTimerProc);
				}
			}
			else {
				if (is_playing) {
					SetTimer(g_BigClockWnd, UPDATE_TIMER_ID, UPDATE_TIMER, UpdateWnTimerProc);
				}
			}
		}
		else if (lParam == IPC_GET_EMBEDIF_NEW_HWND)
		{
			if (((HWND)wParam == hWndBigClock) && visible &&
				(InitialShowState() != SW_SHOWMINIMIZED))
			{
				// only show on startup if under a classic skin and was set
				ShowHideEmbeddedWindow(hWndBigClock, TRUE, FALSE);
			}
		}
		// this is sent after IPC_PLAYING_FILE on 5.3+ clients
		else if (lParam == IPC_PLAYING_FILEW ||
				 lParam == IPC_PLAYLIST_ITEM_REMOVED) {
			if (lParam == IPC_PLAYING_FILEW) {
				is_paused = 0;
				is_playing = 1;
			}

			pllen = GetPlaylistLength();
			plpos = GetPlaylistPosition();
			itemlen = GetCurrentTrackLengthMilliSeconds();

			KillTimer(g_BigClockWnd, UPDATE_TIMER_ID);
			SetTimer(g_BigClockWnd, UPDATE_TIMER_ID, UPDATE_TIMER, UpdateWnTimerProc);
		}
		// this whole section tests the playback state to determine what is happening
		else if (lParam == IPC_CB_MISC && wParam == IPC_CB_MISC_STATUS) {
			const int cur_playing = GetPlayingState();
			// CDs can be funky so it's simpler to just assume that if we're into the
			// playing state that the CD is loading (e.g. autoplay on start-up) & its
			// not quite ready with data / playing otherwise we won't then render vis
			if ((cur_playing == 1) && (GetCurrentTrackPos() > 0) ||
				IsCDEntry(GetPlaylistItemFile(plpos, NULL))) {
				if (is_paused) {
					is_paused = 0;
					SetTimer(g_BigClockWnd, UPDATE_TIMER_ID, (!config_vismode &&
							 (config_displaymode == NXSBCDM_TIMEOFDAY) ||
							 (config_displaymode == NXSBCDM_BEATSTIME) ? (config_centi ?
							 UPDATE_TIMER : 1000): UPDATE_TIMER), UpdateWnTimerProc);
				}
			}
			else if (cur_playing == 3) {
				is_paused = 1;
				if ((config_displaymode != NXSBCDM_TIMEOFDAY) &&
					(config_displaymode != NXSBCDM_BEATSTIME)) {
					// we'll let it keep running otherwise view
					// updates won't work as expected plus the
					// drawing will deal with avoiding querying
					// for vis data as needed vs being paused
					//KillTimer(g_BigClockWnd, UPDATE_TIMER_ID);
					InvalidateRect(g_BigClockWnd, NULL, FALSE);
				}
			}
			else if (!cur_playing && (is_playing == 1)) {
				is_playing = is_paused = 0;
				if ((config_displaymode != NXSBCDM_TIMEOFDAY) &&
					(config_displaymode != NXSBCDM_BEATSTIME)) {
					KillTimer(g_BigClockWnd, UPDATE_TIMER_ID);
					InvalidateRect(g_BigClockWnd, NULL, FALSE);
				}
			}
			else if (!cur_playing && (is_playing == 0)) {
				pllen = GetPlaylistLength();
				plpos = GetPlaylistPosition();
				itemlen = GetCurrentTrackLengthMilliSeconds();
				InvalidateRect(g_BigClockWnd, NULL, FALSE);
			}
			else
			{
				// if we've gotten here then probably it's whilst
				// things are loading & trying to resume playback
				// which without this may prevent the vis running
				is_paused = (cur_playing == 3);
				is_playing = (cur_playing == 1 || is_paused);
			}
		}
	}
	
	// this will handle the message needed to be caught before the original window
	// proceedure of the subclass can process it. with multiple windows then this
	// would need to be duplicated for the number of embedded windows your handling
	HandleEmbeddedWindowWinampWindowMessages(hWndBigClock, WINAMP_NXS_BIG_CLOCK_MENUID,
											 &embed, hWnd, uMsg, wParam, lParam);
}

int GetFormattedTime(LPWSTR lpszTime, const UINT size, const int64_t iPos, const int mode) {

	double time_s = (iPos > 0 ? iPos*0.001 : 0);
	/*int days = (int)(time_s / 86400);
	time_s -= (days * 60 * 60 * 24);*/
	int hours = (int)(time_s / 3600) % 60;
	time_s -= (hours*3600);
	int minutes = (int)(time_s/60);
	time_s -= (minutes*60);
	int seconds = (int)(time_s);
	time_s -= seconds;
	int dsec = (int)(time_s*100);

	size_t remaining = size;
	if (mode != 1) {
		/*const wchar_t szFmtFull[] = L"%d:%.2d:%.2d\0";
		if (days > 0) {
			StringCchPrintf(lpszTime, size, L"%d %d:%.2d:%.2d\0", days, hours, minutes, seconds);
		}
		else if (hours > 0) {
			StringCchPrintf(lpszTime, size, szFmtFull, hours, minutes, seconds);
		}
		else {
			StringCchPrintf(lpszTime, size, szFmtFull + 3, minutes, seconds);
		}/*/
		plugin.language->FormattedTimeString(lpszTime, size, (int)(!mode ? (iPos / 1000LL) :
														 ceil(iPos/1000.f)), 0, &remaining);
		if (!*lpszTime)	{
			StringCchCopyEx(lpszTime, size, L"0:00", NULL, &remaining, NULL);
		}
	}
	else {
		const bool show_24hrs = (config_timeofdaymode & 2),
				   show_dot = (config_timeofdaymode & 4);
		if (config_timeofdaymode & 1) {
			const wchar_t szFmtFull[] = L"%d:%.2d:%.2d%s%s\0",
						  szFmtFullCenti[] = L"%d:%.2d:%.2d.%.2d%s\0";
			StringCchPrintfEx(lpszTime, size, NULL, &remaining, NULL, (!config_centi ?
							  szFmtFull : szFmtFullCenti), (!show_24hrs && (hours > 12) ?
							  (hours - 12) : hours), minutes, seconds, (config_centi ?
							  dsec : (intptr_t)L""), (show_24hrs ? L"" : ((hours >= 12) ?
							  (show_dot ? L" ." : L"pm") : (show_dot ? L"" : L"am"))));
		}
		else {
			const wchar_t szFmtNoSec[] = L"%d:%.2d%s\0";
			StringCchPrintfEx(lpszTime, size, NULL, &remaining, NULL, szFmtNoSec,
							  (!show_24hrs && (hours > 12) ? (hours - 12) : hours),
							  minutes, (show_24hrs ? L"" : (hours >= 12 ? (show_dot ?
										L" ." : L"pm") : (show_dot ? L"" : L"am"))));
		}
	}

	if (!mode && config_centi) {
		const wchar_t szMsFmt[] = L".%.2d";
		size_t offset = (size - remaining);
		StringCchPrintfEx((lpszTime + offset), (size - offset),
						   NULL, &remaining, 0, szMsFmt, dsec);
	}

	return (int)(size - remaining);
}

DWORD WINAPI CalcLengthThread(LPVOID lp)
{
startCalc:
	if (!lp) {
		for (int i=0;i<plpos;i++) {
			basicFileInfoStructW bfi = { 0, 0, -1, NULL, 0 };
			bfi.filename = GetPlaylistItemFile(i, NULL);
			if (GetBasicFileInfo(&bfi, TRUE, TRUE)) {
				pltime += bfi.length;
			}

			if (resetCalc) {
				break;
			}
		}
	}
	else {
		for (UINT i=plpos;i<pllen;i++) {
			basicFileInfoStructW bfi = { 0, 0, -1, NULL, 0 };
			bfi.filename = GetPlaylistItemFile(i, NULL);
			if (GetBasicFileInfo(&bfi, TRUE, TRUE)) {
				pltime += bfi.length;
			}

			if (resetCalc) {
				break;
			}
		}
	}

	if (resetCalc) {
		resetCalc = 0;
		goto startCalc;
	}

	pltime *= 1000LL; // s -> ms

	if (IsWindow(g_BigClockWnd))
	{
		InvalidateRect(g_BigClockWnd, NULL, FALSE);
	}

	if (CalcThread)
	{
		CloseHandle(CalcThread);
		CalcThread = 0;
	}
	return 0;
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
			// handle double-size mode as required by auto-scaling the font if its needed
			if (upscaling && dsize) {
				SendMessage(hWnd, WM_USER + 0x99, (WPARAM)dsize, (LPARAM)upscaling);
			}
			else {
				UpdateSkinParts();
			}

			HACCEL accel = WASABI_API_LOADACCELERATORSW(IDR_ACCELERATOR_WND);
			if (accel) {
				/*WASABI_API_APP*/plugin.app->app_addAccelerators(hWnd, &accel, 1, TRANSLATE_MODE_NORMAL);
			}
			break;
		}
	case WM_CLOSE:
		// prevent closing the skinned frame from destroying this window
		return 1;
	case WM_DESTROY:
		if (CalcThread) {
			WaitForSingleObjectEx(CalcThread, INFINITE, TRUE);
			if (CalcThread) {
				CloseHandle(CalcThread);
				CalcThread = 0;
			}
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
		DestroyMenu(g_hPopupMenu);
		KillTimer(hWnd, UPDATE_TIMER_ID);
		break;
	case WM_LBUTTONUP:
		// go forwards or backwards through the options depending on the shift state
		if (!(GetKeyState(VK_SHIFT) & 0x1000)) {
			if (config_displaymode>=NXSBCDM_MAX) {
				config_displaymode = 0;
			}
			else {
				++config_displaymode;
			}
		}
		else {
			if (config_displaymode<=0) {
				config_displaymode = NXSBCDM_MAX;
			}
			else {
				--config_displaymode;
			}
		}

		SaveDisplayMode();
		return 0;
	case WM_COMMAND:	// for what's handled from the accel table
		if (ProcessMenuResult(wParam, hWnd))
		{
			break;
		}
	case WM_SYSKEYDOWN:
	case WM_SYSKEYUP:
	case WM_SYSCHAR:
	case WM_KEYDOWN:
	case WM_KEYUP:
	case WM_CHAR:
		{
			// this is needed to avoid it causing a beep
			// due to it not being reported as unhandled
			EatKeyPress();
		}
	case WM_MOUSEWHEEL:
		{
			PostMessage(plugin.hwndParent, uMsg, wParam, lParam);
			break;
		}
	case WM_CONTEXTMENU:
		{
			if ((HWND)wParam!=hWnd) break;

			int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
			if ((x == -1) || (y == -1)) // x and y are -1 if the user invoked a shift-f10 popup menu
			{
				RECT itemRect = { 0 };
				GetWindowRect(hWnd, &itemRect);
				x = itemRect.left;
				y = itemRect.top;
			}

			if (!g_hPopupMenu) {
				g_hPopupMenu = GetSubMenu(WASABI_API_LOADMENUW(IDR_CONTEXTMENU), 0);
			}

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWDISPLAYMODE,
				MF_BYCOMMAND|(config_showdisplaymode?MF_CHECKED:MF_UNCHECKED));

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHADOWEDTEXT,
				MF_BYCOMMAND|(config_shadowtextnew?MF_CHECKED:MF_UNCHECKED));

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_NONE,
				MF_BYCOMMAND|(!config_vismode?MF_CHECKED:MF_UNCHECKED));

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWOSC,
				MF_BYCOMMAND|((config_vismode&NXSBCVM_OSC)==NXSBCVM_OSC?MF_CHECKED:MF_UNCHECKED));

			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWSPEC,
				MF_BYCOMMAND|((config_vismode&NXSBCVM_SPEC)==NXSBCVM_SPEC?MF_CHECKED:MF_UNCHECKED));

			CheckMenuRadioItem(g_hPopupMenu, ID_CONTEXTMENU_DISABLED, ID_CONTEXTMENU_BEATSTIME,
				ID_CONTEXTMENU_DISABLED+config_displaymode, MF_BYCOMMAND|MF_CHECKED);

			const bool not_using_time_of_day = (config_displaymode != NXSBCDM_TIMEOFDAY);
			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_CENTI,
				MF_BYCOMMAND|(config_centi?MF_CHECKED:MF_UNCHECKED));
			EnableMenuItem(g_hPopupMenu, ID_CONTEXTMENU_CENTI,
				!(not_using_time_of_day || (config_timeofdaymode & 1)));
			
			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWSECONDSFORTIMEOFDAY,
				MF_BYCOMMAND|((config_timeofdaymode & 1)?MF_CHECKED:MF_UNCHECKED));
			EnableMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWSECONDSFORTIMEOFDAY, not_using_time_of_day);
			
			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWTIMEOFDAYAS24HOURS,
				MF_BYCOMMAND|((config_timeofdaymode & 2)?MF_CHECKED:MF_UNCHECKED));
			EnableMenuItem(g_hPopupMenu, ID_CONTEXTMENU_SHOWTIMEOFDAYAS24HOURS, not_using_time_of_day);
			
			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_USEADOTASPM,
				MF_BYCOMMAND|((config_timeofdaymode & 4)?MF_CHECKED:MF_UNCHECKED));
			EnableMenuItem(g_hPopupMenu, ID_CONTEXTMENU_USEADOTASPM,
				!(!not_using_time_of_day && !(config_timeofdaymode & 2)));

#ifdef NATIVE_FREEZE
			CheckMenuItem(g_hPopupMenu, ID_CONTEXTMENU_FREEZE,
				MF_BYCOMMAND|(config_freeze?MF_CHECKED:MF_UNCHECKED));
#endif

			CheckMenuRadioItem(g_hPopupMenu, ID_CONTEXTMENU_DISABLED, ID_CONTEXTMENU_BEATSTIME,
				ID_CONTEXTMENU_DISABLED+config_displaymode, MF_BYCOMMAND|MF_CHECKED);
			TrackPopup(g_hPopupMenu, TPM_LEFTBUTTON, x, y, hWnd);
		}
		break;
	case WM_USER+0x99:
		{
			const int old_dsize = dsize;
			dsize = (int)wParam;
			upscaling = (int)lParam;

			// TODO need to improve this...
			if (upscaling)
			{
				if (dsize)
				{
					if (old_dsize && (dsize != old_dsize))
					{
						const int scale = (1 + old_dsize);
						lfDisplay.lfHeight /= scale;
						lfMode.lfHeight /= scale;
					}

					if (!old_dsize || (dsize == old_dsize))
					{
						lfDisplay.lfHeight *= (1 + dsize);
						lfMode.lfHeight *= (1 + dsize);
					}
				}
				else
				{
					if (old_dsize)
					{
						const int scale = (1 + old_dsize);
						lfDisplay.lfHeight /= scale;
						lfMode.lfHeight /= scale;
					}
				}
			}

			UpdateSkinParts();
			break;
		}
	case WM_USER+0x202:	// WM_DISPLAYCHANGE / IPC_SKIN_CHANGED_NEW
		{
			// make sure we catch all appropriate skin changes
			UpdateSkinParts();
		}
		return 0;
	case WM_ERASEBKGND:
		{
			// handled in WM_PAINT
			return 1;
		}
	case WM_PAINT:
		{
			RECT r = {0};
			wchar_t szTime[256] = {0};

			int64_t pos = 0; // The position we display in our big clock
			int dwDisplayMode = 0;

			GetClientRect(hWnd, &r);

			// Create double-buffer
			HDC hdc = CreateCompatibleDC(NULL);
			HDC hdcwnd = GetDC(hWnd);
			HBITMAP hbm = CreateCompatibleBitmap(hdcwnd, r.right, r.bottom);
			ReleaseDC(hWnd, hdcwnd);
			HBITMAP holdbm = (HBITMAP)SelectObject(hdc, hbm);

			// Paint the background
			SetBkColor(hdc, clrBackground);
			SetBkMode(hdc, TRANSPARENT);
			
			FillRectWithColour(hdc, &r, clrBackground, FALSE);

			HFONT holdfont = (HFONT)SelectObject(hdc, hfDisplay);

			switch (config_displaymode) {
				case NXSBCDM_DISABLED:
					dwDisplayMode = 0;
					break;
				case NXSBCDM_ELAPSEDTIME:
					pos = GetCurrentTrackPos();
					dwDisplayMode = IDS_ELAPSED;
					break;
				case NXSBCDM_REMAININGTIME:
					{
						// due to some quirks in how playback can happen
						// it's possible that the earlier attempt to get
						// this failed / not ready so we re-check it now
						if (itemlen <= 0) {
							itemlen = GetCurrentTrackLengthMilliSeconds();
							if (itemlen < 0) {
								itemlen = 0;
							}
						}
						pos = (itemlen - GetCurrentTrackPos());
						dwDisplayMode = IDS_REMAINING;
					}
					break;
				case NXSBCDM_PLELAPSED:
					{
						// Only do this when song has changed, since the code encapsulated by this
						// if clause is very time consuming if the playlist is large.
						if (plpos != prevplpos) {
							prevplpos = plpos;

							// Get combined duration of all songs up to (but not including) the current song
							pltime = 0;
							WASABI_API_LNGSTRINGW_BUF(IDS_CALCULATING, szTime, ARRAYSIZE(szTime));

							if (!CalcThread) {
								CalcThread = StartThread(CalcLengthThread, 0, THREAD_PRIORITY_LOWEST, 0, NULL);
							}
							else {
								resetCalc = 1;
							}
						}

						// Add elapsed time of current song and store result in pos
						pos = (pltime + GetCurrentTrackPos());
						dwDisplayMode = IDS_PLAYLIST_ELAPSED;
					}
					break;
				case NXSBCDM_PLREMAINING:
					{
						// Only do this when song has changed, since the code encapsulated by this
						// if clause is very time consuming if the playlist is large.
						if (plpos != prevplpos) {
							prevplpos = plpos;

							// Get combined duration of all songs from and including the current song to end of list
							pltime = 0;
							WASABI_API_LNGSTRINGW_BUF(IDS_CALCULATING, szTime, ARRAYSIZE(szTime));

							if (!CalcThread) {
								CalcThread = StartThread(CalcLengthThread, (LPVOID)1, THREAD_PRIORITY_LOWEST, 0, NULL);
							}
							else {
								resetCalc = 1;
							}
						}

						// Subtract elapsed time of current song and store result in pos
						// though this will do some rounding as needed to account for it
						// being 'off' if only whole seconds are being shown compared to
						// centiseconds so it should match the classic skin main window.
						pos = (pltime - GetCurrentTrackPos());
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
				case NXSBCDM_BEATSTIME:
					{
						SYSTEMTIME st = {0};
						GetSystemTime(&st);
						// need to convert this to be UTC+1 & account
						// for the wrapping of the time from the UTC
						if (st.wHour == 23)
						{
							st.wHour = 0;
						}
						else
						{
							++st.wHour;
						}

						StringCchPrintf(szTime, ARRAYSIZE(szTime), (config_centi ? L"@%.02f" : L"@%.f"),
										(((st.wHour * 3600) + (st.wMinute * 60) + st.wSecond) * (1 / 86.4f)));
						dwDisplayMode = IDS_BEATS_TIME;
					}
					break;
			}
			
			if (config_displaymode != NXSBCDM_DISABLED) {
				int len = (!szTime[0] ? (int)GetFormattedTime(szTime, ARRAYSIZE(szTime), pos,
						  (config_displaymode == NXSBCDM_TIMEOFDAY) ? 1 : (((config_displaymode ==
						  NXSBCDM_REMAININGTIME) && !config_centi) ? 2 : 0)) : (int)wcslen(szTime));

				if (config_shadowtextnew) {
					// Draw text's "shadow"
					r.left += 5;
					r.top += 5;
					SetTextColor(hdc, clrTimerTextShadow);
					DrawText(hdc, szTime, len, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

					// Draw text
					r.left -= 5;
					r.top -= 5;
				}

				SetTextColor(hdc, clrTimerText);
				DrawText(hdc, szTime, len, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
			}
			else {
				SetTextColor(hdc, clrTimerText);
			}

			SelectObject(hdc, holdfont);

			/* Only draw visualization if Winamp is playing music and one of the visualization modes are on */
			if (is_playing && ((config_vismode & NXSBCVM_OSC) == NXSBCVM_OSC ||
				(config_vismode & NXSBCVM_SPEC) == NXSBCVM_SPEC)) {
				DrawVisualization(hdc, r);
			}

			if (config_showdisplaymode && dwDisplayMode > 0) {
				holdfont = (HFONT)SelectObject(hdc, hfMode);

				static int dwLastDisplayMode = -1;
				static LPWSTR lpszDisplayMode;
				static int lpszDisplayModeLen;

				// so we don't have to keep getting the string we'll
				// try to cache it to reduce the time to draw things
				if (dwLastDisplayMode != dwDisplayMode)
				{
					if (lpszDisplayMode)
					{
						plugin.memmgr->sysFree(lpszDisplayMode);
					}
					lpszDisplayMode = WASABI_API_LNGSTRINGW_DUP(dwDisplayMode);
					lpszDisplayModeLen = (int)wcslen(lpszDisplayMode);

					dwLastDisplayMode = dwDisplayMode;
				}

				SIZE s = {0};
				GetTextExtentPoint32(hdc, lpszDisplayMode, lpszDisplayModeLen, &s);
				TextOut(hdc, 0, r.bottom-s.cy, lpszDisplayMode, lpszDisplayModeLen);

				SelectObject(hdc, holdfont);
			}

			PAINTSTRUCT ps = {0};
			hdcwnd = BeginPaint(hWnd, &ps);

			// Copy double-buffer to screen
			// we use the client area instead of
			// using the paint area as it's not
			// the same if partially off-screen
			BitBlt(hdcwnd, r.left, r.top, r.right, r.bottom, hdc, 0, 0, SRCCOPY);

			EndPaint(hWnd, &ps);

			// Destroy double-buffer
			SelectObject(hdc, holdbm);
			DeleteObject(hbm);
			DeleteDC(hdc);
		}
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void DrawVisualization(HDC hdc, RECT r)
{
	BLENDFUNCTION bf={0,0,80,0};

	static char* (*export_sa_get)(char data[75*2 + 8]);
	static void (*export_sa_setreq)(int);

	/* Get function pointers from Winamp */
	if (!export_sa_get) {
		export_sa_get = (char* (*)(char data[75*2 + 8]))GetSADataFunc(2);
	}
	if (!export_sa_setreq) {
		export_sa_setreq = (void (*)(int))GetSADataFunc(1);
	}

	HDC hdcVis = CreateCompatibleDC(NULL);
	HBITMAP hbmVis = CreateCompatibleBitmap(hdc, r.right, r.bottom);
	HBITMAP holdbmVis = (HBITMAP)SelectObject(hdcVis, hbmVis);

	/* Create the pen for the line drawings */
	HPEN holdpenVis = (HPEN)SelectObject(hdcVis, hpenVis);

	/* Clear background */
	FillRectWithColour(hdcVis, &r, clrBackground, FALSE);

	/* Specify, that we want both spectrum and oscilloscope data */
	if (export_sa_setreq) export_sa_setreq(1); /* Pass 0 (zero) and get spectrum data only */

	static char data[75*2 + 8];
	// no need to query for the vis data if we're paused
	// as it shouldn't be changing & we can use the last
	// vis data that was received for drawing when paused
	const char *sadata = (export_sa_get && !is_paused ? // Visualization data
						  export_sa_get(data) : data);
	if (sadata) {
		/* Render the oscilloscope */
		if ((config_vismode & NXSBCVM_OSC) == NXSBCVM_OSC) {
			// this is not 'exact' but it'll do for the moment
			const int interval = (int)ceil(r.right / 75.0f),
					  top = ((r.bottom) / 2),
					  scaling = (int)ceil((top + 40) / 40.0f);

			// start at the correct initial position so there isn't
			// a partial line drawn from the centre line up to that
			// which can look wrong if looking too closely at it...
			MoveToEx(hdcVis, r.left, (r.top + top) + (sadata[75] * scaling), NULL);
			for (int x = 1; x < 75; x++) {
				LineTo(hdcVis, r.left + (x * interval), (r.top + top) + (sadata[75 + x] * scaling));
			}
		}

		/* Render the spectrum */
		if ((config_vismode & NXSBCVM_SPEC) == NXSBCVM_SPEC) {
			RECT rVis = { 0,0,r.right,r.bottom };
			static char sapeaks[150];
			static char safalloff[150];
			static char sapeaksdec[150];

			// this is not 'exact' but it'll do for the moment
			const int interval = (int)ceil(rVis.right / 75.0f),
					  scaling = (int)ceil((rVis.bottom - 40) / 40.0f);
			for (int x = 0; x < 75; x++)
			{
				if (!is_paused)
				{
					/* Fix peaks & falloff */
					if (sadata[x] > sapeaks[x]) {
						sapeaks[x] = sadata[x];
						sapeaksdec[x] = 0;
					}
					else {
						sapeaks[x] = sapeaks[x] - 1;

						if (sapeaksdec[x] >= 8) {
							sapeaks[x] = (char)(int)(sapeaks[x] - 0.3 * (sapeaksdec[x] - 8));
						}
						if (sapeaks[x] < 0) {
							sapeaks[x] = 0;
						}
						else {
							sapeaksdec[x] = sapeaksdec[x] + 1;
						}
					}

					if (sadata[x] > safalloff[x]) {
						safalloff[x] = sadata[x];
					}
					else {
						safalloff[x] = safalloff[x] - 2;
					}

					/*MoveToEx(hdc, rVis.left+(x*interval), rVis.bottom, NULL);
					LineTo(hdc, rVis.left+(x*interval), rVis.bottom - safalloff[x]);

					// Draw peaks
					MoveToEx(hdc, rVis.left+(x*interval), rVis.bottom - safalloff[x], NULL);
					LineTo(hdc, rVis.left+(x*interval), rVis.bottom - safalloff[x]);*/
				}

				// Draw peaks as bars to better fill the window space whilst
				// not doing a 75px wide image upscaled (which really sucks)
				const RECT peak = {rVis.left+(x*interval)+1, rVis.bottom - (safalloff[x] * scaling),
								   rVis.left+((x+1)*interval)-1, rVis.bottom};
				FillRectWithColour(hdcVis, &peak, clrVisSA, FALSE);
			}
		}
	}

	SelectObject(hdcVis, holdpenVis);

	// Blit to screen
	GdiAlphaBlend(hdc, r.left, r.top, r.right, r.bottom,
				  hdcVis, 0, 0, r.right, r.bottom, bf);

	// Destroy vis bitmap/DC
	SelectObject(hdcVis, holdbmVis);
	DeleteObject(hbmVis);
	DeleteDC(hdcVis);
}

#ifdef NATIVE_FREEZE
LRESULT CALLBACK GenWndSubclass(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
								UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg) {
	case WM_LBUTTONDOWN:
		{
			/*if (!config_freeze) {
				break;
			}*/

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
	return DefSubclass(hwnd, uMsg, wParam, lParam);
}
#endif

extern "C" __declspec (dllexport) winampGeneralPurposePlugin * winampGetGeneralPurposePlugin(void) {
	return &plugin;
}

extern "C" __declspec(dllexport) int winampUninstallPlugin(HINSTANCE hDllInst, HWND hwndDlg, int param)
{
	// prompt to remove our settings with default as no (just incase)
	if (plugin.language->UninstallSettingsPrompt(reinterpret_cast<const wchar_t*>(plugin.description)))
	{
		SaveNativeIniString(WINAMP_INI, PLUGIN_INISECTION, 0, 0);
		no_uninstall = 0;
	}

	// as we're doing too much in subclasses, etc we cannot allow for on-the-fly removal so need to do a normal reboot
	return GEN_PLUGIN_UNINSTALL_REBOOT;
}