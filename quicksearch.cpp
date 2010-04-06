#include <exception>
#include <stdexcept>
#include <iostream>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include <windows.h>

#include <plugin.hpp>

#include "win32Exception.h"

namespace Msg
{
	enum
	{
		Caption,
		SearchForward,
		SearchBackward,
		NotFound,
	};
}

PluginStartupInfo Far;
FARSTANDARDFUNCTIONS Fsf;

void DoMainMenu();
void SearchForward();
void SearchBackward();

wchar_t const *
GetMsg(int msgId)
{
	return Far.GetMsg(Far.ModuleNumber, msgId);
}

extern "C" int WINAPI
GetMinFarVersionW()
{
	return MAKEFARVERSION(2,0,1420);
}

extern "C" void WINAPI
SetStartupInfoW(PluginStartupInfo const * psi)
{
	Far = *psi;
	Fsf = *Far.FSF;
	Far.FSF = &Fsf;
}

extern "C" void WINAPI
GetPluginInfoW(PluginInfo * info)
{
	static wchar_t const * caption = GetMsg(Msg::Caption);
	info->StructSize = sizeof(PluginInfo);
	info->Flags = PF_DISABLEPANELS | PF_EDITOR;
	info->PluginMenuStrings = &caption;
	info->PluginMenuStringsNumber = 1;
}

extern "C" HANDLE WINAPI
OpenPluginW(int OpenFrom, INT_PTR Item)
{
	switch (OpenFrom)
	{
	case OPEN_EDITOR:
		DoMainMenu();
		return INVALID_HANDLE_VALUE;
	default:
		return INVALID_HANDLE_VALUE;
	}
}

void
DoMainMenu()
{
	try
	{
		FarMenuItemEx items[2] = { 0 };
	
		items[0].Text = GetMsg(Msg::SearchForward);
		items[1].Text = GetMsg(Msg::SearchBackward);
	
		switch (Far.Menu(Far.ModuleNumber, -1, -1, 0, FMENU_USEEXT | FMENU_WRAPMODE, 0, 0, 0, 0, 0,
			reinterpret_cast<FarMenuItem const *>(items), 2))
		{
		case -1: return;
		case 0: SearchForward(); return;
		case 1: SearchBackward(); return;
		}
	}
	catch (std::exception const & e)
	{
		std::wostringstream oss;
		oss << L"\n" << e.what();
		Far.Message(Far.ModuleNumber, FMSG_WARNING | FMSG_ALLINONE | FMSG_MB_OK, 0,
			reinterpret_cast<wchar_t const * const *>(oss.str().c_str()), 0, 0);
	}
}

class QuickSearch
{
private:
	HANDLE hConIn_;

	bool running_;
	void Exit() { running_ = false; }

	std::wstring pattern_;
	void SearchAgain();
	void ShowPattern();
	void NotFound();

	EditorInfo saveInfo_;
	EditorSelect saveBlock_;
	void SaveInfo();
	void RestorePos();
	void RestoreBlock();
	void Unselect();

	bool ProcessInput(INPUT_RECORD const & input);
	bool ProcessKey(KEY_EVENT_RECORD const & key);
	bool ProcessMouse(MOUSE_EVENT_RECORD const & mouse);

	bool IsCharKey(KEY_EVENT_RECORD const & key) const;
public:
	QuickSearch();

	void Run();
};

QuickSearch::QuickSearch()
	: hConIn_(win32::check_handle(GetStdHandle(STD_INPUT_HANDLE)))
{
	SaveInfo();
	
	Unselect();
}

void
QuickSearch::SaveInfo()
{
	Far.EditorControl(ECTL_GETINFO, &saveInfo_);
	saveBlock_.BlockType = saveInfo_.BlockType;
	if (saveBlock_.BlockType == BTYPE_NONE) return;

	saveBlock_.BlockStartLine = saveInfo_.BlockStartLine;

	EditorSetPosition esp = { saveInfo_.BlockStartLine, -1, 0, -1, -1, -1 };
	Far.EditorControl(ECTL_SETPOSITION, &esp);

	EditorGetString egs = { -1 };
	Far.EditorControl(ECTL_GETSTRING, &egs);
	saveBlock_.BlockStartPos = egs.SelStart;

	while (egs.SelEnd == -1)
	{
		EditorSetPosition esp = { egs.StringNumber + 1, -1, 0, -1, -1, -1 };
		if (!Far.EditorControl(ECTL_SETPOSITION, &esp)) break;

		Far.EditorControl(ECTL_GETSTRING, &egs);
	}

	saveBlock_.BlockHeight = egs.StringNumber - saveInfo_.BlockStartLine + 1;
	saveBlock_.BlockWidth = egs.SelEnd - saveBlock_.BlockStartPos;
}

void
QuickSearch::RestorePos()
{
	EditorSetPosition esp = { saveInfo_.CurLine, -1, saveInfo_.CurTabPos, saveInfo_.TopScreenLine, saveInfo_.LeftPos, -1 };
	Far.EditorControl(ECTL_SETPOSITION, &esp);
}

void
QuickSearch::RestoreBlock()
{
	Far.EditorControl(ECTL_SELECT, &saveBlock_);
}

void
QuickSearch::Unselect()
{
	EditorSelect esel = { BTYPE_NONE };
	Far.EditorControl(ECTL_SELECT, &esel);
}

void
QuickSearch::Run()
{
	ShowPattern();
	running_ = true;
	while (running_)
	{
		win32::check(WaitForSingleObject(hConIn_, INFINITE), WAIT_FAILED);
	
		INPUT_RECORD input; DWORD eventsRead;
		win32::check(PeekConsoleInput(hConIn_, &input, 1, &eventsRead));
		
		if (!ProcessInput(input)) return;

		ReadConsoleInput(hConIn_, &input, 1, &eventsRead);
	}
}

bool
QuickSearch::ProcessInput(INPUT_RECORD const & input)
{
	switch (input.EventType)
	{
	case KEY_EVENT:
		return ProcessKey(input.Event.KeyEvent);
	case MOUSE_EVENT:
		return ProcessMouse(input.Event.MouseEvent);
	default:
		return true; // ignore and consume all other events
	}
}

bool
QuickSearch::ProcessKey(KEY_EVENT_RECORD const & key)
{
	if (!key.bKeyDown) return true;

	if (IsCharKey(key))
	{
		pattern_ += key.uChar.UnicodeChar;
		SearchAgain();
		return true;
	}

	static DWORD const SHIFT_MASK = SHIFT_PRESSED
		| LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED
		| LEFT_ALT_PRESSED  | RIGHT_ALT_PRESSED;

	if (key.wVirtualKeyCode == VK_BACK && (key.dwControlKeyState & SHIFT_MASK) == 0)
	{
		pattern_.resize(pattern_.size() - 1);
		SearchAgain();
		return true;
	}

	if (key.wVirtualKeyCode == VK_ESCAPE && (key.dwControlKeyState & SHIFT_MASK) == 0)
	{
		RestoreBlock();
		RestorePos();
		Exit();
		return true;
	}

	if (key.wVirtualKeyCode == VK_RETURN && (key.dwControlKeyState & SHIFT_MASK) == 0)
	{
		Exit();
		return true;
	}

	return false;
}

bool
QuickSearch::ProcessMouse(MOUSE_EVENT_RECORD const & mouse)
{
	return mouse.dwButtonState == 0; // as long as they don't click anything, ignore and consume
}

bool
QuickSearch::IsCharKey(KEY_EVENT_RECORD const & key) const
{
	DWORD ctrlAlt = key.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED | LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED);
	return key.uChar.UnicodeChar >= L' '
		&& (ctrlAlt == 0 ||	ctrlAlt == (LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED));
}

void
QuickSearch::SearchAgain()
{
	ShowPattern();
	RestorePos();

	for (int start = saveInfo_.CurTabPos;; start = 0)
	{
		EditorGetString egs = { -1 };
		Far.EditorControl(ECTL_GETSTRING, &egs);


#if 0
		// Vista and above :(
		int foundLength = 0, foundPos = FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART
			| NORM_IGNORECASE
			| NORM_IGNOREKANATYPE
			| NORM_IGNORENONSPACE
			| NORM_IGNOREWIDTH
			| NORM_LINGUISTIC_CASING,
			egs.StringText + start,
			egs.StringLength - start,
			pattern_.c_str(),
			pattern_.size(),
			&foundLength,
			0, 0, 0);
#else
		wchar_t const * begin = egs.StringText + start;
		wchar_t const * end = egs.StringText + egs.StringLength;
		wchar_t const * found = std::search(begin, end,
			pattern_.begin(), pattern_.end());
		int foundPos = found == end ? -1 : found - begin;
		int foundLength = pattern_.size();
#endif

		if (foundPos >= 0)
		{
			EditorSelect esel = { BTYPE_STREAM, -1, start + foundPos, foundLength, 1 };
			Far.EditorControl(ECTL_SELECT, &esel);

			EditorSetPosition esp = { -1, -1, start + foundPos + foundLength, -1, -1, -1 };
			Far.EditorControl(ECTL_SETPOSITION, &esp);

			Far.EditorControl(ECTL_REDRAW, 0);
			return;
		}
	
		EditorSetPosition esp = { egs.StringNumber + 1, -1, -1, -1, -1, -1 };
		if (!Far.EditorControl(ECTL_SETPOSITION, &esp))
		{
			RestorePos();
			NotFound();
			return;
		}
	}
}

void
QuickSearch::ShowPattern()
{
	std::wostringstream oss;
	oss << L"/" << pattern_;
	Far.EditorControl(ECTL_SETTITLE, const_cast<wchar_t*>(oss.str().c_str()));
	Far.EditorControl(ECTL_REDRAW, 0);
}

void
QuickSearch::NotFound()
{
	std::wostringstream oss;
	oss << L"/" << pattern_ << GetMsg(Msg::NotFound);
	Far.EditorControl(ECTL_SETTITLE, const_cast<wchar_t*>(oss.str().c_str()));
	Far.EditorControl(ECTL_REDRAW, 0);
}

void
SearchForward()
{
	QuickSearch qs;
	qs.Run();
}

void SearchBackward()
{

}

