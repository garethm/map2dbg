#ifndef convertH
#define convertH

// convert -- takes the exe and its map file, and generates a .dbg file.
// also marks the executable as debug-stripped.
// returns the number of symbols converted
int convert(std::string exe, std::string &err);

#endif
