//
//  ZetaCPPUtils.hpp
//  ZetaWatch
//
//  Created by cbreak on 19.07.28.
//  Copyright © 2019 the-color-black.net. All rights reserved.
//

#ifndef ZetaCPPUtils_h
#define ZetaCPPUtils_h

#include "ZFSUtils.hpp"

#include <vector>
#include <sstream>

template<typename T>
inline std::string formatForHumans(std::vector<T> const & things)
{
	if (things.empty())
		return std::string();
	std::stringstream ss;
	ss << things[0];
	for (size_t i = 1; i < things.size(); ++i)
	{
		ss << ", " << things[i];
	}
	return ss.str();
}

#endif /* ZetaCPPUtils_h */
