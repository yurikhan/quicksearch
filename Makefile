CL_OPTIONS=/Ox /EHsc /DUNICODE /I..\\common\\unicode /Zc:forScope,wchar_t /c /nologo /W3
CL=cl.exe $(CL_OPTIONS)

LINK_OPTIONS=/DLL /NOLOGO /RELEASE /SUBSYSTEM:CONSOLE
LINK=link.exe $(LINK_OPTIONS)

RC=rc.exe

DIST_FILES=quicksearch.dll quicksearch64.dll COPYING quicksearch.hlf quicksearch.lng README techinfo.reg

all: quicksearch.dll quicksearch64.dll

clean:
	-del *.dll *.obj *.res *.pdb *.lib *.exp

dist: $(DIST_FILES)
	7z a quicksearch.zip $(DIST_FILES)

quicksearch.dll: quicksearch.obj quicksearch.def quicksearch.res
	x86 $(LINK) /MACHINE:X86 /OUT:quicksearch.dll /PDB:quicksearch.pdb quicksearch.obj /DEF:quicksearch.def quicksearch.res user32.lib Advapi32.lib

quicksearch64.dll: quicksearch64.obj quicksearch.def quicksearch.res
	x86_amd64 $(LINK) /MACHINE:X64 /OUT:quicksearch64.dll /PDB:quicksearch64.pdb quicksearch64.obj /DEF:quicksearch.def quicksearch.res user32.lib Advapi32.lib

quicksearch.obj: quicksearch.cpp
	x86 $(CL) /Foquicksearch.obj quicksearch.cpp

quicksearch64.obj: quicksearch.cpp
	x86_amd64 $(CL) /Foquicksearch64.obj quicksearch.cpp

quicksearch.res: quicksearch.rc
	$(RC) quicksearch.rc
