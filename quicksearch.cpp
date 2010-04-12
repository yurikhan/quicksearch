#include <exception>
#include <stdexcept>
#include <iostream>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <functional>

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

void Start(bool backward);
void DoMainMenu();

wchar_t const *
GetMsg(int msgId)
{
	return Far.GetMsg(Far.ModuleNumber, msgId);
}

void
DoMainMenu()
{
	try
	{
		FarMenuItemEx items[2] = { 0 };

		items[0].Text = GetMsg(Msg::SearchForward);
		items[1].Text = GetMsg(Msg::SearchBackward);

		switch (Far.Menu(Far.ModuleNumber, -1, -1, 0, FMENU_USEEXT | FMENU_WRAPMODE, 0, 0, L"Contents", 0, 0,
			reinterpret_cast<FarMenuItem const *>(items), 2))
		{
		case -1: return;
		case 0: Start(false); return;
		case 1: Start(true); return;
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

class SaveBlockAndPos
{
private:
	EditorInfo saveInfo_;
	EditorSelect saveBlock_;
public:
	void SaveInfo();
	void RestoreBlock();
	void RestorePos();
	void RestoreAll();

	int StartLine() const;
	int StartPos() const;
	int CurPos() const { return saveInfo_.CurPos; }

	int BlockType() const { return saveInfo_.BlockType; }
};

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

bool operator<(Found const & lhs, Found const & rhs)
{
	return 	lhs.line   < rhs.line   || lhs.line == rhs.line
		&&(	lhs.pos    < rhs.pos    || lhs.pos  == rhs.pos
		&&(	lhs.length < rhs.length ));
}
bool operator==(Found const & lhs, Found const & rhs)
{
	return lhs.line   == rhs.line
		&& lhs.pos    == rhs.pos
		&& lhs.length == rhs.length;
}
using std::operator>;
using std::operator<=;
using std::operator>=;
using std::operator!=;

class QuickSearch
{
private:

	bool backward_;

	HANDLE hConIn_;

	bool running_;
	void Exit() { running_ = false; }

	std::wstring patterns_[2];
	size_t activePattern_;
	Found found_[2];
	Found FindPattern(std::wstring const & pattern, int startPos, bool backward = false);
	Found FindPatternForward(std::wstring const & pattern, int startPos);
	Found FindPatternBackward(std::wstring const & pattern, int startPos);
	void SearchAgain();
	void FindNext(bool backward, int startPos);
	void ShowPattern(wchar_t const * message = 0);
	void NotFound();

	SaveBlockAndPos save_;
	void SaveInfo();
	void RestorePos() { save_.RestorePos(); }
	void RestoreBlock() { save_.RestoreBlock(); }
	void RestoreAll() { save_.RestoreAll(); }
	void Unselect();

	bool ProcessInput(INPUT_RECORD const & input);
	bool ProcessKey(KEY_EVENT_RECORD const & key);
	bool ProcessMouse(MOUSE_EVENT_RECORD const & mouse);

	bool IsModifierKey(KEY_EVENT_RECORD const & key) const;
	bool IsCharKey(KEY_EVENT_RECORD const & key) const;
public:
	explicit QuickSearch(bool backward);

	void Run();
};

class ReadClipboard
{
private:
	wchar_t const * buffer_;
public:
	ReadClipboard()	: buffer_(Fsf.PasteFromClipboard()) {}
	~ReadClipboard() { Fsf.DeleteBuffer(const_cast<wchar_t *>(buffer_)); }

	wchar_t const * c_str() { return buffer_; }
};

/*explicit*/
QuickSearch::QuickSearch(bool backward)
	: hConIn_(win32::check_handle(GetStdHandle(STD_INPUT_HANDLE))), activePattern_(0), backward_(backward)
{
	SaveInfo();
}

int
SaveBlockAndPos::StartLine() const
{
	return saveBlock_.BlockType == BTYPE_NONE ? saveInfo_.CurLine : saveBlock_.BlockStartLine;
}

int
SaveBlockAndPos::StartPos() const
{
	return saveBlock_.BlockType == BTYPE_NONE ? saveInfo_.CurPos : saveBlock_.BlockStartPos;
}

void
SaveBlockAndPos::SaveInfo()
{
	Far.EditorControl(ECTL_GETINFO, &saveInfo_);
	saveBlock_.BlockType = saveInfo_.BlockType;

	if (saveBlock_.BlockType != BTYPE_NONE)
	{
		saveBlock_.BlockStartLine = saveInfo_.BlockStartLine;

		EditorSetPosition esp = { saveInfo_.BlockStartLine, -1, -1, -1, -1, -1 };
		Far.EditorControl(ECTL_SETPOSITION, &esp);

		EditorGetString egs = { -1 };
		Far.EditorControl(ECTL_GETSTRING, &egs);
		saveBlock_.BlockStartPos = egs.SelStart;

		int line = saveInfo_.CurLine;
		while (egs.SelEnd == -1)
		{
			if (++line >= saveInfo_.TotalLines) break;

			EditorSetPosition esp = { line, -1, -1, -1, -1, -1 };
			Far.EditorControl(ECTL_SETPOSITION, &esp);

			Far.EditorControl(ECTL_GETSTRING, &egs);
		}

		saveBlock_.BlockHeight = line - saveInfo_.BlockStartLine + 1;
		saveBlock_.BlockWidth = egs.SelEnd - saveBlock_.BlockStartPos;

		RestorePos();
	}
}

void
QuickSearch::SaveInfo()
{
	save_.SaveInfo();

	if (!found_[0])
	{
		found_[0] = Found(save_.StartLine(), save_.StartPos(), 0);
	}
}

void
SaveBlockAndPos::RestorePos()
{
	EditorSetPosition esp = { saveInfo_.CurLine, saveInfo_.CurPos, -1, saveInfo_.TopScreenLine, saveInfo_.LeftPos, -1 };
	Far.EditorControl(ECTL_SETPOSITION, &esp);
}

void
SaveBlockAndPos::RestoreBlock()
{
	Far.EditorControl(ECTL_SELECT, &saveBlock_);
}

void
SaveBlockAndPos::RestoreAll()
{
	RestoreBlock();
	RestorePos();
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

	if (IsModifierKey(key)) return true;

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

	if (key.wVirtualKeyCode == VK_INSERT && shifts == SHIFT_PRESSED
	||  key.wVirtualKeyCode == L'V'      &&  (shifts & (LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))
										 && !(shifts &~(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED)))
	{
		ReadClipboard clip;
		for (wchar_t const * pc = clip.c_str(); *pc; ++pc)
		{
			patterns_[activePattern_] += *pc;
			SearchAgain();
		}
		return true;
	}

	if (key.wVirtualKeyCode == VK_BACK && shifts == 0)
	{
		if (patterns_[activePattern_].empty()) return true;
		patterns_[activePattern_].resize(patterns_[activePattern_].size() - 1);
		SearchAgain();
		return true;
	}

	if (key.wVirtualKeyCode == VK_ESCAPE && shifts == 0)
	{
		RestoreAll();
		Exit();
		return true;
	}

	if (key.wVirtualKeyCode == VK_RETURN && shifts == 0)
	{
		Exit();
		return true;
	}

	if (key.wVirtualKeyCode == VK_TAB && shifts == 0)
	{
		switch (activePattern_)
		{
		case 0:
			SaveInfo();
			activePattern_ = 1;
			break;
		case 1:
			RestoreAll();
			patterns_[1].clear();
			activePattern_ = 0;
			break;
		}
		ShowPattern();
		return true;
	}

	if (key.wVirtualKeyCode == VK_F1 && shifts == 0)
	{
		INPUT_RECORD input;
		DWORD eventsRead;
		ReadConsoleInput(hConIn_, &input, 1, &eventsRead);
		Far.ShowHelp(Far.ModuleName, 0, FHELP_SELFHELP);
		ShowPattern();
		return true;
	}

	if (key.wVirtualKeyCode == VK_F3 && shifts == 0)
	{
		FindNext(false, found_[activePattern_].pos + found_[activePattern_].length);
		return true;
	}
	if (key.wVirtualKeyCode == VK_F3 && shifts == SHIFT_PRESSED)
	{
		FindNext(true, found_[activePattern_].pos);
		return true;
	}

	return false;
}

bool
QuickSearch::ProcessMouse(MOUSE_EVENT_RECORD const & mouse)
{
	return mouse.dwButtonState == 0; // as long as they don't click anything, ignore and consume
}

template <typename InIter, typename T> bool
exist(InIter begin, InIter end, T const & value)
{
	return end != std::find(begin, end, value);
}

template <typename T, size_t n> bool
exist(T const (&ar)[n], T const & value)
{
	return exist(ar + 0, ar + n, value);
}

bool
QuickSearch::IsModifierKey(KEY_EVENT_RECORD const & key) const
{
	static WORD const modifierKeys[] =
	{
		VK_LSHIFT,   VK_RSHIFT,   VK_SHIFT,
		VK_LCONTROL, VK_RCONTROL, VK_CONTROL,
		VK_LMENU,    VK_RMENU,    VK_MENU,
		VK_LWIN,     VK_RWIN,
		VK_NUMLOCK,  VK_CAPITAL,  VK_SCROLL,
	};

	return exist(modifierKeys, key.wVirtualKeyCode);
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
	FindNext(backward_ && activePattern_ == 0, save_.CurPos());
}

void
QuickSearch::FindNext(bool backward, int startPos)
{
	SaveBlockAndPos save;
	save.SaveInfo();

	Found next = FindPattern(patterns_[activePattern_], startPos, backward);
	if (!next || activePattern_ == 1 && next < found_[0])
	{
		save.RestoreAll();
		NotFound();
		return;
	}
	found_[activePattern_] = next;
	EditorSelect esel = { save_.BlockType() == BTYPE_NONE ? BTYPE_STREAM : save_.BlockType(),
		found_[0].line, found_[0].pos,
		found_[activePattern_].pos + found_[activePattern_].length - found_[0].pos,
		found_[activePattern_].line - found_[0].line + 1 };
	Far.EditorControl(ECTL_SELECT, &esel);

	EditorSetPosition esp = { found_[activePattern_].line,
		found_[activePattern_].pos + found_[activePattern_].length, -1, -1, -1, -1 };
	Far.EditorControl(ECTL_SETPOSITION, &esp);

	Far.EditorControl(ECTL_REDRAW, 0);
}

Found
QuickSearch::FindPattern(std::wstring const & pattern, int startPos, bool backward)
{
	return backward ? FindPatternBackward(pattern, startPos) : FindPatternForward(pattern, startPos);
}

class case_equivalent : public std::binary_function<wchar_t, wchar_t, bool>
{
public:
	bool operator()(wchar_t lhs, wchar_t rhs) const
	{
		return CSTR_EQUAL == win32::check(CompareString(LOCALE_USER_DEFAULT,
			NORM_IGNORECASE | NORM_IGNOREKANATYPE |
			NORM_IGNORENONSPACE | NORM_IGNOREWIDTH,
			&lhs, 1,
			&rhs, 1));
	}
};

Found
QuickSearch::FindPatternForward(std::wstring const & pattern, int startPos)
{
	EditorInfo einfo;
	Far.EditorControl(ECTL_GETINFO, &einfo);

	for (int start = startPos;; start = 0)
	{
		EditorGetString egs = { -1 };
		Far.EditorControl(ECTL_GETSTRING, &egs);

		wchar_t const * begin = egs.StringText + start;
		wchar_t const * end = egs.StringText + egs.StringLength;
		wchar_t const * found = std::search(begin, end,
			pattern.begin(), pattern.end(),
			case_equivalent());
		int foundPos = found == end ? -1 : static_cast<int>(found - begin);
		int foundLength = static_cast<int>(pattern.size());

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
Found
QuickSearch::FindPatternBackward(std::wstring const & pattern, int startPos)
{
	EditorInfo einfo;
	Far.EditorControl(ECTL_GETINFO, &einfo);

	EditorGetString egs = { -1 };
	Far.EditorControl(ECTL_GETSTRING, &egs);

	for (int start = egs.StringLength - startPos;; start = 0)
	{
		std::reverse_iterator<wchar_t const *>
			begin(egs.StringText + egs.StringLength - start),
			end(egs.StringText),
			found(std::search(begin, end,
				pattern.rbegin(), pattern.rend(),
				case_equivalent()));
		int foundPos = found == end ? -1 : static_cast<int>(end - found - pattern.size());
		int foundLength = static_cast<int>(pattern.size());

		if (foundPos >= 0)
		{
			return Found(einfo.CurLine, foundPos, foundLength);
		}

	    --einfo.CurLine;
	    if (einfo.CurLine < 0 || einfo.CurLine >= einfo.TotalLines)
	    {
			return Found();
	    }
		EditorSetPosition esp = { einfo.CurLine, -1, -1, -1, -1, -1 };
		Far.EditorControl(ECTL_SETPOSITION, &esp);

		egs.StringNumber = -1;
		Far.EditorControl(ECTL_GETSTRING, &egs);
	}
}

void
QuickSearch::ShowPattern(wchar_t const * message /*= 0*/)
{
	std::wostringstream oss;
	oss << L"/" << patterns_[0];
	if (activePattern_ == 1)
	{
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
Start(bool backward)
{
	QuickSearch qs(backward);
	qs.Run();
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

