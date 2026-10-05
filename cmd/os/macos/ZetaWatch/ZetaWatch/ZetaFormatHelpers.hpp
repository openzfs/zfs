//
//  ZetaFormatHelpers.hpp
//  ZetaWatch
//
//  Created by cbreak on 19.06.22.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#ifndef ZetaFormatHelpers_hpp
#define ZetaFormatHelpers_hpp

#include <sstream>
#include <string>
#include <iomanip>
#include <regex>

struct Prefix
{
	uint64_t factor;
	char const * prefix;
};

extern Prefix const metricPrefixes[];
extern size_t const metricPrefixCount;

extern Prefix const binaryPrefixes[];
extern size_t const binaryPrefixCount;

template<typename T>
std::string formatPrefixedValue(T size, Prefix const * prefix, size_t prefixCount)
{
	for (size_t p = 0; p < prefixCount; ++p)
	{
		if (size >= prefix[p].factor)
		{
			double scaledSize = size / double(prefix[p].factor);
			std::stringstream ss;
			ss << std::setprecision(2) << std::fixed << scaledSize << " " << prefix[p].prefix;
			return ss.str();
		}
	}
	return std::to_string(size) + " ";
}

template<typename T>
std::string formatInformationValue(T size)
{
	return formatPrefixedValue(size, binaryPrefixes, binaryPrefixCount);
}

template<typename T>
std::string formatNormalValue(T size)
{
	return formatPrefixedValue(size, metricPrefixes, metricPrefixCount);
}

template<typename T>
std::string formatBytes(T bytes)
{
	return formatInformationValue(bytes) + "B";
}

template<typename T>
bool parseBytes(char const * byteString, T & outBytes)
{
	std::regex byteRegex(R"((\d+\.?\d*)\s*([EPTGMk]?i?)B?)", std::regex::icase);
	std::cmatch match;
	if (std::regex_match(byteString, match, byteRegex))
	{
		double bytesFormated = std::stod(std::string(match[1].first, match[1].second));
		std::string prefix(match[2].first, match[2].second);
		std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
		if (prefix.length() == 1)
			prefix += 'i';
		Prefix const * foundPrefix = std::find_if(binaryPrefixes, binaryPrefixes + binaryPrefixCount,
			[=](Prefix const & p)
		{
			std::string pp(p.prefix);
			std::transform(pp.begin(), pp.end(), pp.begin(), ::tolower);
			return prefix == pp;
		});
		if (foundPrefix != binaryPrefixes + binaryPrefixCount)
		{
			outBytes = static_cast<T>(bytesFormated * foundPrefix->factor);
		}
		else
		{
			outBytes = static_cast<T>(bytesFormated);
		}
		return true;
	}
	return false;
}

inline std::string formatRate(uint64_t bytes, std::chrono::seconds const & time)
{
	return formatBytes(bytes / time.count()) + "/s";
}

template<typename T> T toFormatable(T t)
{
	return t;
}

inline char const * toFormatable(std::string const & str)
{
	return str.c_str();
}

inline std::string trim(std::string const & s)
{
	size_t first = s.find_first_not_of(' ');
	size_t last = s.find_last_not_of(' ');
	if (first != std::string::npos)
		return s.substr(first, last - first + 1);
	return s;
}

#endif /* ZetaMenuHelpers_h */
