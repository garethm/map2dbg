#include "stdafx.h"
#include <windows.h>
#include <imagehlp.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include "cvexefmt.h"
#include "utils.h"
#pragma hdrstop
#include "convert.h"
//---------------------------------------------------------------------------

//============================================================================
// Convert -- converts Borland's MAP file format, into Microsoft's DBG format,
// and marks the executable as 'debug-stripped'. See readme.txt for a discussion.
// This code is (c) 2000-2002 Lucian Wischik.
//============================================================================

//============================================================================
// First, just a small debugging function and some code for dynamic-loading dll
// and an init and automatic-exit routine
//============================================================================
std::string __fastcall le() {
	LPVOID lpMsgBuf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,NULL,GetLastError(),MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),(LPTSTR) &lpMsgBuf,0,NULL);
	std::string s = std::string((char*)lpMsgBuf);
	LocalFree( lpMsgBuf );
	while ((s.size() > 0) && (s[s.size()-1]=='\r' || s[s.size()-1]=='\n'))
		s.erase( s.size() - 1, 1 );
	return s;
}
//
typedef BOOL (__stdcall *MAPANDLOADPROC)(IN LPSTR ImageName, IN LPSTR DllPath, OUT PLOADED_IMAGE LoadedImage, IN BOOL DotDll, IN BOOL ReadOnly);
typedef BOOL (__stdcall *UNMAPANDLOADPROC)(IN PLOADED_IMAGE LoadedImage);
MAPANDLOADPROC pMapAndLoad = NULL;
UNMAPANDLOADPROC pUnMapAndLoad = NULL;
bool isinit = false, issucc = false; 
HINSTANCE himagehlp = NULL;
//
bool iinit() {
	if (isinit)
		return true;

	isinit = true;
	issucc = false;
	himagehlp = LoadLibrary(L"imagehlp.dll");
	if (himagehlp == 0) {
		std::cerr << "The system DLL imagehlp.dll was not found.";
		return false;
	}
	pMapAndLoad = (MAPANDLOADPROC)GetProcAddress(himagehlp,"MapAndLoad");
	pUnMapAndLoad = (UNMAPANDLOADPROC)GetProcAddress(himagehlp,"UnMapAndLoad");
	if (pMapAndLoad==0 || pUnMapAndLoad==0)
	{
		FreeLibrary(himagehlp);
		himagehlp=NULL;
		std::cerr << "The system DLL imagehlp.dll did not have the required functionality.";
		return false;
	}
	issucc = true;
	return true;
}
void iexit()
{
	if (himagehlp!=NULL)
		FreeLibrary(himagehlp);
	himagehlp = NULL;
	isinit = false;
}

class TAutoExitClass
{
  public: ~TAutoExitClass()
  {
	iexit();
  }
} DummyAutoExit;

//============================================================================
// TDebugFile -- for creating a .DBG file from scratch
// methods TDebugFile(fnexe,fndbg), AddSymbol(seg,off,name), End()
// They return a 'bool' for success or failure. The text string 'err'
// reports what that error was.
// End is automatically called by the destructor. But you might want
// to call it yourself, beforehand, for manual error checking.
// If file is non-null then it means we have succesfully set things up.
//============================================================================
// File format is as follows:
// In each column the offsets are relative to the start of that column.
// Thus, oCv is relative to the file as a whole; cvoSstModule is relative to the
// start of the SstModule; gpoSym is relative to the start of GlobalPub module.
//
// @0. IMAGE_SEPARATE_DEBUG_HEADER -- header of the file. [WriteDBGHeader]
// @.  numsecs * IMAGE_SECTION_HEADER -- executable's section table. [WriteSectionTable]
// @.  1 * IMAGE_DEBUG_DIRECTORY -- only one cv-type debug directory. [WriteDbgDirectory]
// @oCv. <cv-data> -- this is the raw data. of size szCv
//   @0. OMFSignature -- 'NB09'+omfdir. [in WriteCv]
//   @8. OMFDirHeader -- subsection directory header. [in WriteCv]
//   @.  3 * OMFDirEntry -- 3 directory entries: sstModule, sstGlobalPub, sstSegMap. [in WriteCv]
//   @cvoSstModule. <sst-module>, of length SstModuleSize. [WriteSstModule]
//     @0. OMFModule
//     @.  numsecs * OMFSegDest
//     @.  modname, of size fnsize.
//   @cvoGlobalPub. <global-pub>, of length GlobalPubSize.
//     @0. OMFSymHash -- [WriteGlobalPubHeader]
//     @.  nSymbols * var. Variable-sized sympols. [WriteSymbol]
//     @gpoSym. always points to the next symbol to write, is relative to the start of global-pub
//   @cvoSegMap. <seg-map>, of length SetMapSize. [WriteSegMap]
//     @0. OMFSegMap
//     @.  nsec * OMFSegMapDesc
//
// Start
//   * numsec deduced from the executable-image.
//   * oCv is easy
//   * cvoSstModule, szSstModule are constant. szModName is easy.
//   * cvoGlobalPub just comes after, gpoSym initialized to after the OMFSymHash
//     [don't write anything yet]
// AddEntry
//   * increases gpoSym. [WriteSymbol]
// Finish
//   * cvoSegMap = cvoGlobalPub + gpoSym.
//     [WriteDBGHeader, WriteSectionTable, WriteDbgDirectory, WriteCv...]
//     [... WriteSstModule, WriteGlobalPubHeader, WriteSegMap]
//
class TDebugFile {
public:
	TDebugFile(std::string afnexe, std::string afndbg) : file(NULL), ismapped(false), err(""), fnexe(afnexe), fndbg(afndbg) {}
	~TDebugFile() {
		End();
		if (ismapped)
			pUnMapAndLoad(&image);
		ismapped=false;
		if (file!=NULL)
			fclose(file);
		file=NULL;
	}

	bool AddSymbol(unsigned short seg, unsigned long offset, std::string symbol);
	bool End(); // to flush the thing to disk.
	std::string err;

protected:
	std::string fnexe, fndbg; // keep a copy of the arguments to the constructor. We don't init until later.
	std::string modname;
	unsigned int szModName;
	LOADED_IMAGE image;
	bool ismapped; // we load the input exe into this image
	FILE *file; // the output file
	unsigned long oCv;          // offset to 'cv' data, relative to the start of the output file
	unsigned long cvoSstModule; // offset to sstModule within cv block
	unsigned long szSstModule;  // size of that sstModule
	unsigned long cvoGlobalPub; // offset to GlobalPub within cv block
	unsigned long gpoSym;       // offset to next-symbol-to-write within GlobalPub block

	bool check(unsigned long pos, std::string s)
	{
		if (pos != (unsigned long) ftell(file) && err != "") {
			err=s;
			return false;
		} else {
			return true;
		}
	}
	bool EnsureStarted(); // this routine is called automatically by AddSymbol and End
};


bool TDebugFile::EnsureStarted()
{
	if (file!=NULL)
		return true;

	char c[MAX_PATH];
	strcpy_s(c, MAX_PATH, fnexe.c_str());

	BOOL bres = pMapAndLoad(c,0,&image,false,true);
	if (bres) {
		ismapped=true;
	} else {
		err="Failed to load executable '"+fnexe+"'";
		return false;
	}
	modname      = ChangeFileExt(ExtractFileName(fnexe),"");
	szModName    = ((modname.size()+1)+3) & (~3); // round it up
	oCv          = sizeof(IMAGE_SEPARATE_DEBUG_HEADER) + image.NumberOfSections*sizeof(IMAGE_SECTION_HEADER) + 1*sizeof(IMAGE_DEBUG_DIRECTORY);
	cvoSstModule = sizeof(OMFSignature) + sizeof(OMFDirHeader) + 3*sizeof(OMFDirEntry);
	szSstModule  = offsetof(OMFModule,SegInfo) + image.NumberOfSections*sizeof(OMFSegDesc) + szModName;
	cvoGlobalPub = cvoSstModule + szSstModule;
    gpoSym       = sizeof(OMFSymHash);

	if( fopen_s(&file, fndbg.c_str(), "wb") != 0 ) {
		err = "Failed to open output file " + fndbg;
		return false;
	}
	return true;
}


bool TDebugFile::AddSymbol(unsigned short seg, unsigned long offset, std::string symbol)
{
	EnsureStarted();
	if (file==NULL)
		return false;

	BYTE buffer[512];
	PUBSYM32* pPubSym32 = (PUBSYM32*)buffer;
	// nb. that PSUBSYM32 only works with names up to 255 characters. This
	// code is experimental: I don't know what happens if two symbols
	// get truncated down to the same 255char prefix.
	if (symbol.size()>255)
		symbol = symbol.substr(0, 255);
	DWORD cbSymbol      = symbol.size();
	DWORD realRecordLen = sizeof(PUBSYM32) + cbSymbol;
	pPubSym32->reclen   = (unsigned short)(realRecordLen - 2);
	pPubSym32->rectyp   = S_PUB32;
	pPubSym32->off      = offset;
	pPubSym32->seg      = seg;
	pPubSym32->typind   = 0;
	pPubSym32->name[0]  = (unsigned char)cbSymbol;
	lstrcpyA( (PSTR)&pPubSym32->name[1], symbol.c_str() );
	fseek(file, oCv + cvoGlobalPub + gpoSym,SEEK_SET );
	fwrite( pPubSym32, realRecordLen, 1, file );
	gpoSym += realRecordLen;
	return true;
}


bool TDebugFile::End()
{
  int numsecs = image.NumberOfSections;
  unsigned long cvoSegMap = cvoGlobalPub + gpoSym;
  unsigned long szSegMap  = sizeof(OMFSegMap) + numsecs*sizeof(OMFSegMapDesc);
  unsigned long szCv      = cvoSegMap + szSegMap;
  if (numsecs>=0xFFFF)
	return false; // OMFSegDesc only uses 'unsigned short'

  EnsureStarted();
  if (file==NULL)
	return false;
  fseek(file,0,SEEK_SET);
  //
  // WriteDBGHeader
  IMAGE_SEPARATE_DEBUG_HEADER isdh;
  isdh.Signature = IMAGE_SEPARATE_DEBUG_SIGNATURE;
  isdh.Flags = 0;
  isdh.Machine            = image.FileHeader->FileHeader.Machine;
  isdh.Characteristics    = image.FileHeader->FileHeader.Characteristics;
  isdh.TimeDateStamp      = image.FileHeader->FileHeader.TimeDateStamp;
  isdh.CheckSum           = image.FileHeader->OptionalHeader.CheckSum;
  isdh.ImageBase          = image.FileHeader->OptionalHeader.ImageBase;
  isdh.SizeOfImage        = image.FileHeader->OptionalHeader.SizeOfImage;
  isdh.NumberOfSections   = numsecs;
  isdh.ExportedNamesSize  = 0;
  isdh.DebugDirectorySize = 1*sizeof(IMAGE_DEBUG_DIRECTORY);
  isdh.SectionAlignment   = image.FileHeader->OptionalHeader.SectionAlignment;
  fwrite( &isdh,sizeof(isdh),1,file);
  //
  // WriteSectionTable
  check(sizeof(IMAGE_SEPARATE_DEBUG_HEADER),"Section table");
  fwrite(image.Sections, sizeof(IMAGE_SECTION_HEADER), numsecs, file);
  //
  // WriteDbgDirectory
  check(sizeof(IMAGE_SEPARATE_DEBUG_HEADER) + numsecs*sizeof(IMAGE_SECTION_HEADER),"Debug directory");
  IMAGE_DEBUG_DIRECTORY idd;
  idd.Characteristics = 0;
  idd.TimeDateStamp = image.FileHeader->FileHeader.TimeDateStamp;
  idd.MajorVersion = 0;
  idd.MinorVersion = 0;
  idd.Type = IMAGE_DEBUG_TYPE_CODEVIEW;
  idd.SizeOfData = szCv;
  idd.AddressOfRawData = 0;
  idd.PointerToRawData = oCv;
  fwrite( &idd, sizeof(idd), 1, file );
  //
  // WriteCV - misc
  check(oCv, "CV data");
  OMFSignature omfsig = { {'N','B','0','9'}, sizeof(omfsig) };
  fwrite( &omfsig, sizeof(omfsig), 1, file );
  // WriteCV - misc - dirheader
  OMFDirHeader omfdirhdr;
  omfdirhdr.cbDirHeader = sizeof(omfdirhdr);
  omfdirhdr.cbDirEntry = sizeof(OMFDirEntry);
  omfdirhdr.cDir = 3;
  omfdirhdr.lfoNextDir = 0;
  omfdirhdr.flags = 0;
  fwrite( &omfdirhdr, sizeof(omfdirhdr), 1, file );
  // WriteCV - misc - direntry[0]: sstModule
  OMFDirEntry omfdirentry;
  omfdirentry.SubSection = sstModule;
  omfdirentry.iMod = 1;
  omfdirentry.lfo = cvoSstModule;
  omfdirentry.cb = szSstModule;
  fwrite( &omfdirentry, sizeof(omfdirentry), 1, file );
  // WriteCV - misc - direntry[1]: sstGlobalPub
  omfdirentry.SubSection = sstGlobalPub;
  omfdirentry.iMod = 0xFFFF;
  omfdirentry.lfo = cvoGlobalPub;
  omfdirentry.cb = gpoSym;
  fwrite( &omfdirentry, sizeof(omfdirentry), 1, file );
  // WriteCV - misc - direntry[2]: sstSegMap
  omfdirentry.SubSection = sstSegMap;
  omfdirentry.iMod = 0xFFFF;
  omfdirentry.lfo = cvoSegMap;
  omfdirentry.cb = szSegMap;
  fwrite( &omfdirentry, sizeof(omfdirentry), 1, file );
  //
  // WriteSstModule
  check(oCv + cvoSstModule, "CV:SST module");
  OMFModule omfmodule;
  omfmodule.ovlNumber = 0;
  omfmodule.iLib = 0;
  omfmodule.cSeg = (unsigned short)numsecs;
  omfmodule.Style[0] = 'C';
  omfmodule.Style[1] = 'V';
  fwrite( &omfmodule, offsetof(OMFModule,SegInfo), 1, file );
  // WriteSstModule - numsecs*OMFSegDesc
  for (int i = 0; i < numsecs; i++ )
  { OMFSegDesc omfsegdesc;
	omfsegdesc.Seg = (unsigned short)(i+1);
	omfsegdesc.pad = 0;
	omfsegdesc.Off = 0;
	omfsegdesc.cbSeg = image.Sections[i].Misc.VirtualSize;
	fwrite( &omfsegdesc, sizeof(omfsegdesc), 1, file );
  }
  // WriteSstModule - modname
  fwrite( modname.c_str(), szModName, 1, file );
  //
  // WriteGlobalPub
  check(oCv + cvoGlobalPub,"CV:GlobalPub module");
  OMFSymHash omfSymHash;
  omfSymHash.cbSymbol = gpoSym - sizeof(OMFSymHash);
  omfSymHash.symhash = 0; // No symbol or address hash tables...
  omfSymHash.addrhash = 0;
  omfSymHash.cbHSym = 0;
  omfSymHash.cbHAddr = 0;
  fwrite( &omfSymHash, sizeof(omfSymHash), 1, file );
  // WriteGlobal - symbols
  fseek(file, oCv + cvoSegMap, SEEK_SET);
  //
  // WriteSegMap
  check(oCv + cvoSegMap,"CV:SegMap module");
  OMFSegMap omfSegMap = {(unsigned short)numsecs,(unsigned short)numsecs};
  fwrite( &omfSegMap, sizeof(OMFSegMap), 1, file );
  // WriteSegMap - nsec*OMFSegMapDesc
  for (int i = 1; i <= numsecs; i++ )
  { OMFSegMapDesc omfSegMapDesc;
	omfSegMapDesc.flags = 0;
	omfSegMapDesc.ovl = 0;
	omfSegMapDesc.group = 0;
	omfSegMapDesc.frame = (unsigned short)i;
	omfSegMapDesc.iSegName = 0xFFFF;
	omfSegMapDesc.iClassName = 0xFFFF;
	omfSegMapDesc.offset = 0;
	omfSegMapDesc.cbSeg = image.Sections[i-1].Misc.VirtualSize;
	fwrite( &omfSegMapDesc, sizeof(OMFSegMapDesc), 1, file );
  }
  //
  check(oCv + szCv,"CV:end");
  return (err == "");
}


//============================================================================
// TMapFile -- for reading a .map file
// methods GetSymbol(seg,off,name)
//============================================================================
// File format: It's a plain text file
// It must be generated with from BCB with 'publics' or 'detailed'.
// Just segments alone isn't enough. It has a load of junk at the top. We're
// interested in the bit that starts with the line
// "  Address         Publics by Value", "", followed by lines of the form
// " 0001:00000000      c1_0" until the end of the file
// If we discover any @ symbols in the function names, that's probably because
// show-mangled-names was turned on
//
class TMapFile { 
public:
	TMapFile(std::string fnmap);

	bool GetSymbol(unsigned short *aseg, unsigned long *aoff, std::string *aname);

	bool isok;
	bool ismangled;
	size_t line;
	int num;
	std::vector<std::string> str;
	std::string err;
};


TMapFile::TMapFile(std::string fnmap)
{
	err = "";
	isok = false;
	str = LoadLines( fnmap );

	//exact indexof does not work for new Delphi/CBuilder .map files (2007 & 2009)
	//line=str->IndexOf("  Address         Publics by Value");

	std::string s;
	for (size_t i = 0; i < str.size(); i++) {
		s = str.at(i);
		if (s.find_first_of(" Publics by Value") != std::string::npos) {
			line = i;
			break;
		}
	}
	line++; // to skip past that header
	isok = (line != 0);

	if ((line == 0) && (err == ""))
		err="Map file doesn't list any publics - '"+fnmap+"'";
	num = str.size() - line - 1;
	ismangled = false;
	for (size_t i=0; line != 0 && !ismangled && i < str.size(); i++) {
		s = str.at(i);
		ismangled = (s.find_first_of("@") != std::string::npos);
	}
}

bool TMapFile::GetSymbol(unsigned short *aseg, unsigned long *aoff, std::string *aname)
{
  if (line==0)
	return false;
  if (err!="")
	return false;
  while (line < str.size() && str.at(line) == "") 
	  line++;
  if (line == str.size())
	return false;
  std::string s = str.at(line);

  //example of some lines:
  // 0001:0000035C       System.CloseHandle
  // 0001:00000380  __acrtused

  line++;
  if (s.size()<15)          //minimal size = 15
	return false;
  std::string sseg = s.substr(1,4);
  for (size_t i=1; i<=sseg.size(); i++)
  {
	char c = sseg[i];
	bool okay = (c>='0' && c<='9') || (c>='A' && c<='F') || (c>='a' && c<='f');
	if (!okay)
	  return false;
  }
  std::string soff=s.substr(6,8);
  for (size_t i=1; i<soff.size(); i++)
  {
	char c = soff[i];
	bool okay = (c>='0' && c<='9') || (c>='A' && c<='F') || (c>='a' && c<='f');
	if (!okay)
	  return false;
  }
  //AnsiString sname = Trim(s.SubString(21,s.Length()-20));
  std::string sname = trim(s.substr(14, std::string::npos));      //minimal size = 15
  unsigned int val;
  int i;
  i = sscanf_s(sseg.c_str(),"%x",&val);
  if (i!=1)
	return false;
  if (val>0xFFFF)
	return false;
  *aseg = (unsigned short)val;
  i = sscanf_s(soff.c_str(),"%x",&val);
  if (i!=1)
	return false;
  *aoff  = val;
  *aname = sname;
  return true;
}

//============================================================================
// convert -- reads in symbols from a MAP file, writes then out in the DBG
// file, marks the executable as 'debug-stripped'. Or you can tell it not
// to bother reading the map or writing the dbg, but merely mark the executable.
//============================================================================
//
int convert(std::string exe, std::string &err)
{
	if (!iinit()) {
		err = "imagehlp.dll could not be loaded.";
		return 0;
	}
	if (!issucc) {
		err="imagehlp.dll could not be loaded";
		return 0;
	}

	if (!FileExists(exe)) {
		err="File '"+exe+"' does not exist.";
		return 0;
	}
  
	std::string dbg = ChangeFileExt(exe,".dbg");
	std::string map = ChangeFileExt(exe,".map");
	if (!FileExists(map)) {
		err="Need the map file '"+map+"' to get symbols.";
		return 0;
	}
	//
	TMapFile *mf = new TMapFile(map);
	int num=mf->num;
	TDebugFile *df = new TDebugFile(exe,dbg);
	bool anymore=true;
	while (anymore) { 
		unsigned short seg;
		unsigned long off;
		std::string name;
		anymore=mf->GetSymbol(&seg,&off,&name);
		if (anymore)
			if (name.size()>0)                   //skip empty names
				anymore=df->AddSymbol(seg,off,name); // stop it upon error
	}
	delete mf;
	bool dres=df->End();
	std::string derr=df->err;
	delete df;
	if (!dres) {
		err=derr;
		return 0;
	}

	err="";
	return num;
}
