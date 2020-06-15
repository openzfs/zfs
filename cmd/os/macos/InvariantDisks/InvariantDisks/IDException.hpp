//
//  IDException.hpp
//  InvariantDisks
//
//  Created by Gerhard Röthlin on 2014.04.27.
//  Copyright (c) 2014 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without modification, are permitted
//  provided that the conditions of the "3-Clause BSD" license described in the BSD.LICENSE file are met.
//  Additional licensing options are described in the README file.
//

#ifndef ID_EXCEPTION_HPP
#define ID_EXCEPTION_HPP

#include <stdexcept>
#include <sstream>

#include <CoreFoundation/CFError.h>

namespace ID
{
	/*!
	 \brief Base class for all Seal exceptions
	 */
	class Exception : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	/*!
	 \brief Helper class for scope bounded exception throwing
	 */
	template<typename E>
	class Throw
	{
	public:
		~Throw() noexcept(false)
		{
			throw E(m_ss.str());
		}

	public:
		/*!
		 Forward everything to the embedded stringstream
		 */
		template<typename T>
		Throw & operator<<(T && t)
		{
			m_ss << std::forward<T>(t);
			return *this;
		}

		/*!
		 For IO Manipulators
		 */
		Throw & operator<<(std::ostream & (*m)(std::ostream&))
		{
			m_ss << m;
			return *this;
		}

	public:
		/*!
		 Format Core Foundation errors
		 */
		Throw & operator<<(CFErrorRef error)
		{
			CFStringRef es = CFErrorCopyDescription(error);
			char const * esp = CFStringGetCStringPtr(es, kCFStringEncodingUTF8);
			if (esp)
				*this << esp;
			CFRelease(es);
			return *this;
		}

	private:
		std::stringstream m_ss;
	};
}

#endif
