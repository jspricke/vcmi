/*
* NotificationHandler.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/

#include "StdInc.h"
#include "NotificationHandler.h"

#ifdef VCMI_WINDOWS


// Modify the following defines if you have to target a platform prior to the ones specified below.
// Refer to MSDN for the latest info on corresponding values for different platforms.
#ifndef WINVER				// Allow use of features specific to Windows XP or later.
#define WINVER 0x0501		// Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINNT		// Allow use of features specific to Windows XP or later.                   
#define _WIN32_WINNT 0x0501	// Change this to the appropriate value to target other versions of Windows.
#endif						

#ifndef _WIN32_WINDOWS		// Allow use of features specific to Windows 98 or later.
#define _WIN32_WINDOWS 0x0410 // Change this to the appropriate value to target Windows Me or later.
#endif

#ifndef _WIN32_IE			// Allow use of features specific to IE 6.0 or later.
#define _WIN32_IE 0x0600	// Change this to the appropriate value to target other versions of IE.
#endif

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>
#include <shellapi.h>
#include <winuser.h>
// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#define	WM_USER_SHELLICON WM_USER + 1


// Global Variables:
HINSTANCE		hInst;	// current instance
NOTIFYICONDATA	niData;	// notify icon data
HWND hWnd;
bool initialized = false;

void NotificationHandler::notify(std::string msg)
{
	if(hWnd == GetForegroundWindow())
		return;

	ZeroMemory(&niData, sizeof(NOTIFYICONDATA));

	niData.cbSize = sizeof(NOTIFYICONDATA);
	niData.hWnd = (HWND)hWnd;
	niData.uID = 1;
	niData.uFlags = NIF_INFO;

	niData.dwInfoFlags = NIIF_INFO;
	msg.copy(niData.szInfo, msg.length());

	Shell_NotifyIcon(NIM_MODIFY, &niData);
}

void NotificationHandler::init()
{
	if(initialized)
		return;

	ZeroMemory(&niData, sizeof(NOTIFYICONDATA));

	hWnd = FindWindow("SDL_app", NULL);
	hInst = (HINSTANCE)GetModuleHandle("VCMI_client.exe");

	niData.cbSize = sizeof(NOTIFYICONDATA);
	niData.hWnd = (HWND)hWnd;
	niData.uID = 1;
	niData.uFlags = NIF_ICON;

	niData.hIcon = (HICON)LoadImage(
		hInst, 
		"IDI_ICON1",
		IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON),
		GetSystemMetrics(SM_CYSMICON),
		LR_DEFAULTSIZE);

	Shell_NotifyIcon(NIM_ADD, &niData);

	initialized = true;
}
#else

void NotificationHandler::notify(std::string msg)
{
}

void NotificationHandler::init()
{
}

#endif