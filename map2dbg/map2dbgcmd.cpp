//---------------------------------------------------------------------------

#include "stdafx.h"
#pragma hdrstop

#include <windows.h>
#include <imagehlp.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include "cvexefmt.h"
#include "convert.h"
#include "utils.h"

//---------------------------------------------------------------------------

int _tmain(int argc, _TCHAR* argv[])
{
	bool ok = (argc==2);
	if (argc==3 && from_tchar(argv[2])==std::string("/nomap"))
		ok=true;
	if (!ok)
	{
		fputs("Syntax: map2dbg [/nomap] file.exe\n",stdout);
		return 1;
	}

	std::string exe = from_tchar(argv[1]);
	if (argc==3)
		exe = from_tchar(argv[2]);
	if (!FileExists(exe) && FileExists(exe+".exe"))
		exe=exe+".exe";
	if (!FileExists(exe) && FileExists(exe+".dll"))
		exe=exe+".dll";
	if (!FileExists(exe))
	{
		fputs(("File '"+exe+"' not found").c_str(),stdout);
		return 1;
	}

	std::string err;
	int num = convert(exe,err);

	if (err=="")
	{
		fputs(("Converted "+to_s(num)+" symbols.").c_str(),stdout);
		return 0;
	}
	else
	{
		fputs(err.c_str(),stdout);
		return 1;
	}
}
//---------------------------------------------------------------------------

