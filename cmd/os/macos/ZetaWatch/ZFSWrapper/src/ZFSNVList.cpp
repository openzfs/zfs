//
//  ZFSNVList.cpp
//  ZetaWatch
//
//  Created by Gerhard Röthlin on 2015.12.26.
//  Copyright © 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the conditions of the "3-Clause
//  BSD" license described in the BSD.LICENSE file are met. Additional licensing
//  options are described in the README file.
//

#include "ZFSNVList.hpp"

#include <sstream>
#include <algorithm>

extern "C"
{
#include <sys/nvpair.h>
}

namespace zfs
{
namespace
{
	/*!
	 This is a placeholder for a proper time type from std::chrono
	 */
	struct HighResTime
	{
		hrtime_t time;

		HighResTime() : time()
		{
		}

		HighResTime(hrtime_t time) : time(time)
		{
		}
	};

	std::ostream & operator<<(std::ostream & os, HighResTime const & time)
	{
		return os << time.time;
	}

	static void throwOnNVListError(int errorCode)
	{
		if (errorCode)
		{
			throw std::system_error(errorCode, std::generic_category());
		}
	}
} // namespace

// Pair

NVPair::NVPair() : m_pair()
{
}

NVPair::NVPair(nvpair_t * pair) : m_pair(pair)
{
}

bool NVPair::valid() const
{
	return m_pair != nullptr;
}

NVPair::operator bool() const
{
	return valid();
}

std::string NVPair::name() const
{
	return nvpair_name(m_pair);
}

#define NVPAIRCONVERTTOP(externaltype, internaltype, funcsuffix)               \
	template <>                                                                \
	bool NVPair::convertTo<externaltype>(externaltype & outValue) const        \
	{                                                                          \
		internaltype value;                                                    \
		if (nvpair_value_##funcsuffix(m_pair, &value) != 0)                    \
			return false;                                                      \
		outValue = value;                                                      \
		return true;                                                           \
	}

#define NVPAIRCONVERTTOV(externaltype, internaltype, funcsuffix)               \
	template <>                                                                \
	bool NVPair::convertTo<std::vector<externaltype>>(                         \
	    std::vector<externaltype> & outValue) const                            \
	{                                                                          \
		uint_t size = 0;                                                       \
		internaltype * values;                                                 \
		if (nvpair_value_##funcsuffix##_array(m_pair, &values, &size) != 0)    \
			return false;                                                      \
		outValue.assign(values, values + size);                                \
		return true;                                                           \
	}

#define NVPAIRCONVERTTO(externaltype, internaltype, funcsuffix)                \
	NVPAIRCONVERTTOP(externaltype, internaltype, funcsuffix)                   \
	NVPAIRCONVERTTOV(externaltype, internaltype, funcsuffix)

#define NVPAIRCONVERTTOSIMPLE(suffix)                                          \
	NVPAIRCONVERTTO(suffix##_t, suffix##_t, suffix)

NVPAIRCONVERTTOP(bool, boolean_t, boolean_value)
NVPAIRCONVERTTOV(bool, boolean_t, boolean)
NVPAIRCONVERTTOP(double, double, double)
NVPAIRCONVERTTOP(HighResTime, hrtime_t, hrtime)
NVPAIRCONVERTTO(char, uchar_t, byte)
NVPAIRCONVERTTO(std::string, char const *, string)
NVPAIRCONVERTTO(NVList, nvlist_t *, nvlist)
NVPAIRCONVERTTOSIMPLE(int8)
NVPAIRCONVERTTOSIMPLE(uint8)
NVPAIRCONVERTTOSIMPLE(int16)
NVPAIRCONVERTTOSIMPLE(uint16)
NVPAIRCONVERTTOSIMPLE(int32)
NVPAIRCONVERTTOSIMPLE(uint32)
NVPAIRCONVERTTOSIMPLE(int64)
NVPAIRCONVERTTOSIMPLE(uint64)

#undef NVPAIRCONVERTTOSIMPLE
#undef NVPAIRCONVERTTO
#undef NVPAIRCONVERTTOP
#undef NVPAIRCONVERTTOV

int NVPair::type() const
{
	return nvpair_type(m_pair);
}

nvpair_t * NVPair::toPair() const
{
	return m_pair;
}

template <typename T>
bool streamFormatedValue(std::ostream & os, T const & value)
{
	os << value;
	return os.good();
}

template <>
bool streamFormatedValue<char>(std::ostream & os, char const & value)
{
	os << std::hex << (int)value << std::dec;
	return os.good();
}

template <>
bool streamFormatedValue<int8_t>(std::ostream & os, int8_t const & value)
{
	os << (int)value;
	return os.good();
}

template <>
bool streamFormatedValue<uint8_t>(std::ostream & os, uint8_t const & value)
{
	os << (int)value;
	return os.good();
}

template <>
bool streamFormatedValue<std::string>(
    std::ostream & os, std::string const & value)
{
	os << '"' << value << '"';
	return os.good();
}

template <typename T>
bool streamPairValue(std::ostream & os, NVPair const & pair)
{
	return streamFormatedValue(os, pair.convertTo<T>());
}

template <typename T>
bool streamArray(std::ostream & os, NVPair const & pair)
{
	std::vector<T> values = pair.convertTo<std::vector<T>>();
	os << "[\n";
	if (!values.empty())
		streamFormatedValue(os, values[0]);
	for (size_t i = 1; i < values.size(); ++i)
	{
		os << ",\n";
		streamFormatedValue(os, values[i]);
	}
	os << "\n]";
	return true;
}

bool NVPair::streamName(std::ostream & os) const
{
	os << '"' << nvpair_name(m_pair) << '"';
	return os.good();
}

bool NVPair::streamValue(std::ostream & os) const
{
	switch (data_type_t(type()))
	{
	case DATA_TYPE_DONTCARE:
		os << "dontcare";
		return true;
	case DATA_TYPE_UNKNOWN:
		os << "unknown";
		return true;
	case DATA_TYPE_BOOLEAN:
		os << "boolean";
		return true;
	case DATA_TYPE_BYTE:
		return streamPairValue<char>(os, *this);
	case DATA_TYPE_INT16:
		return streamPairValue<int16_t>(os, *this);
	case DATA_TYPE_UINT16:
		return streamPairValue<uint16_t>(os, *this);
	case DATA_TYPE_INT32:
		return streamPairValue<int32_t>(os, *this);
	case DATA_TYPE_UINT32:
		return streamPairValue<uint32_t>(os, *this);
	case DATA_TYPE_INT64:
		return streamPairValue<int64_t>(os, *this);
	case DATA_TYPE_UINT64:
		return streamPairValue<uint64_t>(os, *this);
	case DATA_TYPE_STRING:
		return streamPairValue<std::string>(os, *this);
	case DATA_TYPE_BYTE_ARRAY:
		return streamArray<char>(os, *this);
	case DATA_TYPE_INT16_ARRAY:
		return streamArray<int16_t>(os, *this);
	case DATA_TYPE_UINT16_ARRAY:
		return streamArray<uint16_t>(os, *this);
	case DATA_TYPE_INT32_ARRAY:
		return streamArray<int32_t>(os, *this);
	case DATA_TYPE_UINT32_ARRAY:
		return streamArray<uint32_t>(os, *this);
	case DATA_TYPE_INT64_ARRAY:
		return streamArray<int64_t>(os, *this);
	case DATA_TYPE_UINT64_ARRAY:
		return streamArray<uint64_t>(os, *this);
	case DATA_TYPE_STRING_ARRAY:
		return streamArray<std::string>(os, *this);
	case DATA_TYPE_HRTIME:
		return streamPairValue<HighResTime>(os, *this);
	case DATA_TYPE_NVLIST:
		return streamPairValue<NVList>(os, *this);
	case DATA_TYPE_NVLIST_ARRAY:
		return streamArray<NVList>(os, *this);
	case DATA_TYPE_BOOLEAN_VALUE:
		return streamPairValue<bool>(os, *this);
	case DATA_TYPE_INT8:
		return streamPairValue<int8_t>(os, *this);
	case DATA_TYPE_UINT8:
		return streamPairValue<uint8_t>(os, *this);
	case DATA_TYPE_BOOLEAN_ARRAY:
		return streamArray<bool>(os, *this);
	case DATA_TYPE_INT8_ARRAY:
		return streamArray<int8_t>(os, *this);
	case DATA_TYPE_UINT8_ARRAY:
		return streamArray<uint8_t>(os, *this);
	case DATA_TYPE_DOUBLE:
		return streamPairValue<double>(os, *this);
	}
	return false;
}

std::string NVPair::toString() const
{
	std::stringstream ss;
	ss << *this;
	return ss.str();
}

std::ostream & operator<<(std::ostream & os, NVPair const & pair)
{
	if (!pair.streamName(os))
		return os;
	os << ": ";
	if (!pair.streamValue(os))
		return os;
	return os;
}

// List

NVList::NVList() : m_list(), m_ownsList(false)
{
}

NVList::NVList(TakeOwnership) : m_list(), m_ownsList(true)
{
	auto r = nvlist_alloc(&m_list, NV_UNIQUE_NAME, 0);
	throwOnNVListError(r);
}

NVList::NVList(nvlist_t * list) : m_list(list), m_ownsList(false)
{
}

NVList::NVList(nvlist_t * list, TakeOwnership) : m_list(list), m_ownsList(true)
{
}

NVList::~NVList()
{
	reset();
}

NVList::NVList(NVList && other) noexcept :
        m_list(other.m_list), m_ownsList(other.m_ownsList)
{
	other.m_list = nullptr;
	other.m_ownsList = false;
}

NVList & NVList::operator=(NVList && other) noexcept
{
	reset();
	m_list = other.m_list;
	m_ownsList = other.m_ownsList;
	other.m_list = nullptr;
	other.m_ownsList = false;
	return *this;
}

void NVList::reset()
{
	if (m_ownsList)
		nvlist_free(m_list);
	m_list = nullptr;
	m_ownsList = false;
}

bool NVList::valid() const
{
	return m_list != nullptr;
}

NVList::operator bool() const
{
	return valid();
}

bool NVList::empty() const
{
	return nvlist_empty(m_list) == B_TRUE;
}

bool NVList::exists(char const * key) const
{
	return nvlist_exists(m_list, key) == B_TRUE;
}

NVPair NVList::lookupPair(char const * key) const
{
	nvpair_t * pair = nullptr;
	nvlist_lookup_nvpair(m_list, key, &pair);
	return NVPair(pair);
}

void NVList::addBoolean(const char * name)
{
	auto r = nvlist_add_boolean(m_list, name);
	throwOnNVListError(r);
}

namespace
{
	template <typename D, typename S>
	D convert_input_value(S const & value)
	{
		return static_cast<D>(value);
	}

	template <>
	char const *
	convert_input_value<char const *, std::string>(std::string const & value)
	{
		return value.c_str();
	}

	template <>
	char * convert_input_value<char *, std::string>(std::string const & value)
	{
		// The API has weird const placement, but is not expected to modify
		// anything
		return const_cast<char *>(value.c_str());
	}

	template <>
	nvlist_t * convert_input_value<nvlist_t *, NVList>(NVList const & value)
	{
		return value.toList();
	}
} // namespace

#define NVLIST_ADDP(externaltype, internaltype, funcsuffix)                    \
	template <>                                                                \
	void NVList::add<externaltype>(                                            \
	    char const * key, externaltype const & value)                          \
	{                                                                          \
		auto r = nvlist_add_##funcsuffix(                                      \
		    m_list, key, convert_input_value<internaltype>(value));            \
		throwOnNVListError(r);                                                 \
	}

// Transform data since the function wants non-const pointer
#define NVLIST_ADDV(externaltype, internaltype, funcsuffix)                    \
	template <>                                                                \
	void NVList::add<std::vector<externaltype>>(                               \
	    char const * key, std::vector<externaltype> const & value)             \
	{                                                                          \
		std::vector<internaltype> internal(value.size());                      \
		std::transform(                                                        \
		    value.begin(),                                                     \
		    value.end(),                                                       \
		    internal.begin(),                                                  \
		    convert_input_value<internaltype, externaltype>);                  \
		auto r = nvlist_add_##funcsuffix##_array(                              \
		    m_list,                                                            \
		    key,                                                               \
		    internal.data(),                                                   \
		    static_cast<uint_t>(internal.size()));                             \
		throwOnNVListError(r);                                                 \
	}

#define NVLIST_ADD(externaltype, internaltype, funcsuffix)                     \
	NVLIST_ADDP(externaltype, internaltype, funcsuffix)                        \
	NVLIST_ADDV(externaltype, internaltype, funcsuffix)

#define NVLIST_ADD_SIMPLE(suffix) NVLIST_ADD(suffix##_t, suffix##_t, suffix)

NVLIST_ADDP(bool, boolean_t, boolean_value)
NVLIST_ADDV(bool, boolean_t, boolean)
NVLIST_ADDP(double, double, double)
NVLIST_ADD(char, uchar_t, byte)
NVLIST_ADDP(char const *, char const *, string)
NVLIST_ADDP(std::string, char const *, string)
NVLIST_ADDV(std::string, char *, string)
NVLIST_ADD(NVList, nvlist_t *, nvlist)
NVLIST_ADD_SIMPLE(int8)
NVLIST_ADD_SIMPLE(uint8)
NVLIST_ADD_SIMPLE(int16)
NVLIST_ADD_SIMPLE(uint16)
NVLIST_ADD_SIMPLE(int32)
NVLIST_ADD_SIMPLE(uint32)
NVLIST_ADD_SIMPLE(int64)
NVLIST_ADD_SIMPLE(uint64)

#undef NVLIST_ADD_SIMPLE
#undef NVLIST_ADD
#undef NVLIST_ADDP
#undef NVLIST_ADDV

void NVList::add(char const * key, char const * value)
{
	return add<char const *>(key, value);
}

void NVList::remove(char const * key)
{
	auto r = nvlist_remove_all(m_list, key);
	throwOnNVListError(r);
}

NVList::Iterator NVList::begin() const
{
	return Iterator(toList());
}

NVList::Iterator NVList::end() const
{
	return Iterator();
}

nvlist_t * NVList::toList() const
{
	return m_list;
}

std::string NVList::toString() const
{
	std::stringstream ss;
	ss << *this;
	return ss.str();
}

std::ostream & operator<<(std::ostream & os, NVList const & list)
{
	os << "{\n";
	NVPair pair = nvlist_next_nvpair(list.toList(), nullptr);
	if (pair.valid())
	{
		os << pair;
		pair = nvlist_next_nvpair(list.toList(), pair.toPair());
	}
	while (pair.valid())
	{
		os << ",\n" << pair;
		pair = nvlist_next_nvpair(list.toList(), pair.toPair());
	}
	os << "\n}";
	return os;
}

NVList::Iterator::Iterator() : list(), pair()
{
}

NVList::Iterator::Iterator(nvlist_t * l) : list(l), pair()
{
	pair = nvlist_next_nvpair(list, pair);
}

bool NVList::Iterator::operator==(Iterator const & other) const
{
	return pair == other.pair;
}

bool NVList::Iterator::operator!=(Iterator const & other) const
{
	return !(*this == other);
}

NVPair NVList::Iterator::operator*() const
{
	return NVPair(pair);
}

NVList::Iterator & NVList::Iterator::operator++()
{
	pair = nvlist_next_nvpair(list, pair);
	return *this;
}

NVList::Iterator NVList::Iterator::operator++(int)
{
	Iterator old = *this;
	++(*this);
	return old;
}

} // namespace zfs
