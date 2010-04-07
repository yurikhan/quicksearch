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
	struct Found
	{
		typedef bool (Found::* unspecified_bool)() const;

		int line;
		int pos;
		int length;
	public:
		Found() : line(-1), pos(-1), length(-1) {}
		Found(int line, int pos, int length) : line(line), pos(pos), length(length) {}
		operator unspecified_bool() const
		{
			if (!*this) return 0;
			return &Found::operator!;
		}
		bool operator!() const
		{
			return line == -1;
		}
	};

	HANDLE hConIn_;

	bool running_;
	void Exit() { running_ = false; }

	std::wstring patterns_[2];
	size_t activePattern_;
	Found FindPattern(std::wstring const & pattern);
	void SearchAgain();
	void ShowPattern(wchar_t const * message = 0);
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
	: hConIn_(win32::check_handle(GetStdHandle(STD_INPUT_HANDLE))), activePattern_(0)
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

	static DWORD const SHIFT_MASK = SHIFT_PRESSED
		| LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED
		| LEFT_ALT_PRESSED  | RIGHT_ALT_PRESSED;

	DWORD const shifts = key.dwControlKeyState & SHIFT_MASK;

	if (IsCharKey(key))
	{
		patterns_[activePattern_] += key.uChar.UnicodeChar;
		SearchAgain();
		return true;
	}

	if (key.wVirtualKeyCode == VK_BACK && shifts == 0)
	{
		patterns_[activePattern_].resize(patterns_[activePattern_].size() - 1);
		SearchAgain();
		return true;
	}

	if (key.wVirtualKeyCode == VK_ESCAPE && shifts == 0)
	{
		RestoreBlock();
		RestorePos();
		Exit();
		return true;
	}

	if (key.wVirtualKeyCode == VK_RETURN && shifts == 0)
	{
		Exit();
		return true;
	}

	if (key.wVirtualKeyCode == VK_TAB && (shifts &~ SHIFT_PRESSED) == 0)
	{
		activePattern_ ^= 1;
		ShowPattern();
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

	Found foundStart = FindPattern(patterns_[0]);
	if (!foundStart)
	{
		RestorePos();
		NotFound();
		return;
	}
	EditorSelect esel = { BTYPE_STREAM, foundStart.line, foundStart.pos, foundStart.length, 1 };
	Far.EditorControl(ECTL_SELECT, &esel);

	EditorSetPosition esp = { foundStart.line, -1, foundStart.pos + foundStart.length, -1, -1, -1 };
	Far.EditorControl(ECTL_SETPOSITION, &esp);

	if (!patterns_[1].empty())
	{
		Found foundEnd = FindPattern(patterns_[1]);
		if (!foundEnd)
		{
			Far.EditorControl(ECTL_SETPOSITION, &esp);
			NotFound();
			return;
		}

		std::wostringstream oss;
		esel.BlockWidth = foundEnd.pos + foundEnd.length - foundStart.pos;
		esel.BlockHeight = foundEnd.line - foundStart.line + 1;
		Far.EditorControl(ECTL_SELECT, &esel);

		EditorSetPosition esp = { foundEnd.line, -1, foundEnd.pos + foundEnd.length, -1, -1, -1 };
		Far.EditorControl(ECTL_SETPOSITION, &esp);
	}

	Far.EditorControl(ECTL_REDRAW, 0);
}

QuickSearch::Found
QuickSearch::FindPattern(std::wstring const & pattern)
{
	EditorInfo einfo;
	Far.EditorControl(ECTL_GETINFO, &einfo);

	for (int start = einfo.CurTabPos;; start = 0)
	{
		EditorGetString egs = { -1 };
		Far.EditorControl(ECTL_GETSTRING, &egs);

		wchar_t const * begin = egs.StringText + start;
		wchar_t const * end = egs.StringText + egs.StringLength;
		wchar_t const * found = std::search(begin, end,
			pattern.begin(), pattern.end());
		int foundPos = found == end ? -1 : found - begin;
		int foundLength = pattern.size();

		if (foundPos >= 0)
		{
			return Found(einfo.CurLine, start + foundPos, foundLength);
		}

	    ++einfo.CurLine;
	    if (einfo.CurLine >= einfo.TotalLines)
	    {
			return Found();
	    }
		EditorSetPosition esp = { einfo.CurLine, -1, -1, -1, -1, -1 };
		Far.EditorControl(ECTL_SETPOSITION, &esp);
	}
}

void
QuickSearch::ShowPattern(wchar_t const * message /*= 0*/)
{
	std::wostringstream oss;
	oss << L"/" << patterns_[0];
	if (!patterns_[1].empty() || activePattern_ == 1)
	{
		if (activePattern_ == 0) oss << L"_";
		oss << wchar_t(0x2026) << patterns_[1];
	}
	if (message) oss << message;
	Far.EditorControl(ECTL_SETTITLE, const_cast<wchar_t*>(oss.str().c_str()));
	Far.EditorControl(ECTL_REDRAW, 0);
}

void
QuickSearch::NotFound()
{
	ShowPattern(GetMsg(Msg::NotFound));
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

