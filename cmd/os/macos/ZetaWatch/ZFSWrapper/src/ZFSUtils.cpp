//
//  ZFSUtils.cpp
//  ZetaWatch
//
//  Created by Gerhard Röthlin on 2015.12.24.
//  Copyright © 2015 the-color-black.net. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the conditions of the "3-Clause
//  BSD" license described in the BSD.LICENSE file are met. Additional licensing
//  options are described in the README file.
//

#include "ZFSUtils.hpp"

#include <stdexcept>
#include <future>
#include <sstream>
#include <regex>

extern "C"
{
#include <libzfs.h>
#include <libzfs_core.h>
#include <libzutil.h>

//#include <sys/zfs_mount.h>
#include <sys/mount.h>

#include <unistd.h>
}


namespace zfs
{
inline auto to_boolean_t(bool flag)
{
	return flag ? B_TRUE : B_FALSE;
}

LibZFSHandle::LibZFSHandle() : m_handle(libzfs_init()), m_owned(true)
{
	if (m_handle == nullptr)
	{
		throw std::runtime_error(libzfs_error_init(errno));
	}
	libzfs_mnttab_cache(m_handle, B_TRUE);
}

LibZFSHandle::LibZFSHandle(libzfs_handle_t * handle) :
        m_handle(handle), m_owned(false)
{
	libzfs_mnttab_cache(m_handle, B_TRUE);
}

LibZFSHandle::~LibZFSHandle()
{
	if (m_handle && m_owned)
		libzfs_fini(m_handle);
}

LibZFSHandle::LibZFSHandle(LibZFSHandle && other) noexcept :
        m_handle(other.m_handle), m_owned(other.m_owned)
{
	other.m_handle = nullptr;
}

LibZFSHandle & LibZFSHandle::operator=(LibZFSHandle && other) noexcept
{
	if (m_handle && m_owned)
		libzfs_fini(m_handle);
	m_handle = other.m_handle;
	m_owned = other.m_owned;
	other.m_handle = nullptr;
	return *this;
}

void LibZFSHandle::reset()
{
	*this = LibZFSHandle();
}

libzfs_handle_t * LibZFSHandle::handle() const
{
	return m_handle;
}

static LibZFSHandle::Version parseVersion(char const * versionString)
{
	static std::regex versionRE(R"(^.*(\d+)\.(\d+)\.(\d+).*$)");
	std::cmatch m;
	if (!std::regex_match(versionString, m, versionRE))
		throw std::runtime_error("Can't parse ZFS Version");
	LibZFSHandle::Version version = {
	    static_cast<std::uint16_t>(atoi(m[1].first)),
	    static_cast<std::uint16_t>(atoi(m[2].first)),
	    static_cast<std::uint16_t>(atoi(m[3].first)),
	};
	return version;
}

std::ostream & operator<<(std::ostream & os, LibZFSHandle::Version const & v)
{
	return os << v.major << '.' << v.minor << '.' << v.patch;
}

LibZFSHandle::Version LibZFSHandle::versionUserland()
{
	return parseVersion(zfs_version_userland());
}

LibZFSHandle::Version LibZFSHandle::versionKernel()
{
	char const * zver_kernel = nullptr;
	if (zver_kernel = zfs_version_kernel(); zver_kernel == nullptr)
	{
		throw std::runtime_error("Error getting zfs version from kernel");
	}
	return parseVersion(zver_kernel);
}

LibZFSHandle::Version LibZFSHandle::version()
{
	auto vk = versionKernel();
	auto vu = versionUserland();
	if (vk.major != vu.major || vk.minor != vu.minor || vk.patch != vu.patch)
	{
		std::stringstream ss;
		ss << "ZFS Kernel Module " << vk << " and ZFS Userland Library " << vu
		   << " do not match";
		throw std::runtime_error(ss.str());
	}
	return vk;
}

inline void truncateString(std::string & str)
{
	str.resize(std::strlen(str.data()));
}

using PropList = std::unique_ptr<zprop_list_t, void (*)(zprop_list_t *)>;

static PropList zfsProplist(zfs_handle_t * handle)
{
	PropList pl(nullptr, &zprop_free_list);
	zprop_list_t * plRaw = nullptr;
	int ec = zfs_expand_proplist(handle, &plRaw, B_TRUE, B_FALSE);
	if (ec == 0)
		pl.reset(plRaw);
	return pl;
}

static PropList zpoolProplist(zpool_handle_t * handle)
{
	PropList pl(nullptr, &zprop_free_list);
	zprop_list_t * plRaw = nullptr;
	zfs_type_t fst = ZFS_TYPE_POOL;
	int ec = zpool_expand_proplist(handle, &plRaw, fst, B_TRUE);
	if (ec == 0)
		pl.reset(plRaw);
	return pl;
}

static std::uint64_t getPropNumeric(zfs_handle_t * handle, zfs_prop_t prop)
{
	std::uint64_t v = {};
	int ec = zfs_prop_get_numeric(handle, prop, &v, nullptr, nullptr, 0);
	if (ec != 0)
		return 0; // Maybe report error in some form?
	return v;
}

static std::string getPropString(zfs_handle_t * handle, zfs_prop_t prop)
{
	std::string s;
	s.resize(ZFS_MAXPROPLEN);
	int ec = zfs_prop_get(
	    handle, prop, s.data(), s.size(), nullptr, nullptr, 0, B_FALSE);
	if (ec != 0)
		s.clear(); // Maybe report error in some form?
	else
		truncateString(s);
	return s;
}

static bool
getProperty(zfs_handle_t * handle, zfs_prop_t prop, ZFileSystem::Property & p)
{
	p.name.assign(zfs_prop_to_name(prop));
	p.value.resize(64);
	p.source.resize(128);
	zprop_source_t source = {};
	int ec = zfs_prop_get(
	    handle,
	    prop,
	    p.value.data(),
	    p.value.size(),
	    &source,
	    p.source.data(),
	    p.source.size(),
	    B_FALSE);
	truncateString(p.value);
	truncateString(p.source);
	return ec == 0;
}

static bool
getProperty(zpool_handle_t * handle, zpool_prop_t prop, ZPool::Property & p)
{
	auto name = zpool_prop_to_name(prop);
	if (!name)
		return false;
	p.name.assign(name);
	p.value.resize(64);
	zprop_source_t source = {};
	int ec = zpool_get_prop(
	    handle, prop, p.value.data(), p.value.size(), &source, B_FALSE);
	truncateString(p.value);
	return ec == 0;
}

static bool
getProperty(zpool_handle_t * handle, char const * feature, ZPool::Property & p)
{
	p.name.assign(feature);
	p.value.resize(64);
	int ec =
	    zpool_prop_get_feature(handle, feature, p.value.data(), p.value.size());
	truncateString(p.value);
	return ec == 0;
}

ZFileSystem::ZFileSystem() : m_handle(nullptr)
{
}

ZFileSystem::ZFileSystem(ZFileSystem const & handle) :
        m_handle(zfs_handle_dup(handle.m_handle))
{
}

ZFileSystem::ZFileSystem(zfs_handle_t * handle) : m_handle(handle)
{
}

ZFileSystem::~ZFileSystem()
{
	if (m_handle)
		zfs_close(m_handle);
}

ZFileSystem::ZFileSystem(ZFileSystem && other) noexcept :
        m_handle(other.m_handle)
{
	other.m_handle = nullptr;
}

ZFileSystem & ZFileSystem::operator=(ZFileSystem && other) noexcept
{
	m_handle = other.m_handle;
	other.m_handle = nullptr;
	return *this;
}

LibZFSHandle ZFileSystem::libHandle() const
{
	return LibZFSHandle(zfs_get_handle(m_handle));
}

char const * ZFileSystem::name() const
{
	return zfs_get_name(m_handle);
}

bool ZFileSystem::mounted() const
{
	return zfs_is_mounted(m_handle, nullptr);
}

bool ZFileSystem::mountable() const
{
	if (zfs_prop_get_int(m_handle, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_OFF)
		return false;
	auto mp = mountpoint();
	if (mp == ZFS_MOUNTPOINT_LEGACY || mp == ZFS_MOUNTPOINT_NONE)
		return false;
	auto ks = keyStatus();
	if (ks == KeyStatus::unavailable)
		return false;
	return true;
}

bool ZFileSystem::automountable() const
{
	if (zfs_prop_get_int(m_handle, ZFS_PROP_CANMOUNT) != ZFS_CANMOUNT_ON)
		return false;
	return mountable();
}

int ZFileSystem::mount()
{
	if (!mountable())
		return 0; // no need to do anything, success
	if (mounted())
		return 0; // already mounted, success
	return zfs_mount(m_handle, nullptr, 0);
}

int ZFileSystem::mountRecursive()
{
	return iterAllFileSystems([](ZFileSystem fs) { return fs.mount(); });
}

int ZFileSystem::automount()
{
	if (!automountable())
		return 0; // no need to do anything, success
	return mount();
}

int ZFileSystem::automountRecursive()
{
	return iterAllFileSystems([](ZFileSystem fs) { return fs.automount(); });
}

int ZFileSystem::unmount(bool force)
{
	if (!mounted())
		return 0; // already unmounted, success
	int flags = 0;
	if (force)
		flags |= MS_FORCE;
	return zfs_unmount(m_handle, nullptr, flags);
}

int ZFileSystem::unmountRecursive(bool force)
{
	return iterAllFileSystemsReverse(
	    [force](ZFileSystem fs)
	    {
		    auto res = fs.iterSnapshots([force](ZFileSystem snap)
		                                { return snap.unmount(force); });
		    if (res != 0)
			    return res;
		    return fs.unmount(force);
	    });
}

int ZFileSystem::snapshot(std::string const & snapName, bool recursive)
{
	std::string fullName = name();
	fullName += '@';
	fullName += snapName;
	auto lib = libHandle();
	return zfs_snapshot(
	    lib.handle(), fullName.c_str(), to_boolean_t(recursive), nullptr);
}

int ZFileSystem::rollback(bool force)
{
	std::string snapName = name();
	std::string baseName = snapName.substr(0, snapName.find_last_of('@'));
	if (type() != FSType::snapshot)
	{
		throw std::runtime_error(snapName + " is not a snapshot");
	}
	auto baseFS = libHandle().filesystem(baseName);
	return zfs_rollback(baseFS.m_handle, m_handle, to_boolean_t(force));
}

int ZFileSystem::clone(std::string const & newFSName)
{
	std::string snapName = name();
	if (type() != FSType::snapshot)
	{
		throw std::runtime_error(snapName + " is not a snapshot");
	}
	if (auto r = zfs_create_ancestors(libHandle().handle(), newFSName.c_str()))
		return r;
	return zfs_clone(m_handle, newFSName.c_str(), nullptr);
}

int ZFileSystem::destroy(bool force)
{
	if (auto error = unmount(force))
		return error;
	// This requires that there are no dependents left
	return zfs_destroy(m_handle, B_FALSE);
}

static int
destroySnapshots(libzfs_handle_t * libHandle, std::vector<ZFileSystem> & snaps)
{
	if (snaps.empty())
		return 0;
	NVList snapList(NVList::TakeOwnership{});
	for (auto const & s : snaps)
	{
		snapList.addBoolean(s.name());
	}
	snaps.clear();
	return zfs_destroy_snaps_nvl(libHandle, snapList.toList(), B_FALSE);
}

int ZFileSystem::destroyRecursive(bool force)
{
	auto lib = libHandle();
	std::vector<ZFileSystem> snaps;
	NVList snapList(NVList::TakeOwnership{});
	int error = iterDependents(
	    [&snaps, &lib](ZFileSystem fs)
	    {
		    // Destroy snapshots in a batch for performance
		    if (fs.type() == FSType::snapshot)
		    {
			    snaps.push_back(std::move(fs));
			    return 0;
		    }
		    else
		    {
			    if (auto error = destroySnapshots(lib.handle(), snaps))
				    return error;
			    return fs.destroy();
		    }
	    });
	if (error)
		return error;
	error = destroySnapshots(lib.handle(), snaps);
	if (error)
		return error;
	return destroy(force);
}

struct Pipe
{
	int fd[2];
	int oldStdIn;

	Pipe() : fd{-1, -1}, oldStdIn(-1)
	{
		auto r = pipe(fd);
		if (r == -1)
		{
			throw std::runtime_error("Could not create password pipe");
		}
		oldStdIn = dup(0);
		if (oldStdIn == -1)
		{
			restore();
			throw std::runtime_error("FD dup Error");
		}
		auto res = dup2(fd[0], 0);
		if (res == -1)
		{
			restore();
			throw std::runtime_error("FD dup2 Error");
		}
	}

	~Pipe()
	{
		restore();
	}

	void restore()
	{
		// Ignore errors
		if (oldStdIn != -1)
		{
			dup2(oldStdIn, 0);
			close(oldStdIn);
		}
		if (fd[0] != -1)
			close(fd[0]);
		if (fd[1] != -1)
			close(fd[1]);
	}
};

int ZFileSystem::loadKeyFile()
{
	if (keyLocation() != KeyLocation::uri)
		return EINVAL;
	auto res = zfs_crypto_load_key(m_handle, B_FALSE, nullptr);
	if (res == 0)
		zfs_refresh_properties(m_handle);
	return res;
}

int ZFileSystem::loadKey(std::string const & key)
{
	// Hackery needed because libzfs wants to read the password from the
	// standard input instead of accepting it as argument.
	Pipe p;
	auto future = std::async(
	    [&p, &key]()
	    {
		    size_t r = 0;
		    r = write(p.fd[1], key.data(), key.size());
		    if (r == size_t(-1))
			    return r;
		    r = write(p.fd[1], "\n", 1);
		    if (r == size_t(-1))
			    return r;
		    return size_t();
	    });
	char prompt[] = "prompt";
	auto res = zfs_crypto_load_key(m_handle, B_FALSE, prompt);
	bool writeRes = future.get();
	if (res == 0 && writeRes == 0)
		zfs_refresh_properties(m_handle);
	return res | writeRes;
}

int ZFileSystem::unloadKey()
{
	auto res = iterDependents([](zfs::ZFileSystem fs) { return fs.unmount(); });
	if (res != 0)
		return res;
	res = unmount();
	if (res != 0)
		return res;
	return zfs_crypto_unload_key(m_handle);
}

ZFileSystem::FSType ZFileSystem::type() const
{
	static_assert(
	    (zfs_type_t)FSType::filesystem == ZFS_TYPE_FILESYSTEM &&
	        (zfs_type_t)FSType::snapshot == ZFS_TYPE_SNAPSHOT &&
	        (zfs_type_t)FSType::volume == ZFS_TYPE_VOLUME &&
	        (zfs_type_t)FSType::pool == ZFS_TYPE_POOL &&
	        (zfs_type_t)FSType::bookmark == ZFS_TYPE_BOOKMARK,
	    "ZFileSystem::Type == zfs_type_t");
	return static_cast<FSType>(zfs_get_type(m_handle));
}

std::uint64_t ZFileSystem::used() const
{
	return getPropNumeric(m_handle, ZFS_PROP_USED);
}

std::uint64_t ZFileSystem::available() const
{
	return getPropNumeric(m_handle, ZFS_PROP_AVAILABLE);
}

std::uint64_t ZFileSystem::referenced() const
{
	return getPropNumeric(m_handle, ZFS_PROP_REFERENCED);
}

std::uint64_t ZFileSystem::logicalused() const
{
	return getPropNumeric(m_handle, ZFS_PROP_LOGICALUSED);
}

float ZFileSystem::compressRatio() const
{
	return getPropNumeric(m_handle, ZFS_PROP_COMPRESSRATIO) / 100.0;
}

std::string ZFileSystem::mountpoint() const
{
	return getPropString(m_handle, ZFS_PROP_MOUNTPOINT);
}

std::vector<ZFileSystem::Property> ZFileSystem::properties() const
{
	std::vector<ZFileSystem::Property> properties;
	auto pl = zfsProplist(m_handle);
	ZFileSystem::Property prop;
	for (auto p = pl.get(); p != nullptr; p = p->pl_next)
	{
		if (p->pl_prop != ZPROP_INVAL)
		{
			if (getProperty(
			        m_handle, static_cast<zfs_prop_t>(p->pl_prop), prop))
				properties.push_back(prop);
		}
	}
	return properties;
}

bool ZFileSystem::isEncryptionRoot() const
{
	boolean_t root = B_FALSE;
	if (zfs_crypto_get_encryption_root(m_handle, &root, nullptr))
	{
		return root == B_TRUE;
	}
	return false;
}

std::pair<std::string, bool> ZFileSystem::encryptionRoot() const
{
	boolean_t root = B_FALSE;
	// MAXNAMELEN == 256 from spl's include/sys/sysmacros.h
	std::string rootName(256 + 1, '\0');
	zfs_crypto_get_encryption_root(m_handle, &root, &rootName[0]);
	rootName.resize(std::strlen(rootName.data()));
	return std::pair<std::string, bool>{rootName, root == B_TRUE};
}

ZFileSystem::KeyStatus ZFileSystem::keyStatus() const
{
	static_assert(
	    KeyStatus::none == (KeyStatus)ZFS_KEYSTATUS_NONE &&
	        KeyStatus::unavailable == (KeyStatus)ZFS_KEYSTATUS_UNAVAILABLE &&
	        KeyStatus::available == (KeyStatus)ZFS_KEYSTATUS_AVAILABLE,
	    "ZFileSystem::KeyStatus == zfs_keystatus");
	return static_cast<KeyStatus>(
	    zfs_prop_get_int(m_handle, ZFS_PROP_KEYSTATUS));
}

ZFileSystem::KeyLocation ZFileSystem::keyLocation() const
{
	static_assert(
	    KeyLocation::none == (KeyLocation)ZFS_KEYLOCATION_NONE &&
	        KeyLocation::prompt == (KeyLocation)ZFS_KEYLOCATION_PROMPT &&
	        KeyLocation::uri == (KeyLocation)ZFS_KEYLOCATION_URI,
	    "ZFileSystem::KeyLocation == zfs_keylocation");
	auto loc = getPropString(m_handle, ZFS_PROP_KEYLOCATION);
	if (loc == "prompt")
		return KeyLocation::prompt;
	else if (loc.compare(0, 7, "file://") == 0)
		return KeyLocation::uri;
	return KeyLocation::none;
}

bool ZFileSystem::isRoot() const
{
	// There doesn't seem to be a better way
	return strchr(name(), '/') == nullptr;
}

std::uint64_t ZFileSystem::createTXG() const
{
	return getPropNumeric(m_handle, ZFS_PROP_CREATETXG);
}

std::uint64_t ZFileSystem::cloneCount() const
{
	if (type() != FSType::snapshot)
		return 0;
	return zfs_prop_get_int(m_handle, ZFS_PROP_NUMCLONES);
}

namespace
{
	template <typename CB>
	struct ZFileSystemCallback
	{
		using Callback = CB;

		ZFileSystemCallback(Callback callback) : m_callback(std::move(callback))
		{
		}

		int handle(zfs_handle_t * fs)
		{
			return m_callback(ZFileSystem(fs));
		}

		static int handle_s(zfs_handle_t * fs, void * stored_this)
		{
			return static_cast<ZFileSystemCallback *>(stored_this)->handle(fs);
		}

		Callback m_callback;
	};
} // namespace

std::vector<ZFileSystem> ZFileSystem::childFileSystems() const
{
	std::vector<ZFileSystem> children;
	ZFileSystemCallback cb(
	    [&](ZFileSystem fs)
	    {
		    children.push_back(std::move(fs));
		    return 0;
	    });
	zfs_iter_filesystems(m_handle, &cb.handle_s, &cb);
	std::sort(children.begin(), children.end());
	return children;
}

std::vector<ZFileSystem> ZFileSystem::allFileSystems() const
{
	std::vector<ZFileSystem> children;
	ZFileSystemCallback<std::function<int(ZFileSystem)>> cb(
	    [&](ZFileSystem fs)
	    {
		    children.push_back(std::move(fs));
		    zfs_iter_filesystems(children.back().m_handle, cb.handle_s, &cb);
		    return 0;
	    });
	cb.handle(zfs_handle_dup(m_handle));
	std::sort(children.begin(), children.end());
	return children;
}

std::vector<ZFileSystem> ZFileSystem::snapshots() const
{
	std::vector<ZFileSystem> snapshots;
	ZFileSystemCallback cb(
	    [&](ZFileSystem fs)
	    {
		    snapshots.push_back(std::move(fs));
		    return 0;
	    });
	zfs_iter_snapshots_sorted(m_handle, &cb.handle_s, &cb, 0, 0);
	return snapshots;
}

std::vector<ZFileSystem>
ZFileSystem::snapshotsSince(ZFileSystem const & snap) const
{
	auto min = snap.createTXG();
	std::vector<ZFileSystem> snapshots;
	ZFileSystemCallback cb(
	    [&](ZFileSystem fs)
	    {
		    snapshots.push_back(std::move(fs));
		    return 0;
	    });
	// Don't include the given snapshot itself
	zfs_iter_snapshots_sorted(m_handle, &cb.handle_s, &cb, min + 1, 0);
	return snapshots;
}

std::vector<ZFileSystem> ZFileSystem::bookmarks() const
{
	std::vector<ZFileSystem> bookmarks;
	ZFileSystemCallback cb(
	    [&](ZFileSystem fs)
	    {
		    bookmarks.push_back(std::move(fs));
		    return 0;
	    });
	zfs_iter_bookmarks(m_handle, &cb.handle_s, &cb);
	return bookmarks;
}

std::vector<ZFileSystem>
ZFileSystem::bookmarksSince(ZFileSystem const & snap) const
{
	auto min = snap.createTXG();
	std::vector<ZFileSystem> bookmarks;
	ZFileSystemCallback cb(
	    [&](ZFileSystem fs)
	    {
		    if (fs.createTXG() > min)
			    bookmarks.push_back(std::move(fs));
		    return 0;
	    });
	zfs_iter_bookmarks(m_handle, &cb.handle_s, &cb);
	return bookmarks;
}

std::vector<ZFileSystem> ZFileSystem::dependents() const
{
	std::vector<ZFileSystem> dependents;
	ZFileSystemCallback cb(
	    [&](ZFileSystem fs)
	    {
		    dependents.push_back(std::move(fs));
		    return 0;
	    });
	zfs_iter_dependents(m_handle, B_FALSE, &cb.handle_s, &cb);
	return dependents;
}

int ZFileSystem::iterChildFilesystems(
    std::function<int(ZFileSystem)> callback) const
{
	ZFileSystemCallback cb(std::move(callback));
	return zfs_iter_filesystems(m_handle, &cb.handle_s, &cb);
}

int ZFileSystem::iterAllFileSystems(
    std::function<int(ZFileSystem)> callback) const
{
	ZFileSystemCallback<std::function<int(ZFileSystem)>> cb(
	    [&](ZFileSystem fs)
	    {
		    ZFileSystem fsc(zfs_handle_dup(fs.m_handle));
		    auto r = callback(std::move(fs));
		    if (r != 0)
			    return r;
		    return zfs_iter_filesystems(fsc.m_handle, &cb.handle_s, &cb);
	    });
	return cb.handle(zfs_handle_dup(m_handle));
}

int ZFileSystem::iterAllFileSystemsReverse(
    std::function<int(ZFileSystem)> callback) const
{
	ZFileSystemCallback<std::function<int(ZFileSystem)>> cb(
	    [&](ZFileSystem fs)
	    {
		    auto r = zfs_iter_filesystems(fs.m_handle, &cb.handle_s, &cb);
		    if (r != 0)
			    return r;
		    return callback(std::move(fs));
	    });
	return cb.handle(zfs_handle_dup(m_handle));
}

int ZFileSystem::iterSnapshots(std::function<int(ZFileSystem)> callback) const
{
	ZFileSystemCallback cb(std::move(callback));
	return zfs_iter_snapshots_sorted(m_handle, &cb.handle_s, &cb, 0, 0);
}

int ZFileSystem::iterDependents(std::function<int(ZFileSystem)> callback) const
{
	ZFileSystemCallback cb(std::move(callback));
	return zfs_iter_dependents(m_handle, B_FALSE, &cb.handle_s, &cb);
}

ZPool::ZPool(libzfs_handle_t * zfsHandle, std::string const & name) :
        m_handle(zpool_open_canfail(zfsHandle, name.c_str()))
{
	if (m_handle == nullptr)
		throw std::runtime_error("Unable to open pool " + name);
}

ZPool::ZPool() : m_handle(nullptr)
{
}

ZPool::ZPool(zpool_handle_t * handle) : m_handle(handle)
{
}

ZPool::~ZPool()
{
	if (m_handle)
		zpool_close(m_handle);
}

ZPool::ZPool(ZPool && other) noexcept : m_handle(other.m_handle)
{
	other.m_handle = nullptr;
}

ZPool & ZPool::operator=(ZPool && other) noexcept
{
	m_handle = other.m_handle;
	other.m_handle = nullptr;
	return *this;
}

LibZFSHandle ZPool::libHandle() const
{
	return LibZFSHandle(zpool_get_handle(m_handle));
}

char const * ZPool::name() const
{
	return zpool_get_name(m_handle);
}

uint64_t ZPool::guid() const
{
	return zpool_get_prop_int(m_handle, ZPOOL_PROP_GUID, nullptr);
}

uint64_t ZPool::status() const
{
	char const * cp = nullptr;
	zpool_errata_t errata = {};
	zpool_status_t stat = zpool_get_status(m_handle, &cp, &errata);
	return stat;
}

NVList ZPool::config() const
{
	return NVList(zpool_get_config(m_handle, nullptr));
}

zfs::NVList ZPool::vdevTree() const
{
	return config().lookup<zfs::NVList>(ZPOOL_CONFIG_VDEV_TREE);
}

std::vector<zfs::NVList> ZPool::vdevs() const
{
	return vdevChildren(vdevTree());
}

std::vector<zfs::NVList> ZPool::caches() const
{
	return vdevCaches(vdevTree());
}

VDevStat ZPool::vdevStat() const
{
	VDevStat vdevStat(NVList const & vdev);
	return vdevStat(vdevTree());
}

ScanStat ZPool::scanStat() const
{
	ScanStat scanStat(NVList const & vdev);
	return scanStat(vdevTree());
}

std::string ZPool::vdevName(zfs::NVList const & vdev) const
{
	auto name_cstr =
	    zpool_vdev_name(zpool_get_handle(m_handle), m_handle, vdev.toList(), 0);
	std::string name(name_cstr);
	free(name_cstr);
	if (zfs::vdevIsLog(vdev))
		return "log: " + name;
	return name;
}

static std::string vdevDevice(
    libzfs_handle_t * libHandle,
    zpool_handle_t * poolHandle,
    NVList const & vdev)
{
	auto name_cstr = zpool_vdev_name(
	    libHandle,
	    poolHandle,
	    vdev.toList(),
	    VDEV_NAME_PATH | VDEV_NAME_FOLLOW_LINKS | VDEV_NAME_TYPE_ID);
	std::string name(name_cstr);
	free(name_cstr);
	return name;
}

std::string ZPool::vdevDevice(zfs::NVList const & vdev) const
{
	return zfs::vdevDevice(zpool_get_handle(m_handle), m_handle, vdev);
}

std::vector<ZPool::Property> ZPool::properties() const
{
	std::vector<ZPool::Property> properties;
	PropList pl = zpoolProplist(m_handle);
	ZPool::Property prop;
	for (auto p = pl.get(); p != nullptr; p = p->pl_next)
	{
		if (p->pl_prop != ZPROP_INVAL)
		{
			if (getProperty(
			        m_handle, static_cast<zpool_prop_t>(p->pl_prop), prop))
				properties.push_back(prop);
		}
		else
		{
			if (getProperty(m_handle, p->pl_user_prop, prop))
				properties.push_back(prop);
		}
	}
	return properties;
}

ZFileSystem ZPool::rootFileSystem() const
{
	auto lib = zpool_get_handle(m_handle);
	auto name = zpool_get_name(m_handle);
	auto fs = zfs_open(lib, name, ZFS_TYPE_FILESYSTEM);
	if (!fs)
		throw std::runtime_error("Unable to open root filesystem");
	return ZFileSystem(fs);
}

std::vector<ZFileSystem> ZPool::allFileSystems() const
{
	try
	{
		auto root = rootFileSystem();
		std::vector<ZFileSystem> fileSystems = root.allFileSystems();
		return fileSystems;
	}
	catch (std::runtime_error const & e)
	{
		// Ignore errors
	}
	return std::vector<ZFileSystem>{};
}

int ZPool::iterAllFileSystems(std::function<int(ZFileSystem)> callback) const
{
	return rootFileSystem().iterAllFileSystems(callback);
}

int ZPool::iterAllFileSystemsReverse(
    std::function<int(ZFileSystem)> callback) const
{
	return rootFileSystem().iterAllFileSystemsReverse(callback);
}

void ZPool::exportPool(bool force)
{
	std::stringstream ss;
	ss << "export " << (force ? "-f" : "") << name();
	boolean_t forceBT = to_boolean_t(force);
	int res = 0;
	res = zpool_disable_datasets(m_handle, forceBT);
	if (res != 0)
		libHandle().throwLastError(ss.str());
	res = zpool_export(m_handle, forceBT, ss.str().c_str());
	if (res != 0)
		libHandle().throwLastError(ss.str());
}

void ZPool::scrub()
{
	int res = 0;
	res = zpool_scan(m_handle, POOL_SCAN_SCRUB, POOL_SCRUB_NORMAL);
	if (res != 0)
		libHandle().throwLastError("scrub " + std::string(name()));
}

void ZPool::scrubPause()
{
	int res = 0;
	res = zpool_scan(m_handle, POOL_SCAN_SCRUB, POOL_SCRUB_PAUSE);
	if (res != 0)
		libHandle().throwLastError("scrub -p " + std::string(name()));
}

void ZPool::scrubStop()
{
	int res = 0;
	res = zpool_scan(m_handle, POOL_SCAN_NONE, POOL_SCRUB_NORMAL);
	if (res != 0)
		libHandle().throwLastError("scrub -s " + std::string(name()));
}

zpool_handle_t * ZPool::handle() const
{
	return m_handle;
}

bool healthy(uint64_t stat, bool allowHostIDMismatch)
{
	switch (zpool_status_t(stat))
	{
	case ZPOOL_STATUS_OK:
	case ZPOOL_STATUS_VERSION_OLDER:
	case ZPOOL_STATUS_FEAT_DISABLED:
		return true;
	case ZPOOL_STATUS_HOSTID_MISMATCH:
		return allowHostIDMismatch;
	default:
		return false;
	}
}

ZFileSystem LibZFSHandle::filesystem(std::string const & name) const
{
	int allowedTypes = ZFS_TYPE_FILESYSTEM | ZFS_TYPE_SNAPSHOT |
	                   ZFS_TYPE_VOLUME | ZFS_TYPE_POOL | ZFS_TYPE_BOOKMARK;
	auto fs = zfs_open(handle(), name.c_str(), allowedTypes);
	if (fs == nullptr)
		throw std::runtime_error("Filesystem " + name + " does not exist");
	return ZFileSystem(fs);
}

int LibZFSHandle::createFilesystem(
    std::string const & name, std::string const & mountpoint)
{
	NVList options(NVList::TakeOwnership{});
	if (!mountpoint.empty())
		options.add(zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), mountpoint);
	if (auto r = zfs_create_ancestors(m_handle, name.c_str()))
		return r;
	return zfs_create(
	    m_handle, name.c_str(), ZFS_TYPE_FILESYSTEM, options.toList());
}

int LibZFSHandle::createVolume(
    std::string const & name, std::uint64_t volumeSize, std::uint64_t blockSize)
{
	NVList options(NVList::TakeOwnership{});
	options.add(zfs_prop_to_name(ZFS_PROP_VOLSIZE), volumeSize);
	if (blockSize)
		options.add(zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), blockSize);
	if (auto r = zfs_create_ancestors(m_handle, name.c_str()))
		return r;
	return zfs_create(
	    m_handle, name.c_str(), ZFS_TYPE_VOLUME, options.toList());
}

namespace
{
	struct ZPoolCallback
	{
		ZPoolCallback(std::function<void(ZPool)> callback) :
		        m_callback(callback)
		{
		}

		int handle(zpool_handle_t * pool)
		{
			m_callback(ZPool(pool));
			return 0;
		}

		static int handle_s(zpool_handle_t * pool, void * stored_this)
		{
			return static_cast<ZPoolCallback *>(stored_this)->handle(pool);
		}

		std::function<void(ZPool)> m_callback;
	};
} // namespace

std::vector<ZPool> LibZFSHandle::pools() const
{
	std::vector<ZPool> zpools;
	ZPoolCallback cb([&](ZPool pool) { zpools.push_back(std::move(pool)); });
	zpool_iter(handle(), &ZPoolCallback::handle_s, &cb);
	std::sort(zpools.begin(), zpools.end());
	return zpools;
}

void LibZFSHandle::iterPools(std::function<void(ZPool)> callback) const
{
	ZPoolCallback cb(callback);
	zpool_iter(handle(), &ZPoolCallback::handle_s, &cb);
}

ZPool LibZFSHandle::pool(std::string const & name) const
{
	return ZPool(m_handle, name);
}

int LibZFSHandle::lastErrorCode() const
{
	return libzfs_errno(m_handle);
}

std::string LibZFSHandle::lastErrorAction() const
{
	return std::string(libzfs_error_action(m_handle));
}

std::string LibZFSHandle::lastErrorDescription() const
{
	return std::string(libzfs_error_description(m_handle));
}

std::string LibZFSHandle::lastError() const
{
	return lastErrorAction() + " (" + lastErrorDescription() + ")";
}

void LibZFSHandle::throwLastError(std::string const & action)
{
	throw std::runtime_error("Error in " + action + ": " + lastError());
}

std::vector<std::string>
LibZFSHandle::devicesFromPoolConfig(NVList const & config) const
{
	std::vector<std::string> devices;
	auto vdevtree = config.lookup<zfs::NVList>(ZPOOL_CONFIG_VDEV_TREE);
	for (auto const & directChild : vdevChildren(vdevtree))
	{
		auto type = zfs::vdevType(directChild);
		if (type == VDEV_TYPE_DISK)
		{
			devices.push_back(vdevDevice(handle(), nullptr, directChild));
		}
		else
		{
			for (auto const & indirectChild : vdevChildren(directChild))
			{
				auto type = zfs::vdevType(indirectChild);
				if (type == VDEV_TYPE_DISK)
				{
					devices.push_back(
					    vdevDevice(handle(), nullptr, indirectChild));
				}
			}
		}
	}
	return devices;
}

/*!
 Requires that the lifetime of the passed searchPaths is at least
 as long as that of the passed args, and that the returned vector
 is also kept alive until args is discarded.
 */
[[nodiscard]] std::vector<char *> setImportSearchPaths(
    importargs_t * args, std::vector<std::string> const & searchPaths)
{
	std::vector<char *> pointers;
	if (searchPaths.empty())
		return pointers;
	pointers.reserve(searchPaths.size());
	for (auto const & path : searchPaths)
	{
		// The data shouldn't be modified, but the C API is not
		// const correct, so const cast is required
		pointers.push_back(const_cast<char *>(path.data()));
	}
	args->path = pointers.data();
	args->paths = static_cast<int>(pointers.size());
	return pointers;
}

std::vector<ImportablePool> LibZFSHandle::importablePools(
    std::vector<std::string> const & searchPathOverride) const
{
	importargs_t args = {};
	auto keep = setImportSearchPaths(&args, searchPathOverride);
	libpc_handle_t lpch = {
		.lpc_lib_handle = m_handle,
		.lpc_ops = &libzfs_config_ops,
	};
	auto list = NVList(
	    zpool_search_import(&lpch, &args),
	    zfs::NVList::TakeOwnership());
	std::vector<ImportablePool> pools;
	for (auto pair : list)
	{
		auto l = pair.convertTo<NVList>();
		uint64_t poolState = l.lookup<uint64_t>(ZPOOL_CONFIG_POOL_STATE);
		if (poolState == POOL_STATE_DESTROYED)
			continue; // Ignore destroyed pools
		char const * msg = nullptr;
		zpool_errata_t errata = {};
		zpool_status_t status = zpool_import_status(l.toList(), &msg, &errata);
		pools.push_back({
		    pair.name(),
		    l.lookup<uint64_t>(ZPOOL_CONFIG_POOL_GUID),
		    status,
		    devicesFromPoolConfig(l),
		});
	}
	std::sort(pools.begin(), pools.end());
	return pools;
}

static std::vector<ZPool> import_with_args(
    LibZFSHandle const & lib,
    importargs_t * args,
    LibZFSHandle::ImportProps const & props)
{
	auto keep = setImportSearchPaths(args, props.searchPathOverride);
	libpc_handle_t lpch = {
		.lpc_lib_handle = lib.handle(),
		.lpc_ops = &libzfs_config_ops,
	};
	auto list = NVList(
	    zpool_search_import(&lpch, args),
	    zfs::NVList::TakeOwnership());
	std::vector<ZPool> pools;
	for (auto pair : list)
	{
		auto l = pair.convertTo<NVList>();
		uint64_t poolState = l.lookup<uint64_t>(ZPOOL_CONFIG_POOL_STATE);
		if (poolState == POOL_STATE_DESTROYED)
			continue; // Ignore destroyed pools
		char const * msg = nullptr;
		zpool_errata_t errata = {};
		zpool_status_t status = zpool_import_status(l.toList(), &msg, &errata);
		if (!props.allowUnhealthy &&
		    !healthy(status, props.allowHostIDMismatch))
			continue; // Ignore pools that aren't healthy

		NVList propsNVL(NVList::TakeOwnership{});
		char const * tempname = nullptr;
		int importFlags = ZFS_IMPORT_NORMAL;
		if (!props.altroot.empty())
		{
			propsNVL.add(zpool_prop_to_name(ZPOOL_PROP_CACHEFILE), "none");
			propsNVL.add(zpool_prop_to_name(ZPOOL_PROP_ALTROOT), props.altroot);
		}
		if (!props.newName.empty())
		{
			propsNVL.add(zpool_prop_to_name(ZPOOL_PROP_CACHEFILE), "none");
			tempname = props.newName.data();
		}
		if (props.readOnly.has_value())
		{
			propsNVL.add(
			    zpool_prop_to_name(ZPOOL_PROP_READONLY),
			    *props.readOnly ? "on" : "off");
		}

		auto r = zpool_import_props(
		    lib.handle(), l.toList(), tempname, propsNVL.toList(), importFlags);
		if (r)
		{
			throw std::runtime_error(
			    "Error importing pool " + pair.name() + ": " + lib.lastError());
		}
		ZPool importedPool = lib.pool(pair.name());
		r = zpool_enable_datasets(importedPool.handle(), nullptr, 0, 0);
		pools.push_back(std::move(importedPool));
		if (r)
		{
			throw std::runtime_error(
			    "Error enabling datasets: " + lib.lastError());
		}
	}
	return pools;
}

std::vector<ZPool>
LibZFSHandle::importAllPools(LibZFSHandle::ImportProps const & props) const
{
	importargs_t args = {};
	return import_with_args(*this, &args, props);
}

ZPool LibZFSHandle::import(
    std::string const & name, LibZFSHandle::ImportProps const & props) const
{
	importargs_t args = {};
	args.poolname = const_cast<char *>(name.c_str());
	auto pools = import_with_args(*this, &args, props);
	if (pools.size() != 1)
		throw std::runtime_error("Invalid number of pools imported");
	return std::move(pools.front());
}

ZPool LibZFSHandle::import(
    uint64_t guid, LibZFSHandle::ImportProps const & props) const
{
	importargs_t args = {};
	args.guid = guid;
	auto pools = import_with_args(*this, &args, props);
	if (pools.size() != 1)
		throw std::runtime_error("Invalid number of pools imported");
	return std::move(pools.front());
}

std::string vdevType(NVList const & vdev)
{
	return vdev.lookup<std::string>(ZPOOL_CONFIG_TYPE);
}

bool vdevIsLog(NVList const & vdev)
{
	uint64_t isLog = 0;
	vdev.lookup(ZPOOL_CONFIG_IS_LOG, isLog);
	return isLog;
}

uint64_t vdevGUID(NVList const & vdev)
{
	return vdev.lookup<uint64_t>(ZPOOL_CONFIG_GUID);
}

uint64_t poolGUID(NVList const & vdev)
{
	return vdev.lookup<uint64_t>(ZPOOL_CONFIG_POOL_GUID);
}

std::vector<NVList> vdevChildren(NVList const & vdev)
{
	std::vector<zfs::NVList> children;
	vdev.lookup<std::vector<zfs::NVList>>(ZPOOL_CONFIG_CHILDREN, children);
	return children;
}

std::vector<NVList> vdevCaches(NVList const & vdev)
{
	std::vector<zfs::NVList> caches;
	vdev.lookup<std::vector<zfs::NVList>>(ZPOOL_CONFIG_L2CACHE, caches);
	return caches;
}

VDevStat vdevStat(NVList const & vdev)
{
	auto statVec = vdev.lookup<std::vector<uint64_t>>(ZPOOL_CONFIG_VDEV_STATS);
	vdev_stat_t zfsStat = {};
	if (sizeof(zfsStat) != statVec.size() * sizeof(uint64_t))
		throw std::logic_error(
		    "Internal nvlist structure size does not match vdev_stat_t size");
	// Note: this is somewhat non-portable but the equivalent C Code does the
	// same
	std::copy(
	    statVec.begin(), statVec.end(), reinterpret_cast<uint64_t *>(&zfsStat));
	VDevStat interfaceStat = {
	    zfsStat.vs_state,
	    zfsStat.vs_aux,
	    zfsStat.vs_alloc,
	    zfsStat.vs_space,
	    zfsStat.vs_dspace,
	    zfsStat.vs_read_errors,
	    zfsStat.vs_write_errors,
	    zfsStat.vs_checksum_errors,
	    zfsStat.vs_fragmentation};
	return interfaceStat;
}

ScanStat scanStat(NVList const & vdev)
{
	ScanStat interfaceStat = {ScanStat::funcNone, ScanStat::stateNone};
	std::vector<uint64_t> statVec;
	if (!vdev.lookup<std::vector<uint64_t>>(ZPOOL_CONFIG_SCAN_STATS, statVec))
		return interfaceStat;
	pool_scan_stat_t scanStat = {};
	// Internal nvlist structure is inconsistent, not critical
	if (sizeof(scanStat) != statVec.size() * sizeof(uint64_t))
		return interfaceStat;
	// Note: this is somewhat non-portable but the equivalent C Code does the
	// same
	std::copy(
	    statVec.begin(),
	    statVec.end(),
	    reinterpret_cast<uint64_t *>(&scanStat));
	interfaceStat.func = static_cast<ScanStat::Func>(scanStat.pss_func);
	interfaceStat.state = static_cast<ScanStat::State>(scanStat.pss_state);
	interfaceStat.total = scanStat.pss_to_examine;
	interfaceStat.scanned = scanStat.pss_examined;
	interfaceStat.issued = scanStat.pss_issued;
	interfaceStat.passScanned = scanStat.pss_pass_exam;
	interfaceStat.passIssued = scanStat.pss_pass_issued;
	interfaceStat.passStartTime = scanStat.pss_pass_start;
	interfaceStat.passPauseTime = scanStat.pss_pass_scrub_pause;
	interfaceStat.passPausedSeconds = scanStat.pss_pass_scrub_spent_paused;
	interfaceStat.errors = scanStat.pss_errors;
	interfaceStat.scanStartTime = scanStat.pss_start_time;
	interfaceStat.scanEndTime = scanStat.pss_end_time;
	return interfaceStat;
}

} // namespace zfs
