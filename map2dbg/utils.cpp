#include "stdafx.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <clocale>
#include <fstream>
#include <locale>
#include <vector>

#include "utils.h"

std::string ChangeFileExt(const std::string& filename, const std::string& newExtension) {
	size_t pos = filename.find_last_of(".", std::string::npos);
	if (pos != std::string::npos) {
		return filename.substr(0, pos) + newExtension;
	} else {
		return filename;
	}
}

std::string ExtractFileName(const std::string& filename) {
	size_t pos = filename.find_last_of("\\", std::string::npos);
	if( pos != std::string::npos ) {
		return filename.substr(pos + 1, std::string::npos);
	} else {
		return filename;
	}
}

bool FileExists(const std::string& filename) {
	struct __stat64 buf;
	int result;

	result = _stat64( filename.c_str(), &buf );
	return result == 0;
}

std::vector<std::string> LoadLines(const std::string& filename) {
	std::vector<std::string> result;
	
	std::ifstream ifs(filename.c_str());
	while( ifs.good() ) {
		char buffer[4096];
		ifs.getline( buffer, 4096 );
		result.push_back( buffer );
	}

	return result;
}

std::string from_tchar(_TCHAR* value) {
#ifdef UNICODE
	std::locale const loc("");
	std::wstring text = std::wstring(value);
	wchar_t const* from = text.c_str();
	std::size_t const len = text.size();
	std::vector<char> buffer(len + 1);
	std::use_facet<std::ctype<wchar_t> >(loc).narrow(from, from + len, '_', &buffer[0]);
	return std::string(&buffer[0], &buffer[len]);
#else
	return std::string(value);
#endif
}

std::string trim(const std::string &str0)
{
	std::string str = str0;
	size_t at2 = str.find_last_not_of(" \t\r\n\0\a\b\f\v");
	size_t at1 = str.find_first_not_of(" \t\r\n\0\a\b\f\v");
	if (at2 != std::string::npos) str.erase(at2+1);
	if (at1 != std::string::npos) str.erase(0,at1);
	return str;
}