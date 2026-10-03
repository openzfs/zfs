//
//  ZFSNVList.hpp
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

#ifndef ZETA_ZFSNVLIST_HPP
#define ZETA_ZFSNVLIST_HPP

#include <stdexcept>
#include <string>
#include <vector>

// sys/nvpair.h forward declarations
typedef struct nvlist nvlist_t;
typedef struct nvpair nvpair_t;

namespace zfs
{
/*!
 \brief A non-owning wrapper for using the zfs nvpair data structure more
 conveniently
 */
class NVPair
{
public:
	/*!
	 Default construct an invalid NVPair, not refering to any nvpair_t.
	 */
	NVPair();

	/*!
	 Construct an NVPair that refers to an nvpair_t without taking ownership.
	 */
	NVPair(nvpair_t * pair);

public:
	/*!
	 \returns true if this NVPair is valid, and has an associated nvpair, false
	 otherwise.
	 */
	bool valid() const;

	/*!
	 \see valid()
	 */
	explicit operator bool() const;

	/*!
	 \returns the name of this pair
	 */
	std::string name() const;

public:
	/*!
	 Converts the given nvpair to the given type if possible, writing the value
	 into outValue. Only the types `bool`, `char`, `std::string`, `NVList`,
	 `int8_t`, `uint8_t`, `int16_t`, `uint16_t`, `int32_t`, `uint32_t`,
	 `int64_t`, `uint64_t` and vectors of them, as well as double are supported.
	 \returns true if the NVPair can be converted, false otherwise
	 */
	template <typename T>
	bool convertTo(T & outValue) const;

	/*!
	 Converts the given nvpair to the given type if possible
	 \returns the converted value
	 \throws std::logic_error otherwise
	 */
	template <typename T>
	T convertTo() const;

public:
	/*!
	 \returns the type that the underlying nvpair_t contains, as element of enum
	 data_type_t \note Normally, convertTo() is more convenient for end users
	 */
	int type() const;

	/*!
	 \returns the underlying nvpair_t
	 */
	nvpair_t * toPair() const;

public:
	std::string toString() const;
	bool streamName(std::ostream & os) const;
	bool streamValue(std::ostream & os) const;

private:
	nvpair_t * m_pair;
};

std::ostream & operator<<(std::ostream & os, NVPair const & pair);

/*!
 \brief A wrapper for using the zfs nvlist data structure more conveniently
 */
class NVList
{
public:
	class Iterator
	{
	public:
		bool operator==(Iterator const &) const;
		bool operator!=(Iterator const &) const;
		NVPair operator*() const;
		Iterator & operator++();
		Iterator operator++(int);

	private:
		friend class NVList;
		Iterator();
		Iterator(nvlist_t * l);

	private:
		nvlist_t * list;
		nvpair_t * pair;
	};

public:
	/*!
	 Tag type for creation of an NVList that owns its underlying nvlist_t, and
	 will free it.
	 */
	struct TakeOwnership
	{
	};

public:
	/*!
	 Create an invalid NVList, not refering to any nvlist_t.
	 */
	NVList();

	/*!
	 Creates an NVList refering to a freshly created nvlist_t that is owned.
	 */
	explicit NVList(TakeOwnership);

	/*!
	 Create an NVList that referrs to an existing nvlist_t without taking
	 ownership.
	 */
	NVList(nvlist_t * list);

	/*!
	 Create an NVList that takes ownership of an existing nvlist_t. This will
	 cause the destructor to free the list.
	 */
	explicit NVList(nvlist_t * list, TakeOwnership);

	/*!
	 Free owned resources.
	 */
	~NVList();

public:
	NVList(NVList && other) noexcept;
	NVList & operator=(NVList && other) noexcept;

public:
	/*!
	 \returns true if this NVList is valid, and has an associated nvlist, false
	 otherwise.
	 */
	bool valid() const;

	/*!
	 \see valid()
	 */
	explicit operator bool() const;

	/*
	 \returns true if the associated nvlist is empty, false otherwise.
	 */
	bool empty() const;

	/*!
	 \returns true if an entry with the given key exists, false otherwise.
	 */
	bool exists(char const * key) const;

	/*!
	 Looks up the given key
	 \returns the associated value if it is found.
	 \throws std::out_of_range otherwise
	 */
	template <typename T>
	T lookup(char const * key) const;

	/*!
	 Looks up the given key. If it is found, the outValue parameter is set to
	 that value. \returns true if the given key is found, false otherwise
	 */
	template <typename T>
	bool lookup(char const * key, T & outValue) const;

	/*!
	 Looks up the given key. If it is found, an NVPair representing it is
	 returned. \returns an NVPair representing the queried key, or an invalid
	 NVPair
	 */
	NVPair lookupPair(char const * key) const;

public:
	/**!
	 Adds a given key as boolean. This is different from adding a bool value
	 with a key, closer to adding keys to a set.
	 */
	void addBoolean(char const * key);

	/*!
	 Adds a value with the given key.
	 Only the types `bool`, `char`, `std::string`, `NVList`, `int8_t`,
	 `uint8_t`, `int16_t`, `uint16_t`, `int32_t`, `uint32_t`, `int64_t`,
	 `uint64_t` and vectors of them, as well as double are supported.
	 \param key The key string
	 \param value A value of a supported type
	 */
	template <typename T>
	void add(char const * key, T const & value);

	/*!
	 Adds a C-String value with the given key.
	 \param key The key string
	 \param value A value of C-String type
	 */
	void add(char const * key, char const * value);

	/*!
	 Removes the value with the given key.
	 \param key The key string to remove
	 */
	void remove(char const * key);

public:
	/*!
	 NVList::Iterator iterates over all direct members as NVPair.
	 \returns iterator to the beginning of this nvlist
	 */
	Iterator begin() const;

	/*!
	 \see begin()
	 */
	Iterator end() const;

public:
	/*!
	 \returns the underlying nvlist_t
	 */
	nvlist_t * toList() const;

public:
	/*!
	 Creates a string representation of the underlying nvlist_t. This is mainly
	 for testing and debug purposes. The result is roughly json formated.
	 */
	std::string toString() const;

private:
	void reset();

private:
	nvlist * m_list;
	bool m_ownsList;
};

std::ostream & operator<<(std::ostream & os, NVList const & list);

// Private Implementation Follows

template <typename T>
T NVPair::convertTo() const
{
	T value;
	if (!convertTo(value))
		throw std::logic_error("NVPair can not be converted to requested type");
	return value;
}

template <typename T>
T NVList::lookup(char const * key) const
{
	T value;
	if (!lookup(key, value))
		throw std::out_of_range(
		    std::string("Key ") + key + " does not exist in NVList");
	return value;
}

template <typename T>
bool NVList::lookup(char const * key, T & outValue) const
{
	if (NVPair pair = lookupPair(key))
		return pair.convertTo(outValue);
	return false;
}

} // namespace zfs

#endif
