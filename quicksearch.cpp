#include <windows.h>

#include <plugin.hpp>

namespace Msg
{
	enum
	{
		Caption,
		SearchForward,
		SearchBackward,
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

void DoMainMenu()
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

void SearchForward()
{

}

void SearchBackward()
{

}

