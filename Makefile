all: quicksearch.dll

clean:
	-del quicksearch.dll *.obj *.res *.pdb *.lib *.exp

dist:
	7z a quicksearch.zip quicksearch.dll COPYING quicksearch.hlf quicksearch.lng README

quicksearch.dll: quicksearch.obj quicksearch.def quicksearch.res
	link /DEF:quicksearch.def /DLL /MACHINE:X86 /NOLOGO /OUT:quicksearch.dll /PDB:quicksearch.pdb /RELEASE /SUBSYSTEM:CONSOLE quicksearch.obj quicksearch.res user32.lib

quicksearch.obj: quicksearch.cpp
	cl /Ox /EHsc /DUNICODE /I..\\common\\unicode /Zc:forScope,wchar_t /c /nologo /W3 quicksearch.cpp

quicksearch.res: quicksearch.rc
	rc quicksearch.rc
