#include <exception>
#include <stdexcept>
#include <iostream>
#include <ostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <map>
#include <limits>

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
		AlreadyActive,
	};
}

PluginStartupInfo Far;
FARSTANDARDFUNCTIONS Fsf;

wchar_t const *
GetMsg(int msgId)
{
	return Far.GetMsg(Far.ModuleNumber, msgId);
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

	typedef std::map<int/*EditorID=>*/, QuickSearch> Instances;
	static Instances instances_;

	int id_;
	bool backward_;
	bool firstTime_;
	wchar_t const * message_;

	void Exit();

	std::wstring patterns_[2];
	size_t activePattern_;
	Found found_[2];
	Found FindPattern(std::wstring const & pattern, int startPos, bool backward = false);
	Found FindPatternForward(std::wstring const & pattern, int startPos);
	Found FindPatternBackward(std::wstring const & pattern, int startPos);
	void SearchAgain();
	void FindNext(bool backward, int startPos);
	void ShowPattern(wchar_t const * message = 0);
	void DeferShowPattern(wchar_t const * message = 0) { firstTime_ = true; message_ = message; }
	void NotFound();

	SaveBlockAndPos save_[2];
	void Unselect();
	void SelectFound();

	bool ProcessInput(INPUT_RECORD const & input);
	bool ProcessKey(KEY_EVENT_RECORD const & key);
	bool ProcessMouse(MOUSE_EVENT_RECORD const & mouse);

	bool IsModifierKey(KEY_EVENT_RECORD const & key) const;
	bool IsCharKey(KEY_EVENT_RECORD const & key) const;
public:
	static void DoMainMenu();
	static void Start(int editorID, bool backward);
	static int ProcessEditorInput(INPUT_RECORD const & input);

	explicit QuickSearch(int editorID, bool backward);

	void Run();
};

/*static*/ QuickSearch::Instances
QuickSearch::instances_;

class ReadClipboard
{
private:
	wchar_t const * buffer_;
public:
	ReadClipboard()	: buffer_(Fsf.PasteFromClipboard()) {}
	~ReadClipboard() { Fsf.DeleteBuffer(const_cast<wchar_t *>(buffer_)); }

	wchar_t const * c_str() { return buffer_; }
};

/*static*/ void
QuickSearch::DoMainMenu()
{
	try
	{
		EditorInfo einfo;
		Far.EditorControl(ECTL_GETINFO, &einfo);

		FarMenuItemEx items[2] = { 0 };

		items[0].Text = GetMsg(Msg::SearchForward);
		items[1].Text = GetMsg(Msg::SearchBackward);

		switch (Far.Menu(Far.ModuleNumber, -1, -1, 0, FMENU_USEEXT | FMENU_WRAPMODE, 0, 0, L"Contents", 0, 0,
			reinterpret_cast<FarMenuItem const *>(items), 2))
		{
		case -1: return;
		case 0: Start(einfo.EditorID, false); return;
		case 1: Start(einfo.EditorID, true); return;
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

/*explicit*/
QuickSearch::QuickSearch(int editorID, bool backward)
	: id_(editorID), activePattern_(0), backward_(backward)
{
	save_[0].SaveInfo();

	found_[0] = Found(save_[0].StartLine(), save_[0].StartPos(), 0);
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
	firstTime_ = true;
	message_ = 0;
}

void
QuickSearch::Exit()
{
	Far.EditorControl(ECTL_SETTITLE, 0);
	instances_.erase(id_);
	Far.EditorControl(ECTL_REDRAW, 0);
}

bool
QuickSearch::ProcessInput(INPUT_RECORD const & input)
{
	if (firstTime_)
	{
		firstTime_ = false;
		ShowPattern(message_);
	}
	switch (input.EventType)
	{
	case KEY_EVENT:
	case FARMACRO_KEY_EVENT:
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
		for (wchar_t const * pc = clip.c_str(); pc && *pc; ++pc)
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
		save_[0].RestoreAll();
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
			save_[1].SaveInfo();
			activePattern_ = 1;
			break;
		case 1:
			patterns_[1].clear();
			activePattern_ = 0;
			SelectFound();
			break;
		}
		ShowPattern();
		return true;
	}

	if (key.wVirtualKeyCode == VK_F1 && shifts == 0)
	{
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

	Exit();
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
	save_[activePattern_].RestorePos();
	FindNext(backward_ && activePattern_ == 0, save_[activePattern_].CurPos());
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
	}
	else
	{
		found_[activePattern_] = next;
	}
	SelectFound();

	Far.EditorControl(ECTL_REDRAW, 0);
}

void
QuickSearch::SelectFound()
{
	EditorSelect esel = {
		save_[activePattern_].BlockType() == BTYPE_NONE ? BTYPE_STREAM : save_[activePattern_].BlockType(),
		found_[0].line, found_[0].pos,
		found_[activePattern_].pos + found_[activePattern_].length - found_[0].pos,
		found_[activePattern_].line - found_[0].line + 1 };
	Far.EditorControl(ECTL_SELECT, &esel);

	EditorSetPosition esp = { found_[activePattern_].line,
		found_[activePattern_].pos + found_[activePattern_].length, -1, -1, -1, -1 };
	Far.EditorControl(ECTL_SETPOSITION, &esp);
}

Found
QuickSearch::FindPattern(std::wstring const & pattern, int startPos, bool backward)
{
	return backward ? FindPatternBackward(pattern, startPos) : FindPatternForward(pattern, startPos);
}

int
int_from_size(size_t size)
{
	return static_cast<int>((std::min)(size, size_t((std::numeric_limits<int>::max)())));
}

void
to_upper(wchar_t const * in, int cchIn, std::vector<wchar_t> & out)
{
	out.resize(cchIn);

	// LCMapString blows up if cchIn is 0
	if (cchIn == 0) return;

	win32::check(LCMapString(LOCALE_USER_DEFAULT,
		LCMAP_LINGUISTIC_CASING | LCMAP_UPPERCASE,
		in, cchIn,
		&out[0], cchIn));
}

Found
QuickSearch::FindPatternForward(std::wstring const & pattern, int startPos)
{
	std::vector<wchar_t> uc_pattern;
	to_upper(pattern.c_str(), int_from_size(pattern.size()), uc_pattern);

	EditorInfo einfo;
	Far.EditorControl(ECTL_GETINFO, &einfo);

	for (int start = startPos;; start = 0)
	{
		EditorGetString egs = { -1 };
		Far.EditorControl(ECTL_GETSTRING, &egs);

		std::vector<wchar_t> uc_line;
		to_upper(egs.StringText + start, egs.StringLength - start, uc_line);

		std::vector<wchar_t>::iterator found = std::search(uc_line.begin(), uc_line.end(),
			uc_pattern.begin(), uc_pattern.end());
		int foundPos = found == uc_line.end() ? -1 : static_cast<int>(found - uc_line.begin());
		int foundLength = static_cast<int>(uc_pattern.size());

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
	std::vector<wchar_t> uc_pattern;
	to_upper(pattern.c_str(), int_from_size(pattern.size()), uc_pattern);

	EditorInfo einfo;
	Far.EditorControl(ECTL_GETINFO, &einfo);

	EditorGetString egs = { -1 };
	Far.EditorControl(ECTL_GETSTRING, &egs);

	for (int start = egs.StringLength - startPos;; start = 0)
	{
		std::vector<wchar_t> uc_line;
		to_upper(egs.StringText, egs.StringLength - start, uc_line);

		std::vector<wchar_t>::reverse_iterator
			found(std::search(uc_line.rbegin(), uc_line.rend(),
				uc_pattern.rbegin(), uc_pattern.rend()));
		int foundPos = found == uc_line.rend() ? -1 : static_cast<int>(uc_line.rend() - found - uc_pattern.size());
		int foundLength = static_cast<int>(uc_pattern.size());

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

/*static*/ void
QuickSearch::Start(int editorID, bool backward)
{
	Instances::iterator it = instances_.lower_bound(editorID);
	if (it != instances_.end() && it->first == editorID)
	{
		it->second.DeferShowPattern(GetMsg(Msg::AlreadyActive));
		return;
	}

	instances_.insert(it, std::make_pair(editorID, QuickSearch(editorID, backward)))->second.Run();
}

/*static*/ int
QuickSearch::ProcessEditorInput(INPUT_RECORD const & input)
{
	EditorInfo einfo;
	Far.EditorControl(ECTL_GETINFO, &einfo);

	Instances::iterator it = instances_.find(einfo.EditorID);
	if (it == instances_.end()) return 0; // we're not running in this editor

	if (it->second.ProcessInput(input)) return 1;
	return 0;
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
		QuickSearch::DoMainMenu();
		return INVALID_HANDLE_VALUE;
	default:
		return INVALID_HANDLE_VALUE;
	}
}

extern "C" int WINAPI
ProcessEditorInputW(INPUT_RECORD const * input)
{
	return QuickSearch::ProcessEditorInput(*input);
}
