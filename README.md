--

Code forked from https://code.google.com/p/map2dbg/

Released by André Mussche
Licence: New BSD License

--

Map2Dbg is a mall tool to convert a .map file to a .dbg file.

A map file is a Borland/Codegear debug file, created by Delphi and C++Builder. However, it is not compatible with Microsoft's debug file format (.dbg), so it cannot be used with Microsoft debug tools (windbg.exe for example).

Map2dbg is originally created by Lucian Wischik: http://www.wischik.com/lu/programmer/

A small fix is made to his version to make it compatible with newer .map files (Delphi and C++Builder 2006 - 2009). It is also compatible with the free Turbo Delphi and Turbo C++Builder: http://turboexplorer.com/downloads

Goal of this open source project (with approval of Lucian Wischik) is:

Keep compatible with every new version of Delphi/C++Builder
Convert more debug info (e.g. line numbering, source file names)
Port it to Delphi (so it can be included in Delphi projects)
Make a plugin for Delphi/CBuilder, so converting is done after each build
Because of my little knowledge and experience of C++: if you can help me with point 2 (line numbering + source files), that would be very nice!

--

Instructions for which this tool is useful: http://capnbry.net/blog/?p=18