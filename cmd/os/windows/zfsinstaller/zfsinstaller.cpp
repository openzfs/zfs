
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License(the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2018 Julian Heuking <J.Heuking@beckhoff.com>
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>
 */

extern "C" {
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
extern char *optarg;
extern int optind;

#include <sys/fs/zfs.h>
// kernel header'
// #include <sys/zfs_ioctl_compat.h>
#define	ZFSIOCTL_BASE 0x800

#include <strsafe.h>
#include <cfgmgr32.h>
#include <newdev.h>

#define	ZFS_ROOTDEV "ROOT\\OpenZFS"
#define	ZVOL_ROOTDEV "ROOT\\OpenZVOL"
// DevCon uses LoadLib() - but lets just static link
#pragma comment(lib, "Newdev.lib")

#include <setupapi.h>
#pragma comment(lib, "setupapi.lib")


}

#include "zfsinstaller.h"

#include <ctime>
#include <string>
#include <filesystem>
namespace fs = std::filesystem;

#include <devguid.h>

#define	MAX_PATH_LEN 1024

//  Usage:
//    zfsinstaller install [inf] [installFolder]
//			defaults to something like %ProgramFiles%\ZFS)
//    zfsinstaller uninstall [inf] (could default to something
//			like %ProgramFiles%\ZFS\OpenZFS.inf)
//    zfsinstaller trace [-f Flags] [-l Levels] [-s SizeOfETLInMB]
//			[-p AbsolutePathOfETL]
//    zfsinstaller trace -d

const unsigned char OPEN_ZFS_GUID[] = "c20c603c-afd4-467d-bf76-c0a4c10553df";
const unsigned char LOGGER_SESSION[] = "autosession\\OpenZFS_trace";
const std::string ETL_FILE("\\OpenZFS.etl");
const std::string MANIFEST_FILE("\\OpenZFS.man");

enum manifest_install_types
{
	MAN_INSTALL,
	MAN_UNINSTALL,
};

void clean_extra_installs(void);
void CleanupOpenZFSDriverPackages(void);
bool install_zed(void);
bool uninstall_zed(void);
int zfs_preflight(void);
int zfs_postflight(void);

bool reboot_indicated = false;

int
session_exists(void)
{
	char   command[MAX_PATH_LEN];

	sprintf_s(command, "logman query %s > nul", LOGGER_SESSION);

	return (system(command));  // returns 0 if Session exists
	// else non-zero if Session does not exist
}

int
zfs_log_session_delete(void)
{
	char command[MAX_PATH_LEN];

	int ret = session_exists();

	if (ret == 0) {  // Session exists
		sprintf_s(command, "logman delete %s > nul",
			LOGGER_SESSION);
		ret = system(command);
		if (ret == 0)
			fprintf(stderr, "Logman session %s deleted "
				"successfully\n", LOGGER_SESSION);
		else
			fprintf(stderr, "Error while deleting session %s\n",
				LOGGER_SESSION);
		return (ret);
	} else
		return (0); // Session does not exist ; We will pass success
}

int
validate_flag_level(const char *str, size_t len)
{
	if ((str[0] == '0') && (str[1] == 'x' || str[1] == 'X')) str += 2;

	while (*str == '0') ++str;

	size_t length = strlen(str);
	if (length > len)
		return (1);
	if (*str == '-')
		return (2);
	if (str[strspn(str, "0123456789abcdefABCDEF")] == 0)
		return (0);   // Vaild hex string;
	else
		return (3);   // Invalid hex string;
}

int
validate_args(const char *flags, const char *levels,
	int size_in_mb, const char *etl_file)
{
	if (validate_flag_level(flags, 8)) {
		fprintf(stderr, "Valid input for flags should be in "
			"interval [0x0, 0xffffffff]\n");
		return (1);
	}

	if (validate_flag_level(levels, 2)) {
		fprintf(stderr, "Valid input for levels should be in "
			"interval [0x0, 0xff]\n");
		return (2);
	}

	if (etl_file) {
		if (!strstr(etl_file, ".etl")) {
			fprintf(stderr, "Etl file path/name %s is incorrect\n",
				etl_file);
			return (3);
		}
	} else {
			fprintf(stderr, "Etl file path/name is incorrect\n");
			return (4);
	}

	if (size_in_mb <= 0) {
		fprintf(stderr, "Size of etl should be greater than 0\n");
		return (5);
	}
	return (0);
}

int
move_file(const char *etl_file)
{
	char move_etl[MAX_PATH_LEN];
	time_t rawtime;
	struct tm timeinfo;
	char buffer[25];

	time(&rawtime);
	localtime_s(&timeinfo, &rawtime);
	strftime(buffer, sizeof (buffer), "_%Y%m%d%H%M%S", &timeinfo);

	strcpy_s(move_etl, etl_file);
	char *etl = strstr(move_etl, ".etl");
	if (etl)
		etl[0] = 0;

	strcat_s(move_etl, buffer);
	strcat_s(move_etl, ".etl");

	if (0 == rename(etl_file, move_etl)) {
		fprintf(stderr, "%s already exists\n", etl_file);
		fprintf(stderr, "%s has been renamed to %s\n",
			etl_file, move_etl);
		return (0);
	} else {
		fprintf(stderr, "Error while renaming the file %s\n", etl_file);
		return (1);
	}
}

void
hex_modify(std::string& hex)
{
	if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
		return;
	hex = std::string("0x") + hex;
}

std::string get_cwd()
{
	CHAR cwd_path[MAX_PATH_LEN] = { 0 };
	DWORD len = GetCurrentDirectoryA(MAX_PATH_LEN, cwd_path);
	(void)len;
	return (std::string(cwd_path));
}

int
arg_parser(int argc, char **argv, std::string &flags,
	std::string &levels, int &size_in_mb, std::string &etl_file)
{
	int option_index = 0;
	while ((option_index = getopt(argc, argv, "l:f:s:p:d")) != -1) {
		switch (option_index) {
		case 'p':
			etl_file = std::string(optarg);
			break;
		case 'l':
			levels = std::string(optarg);
			hex_modify(levels);
			break;
		case 'f':
			flags = std::string(optarg);
			hex_modify(flags);
			break;
		case 's':
			size_in_mb = atoi(optarg);
			break;
		case 'd':
			fprintf(stderr, "-d cannot used with other "
				"parameters\n");
			return (1);
		default:
			fprintf(stderr, "Incorrect argument provided\n");
			return (ERROR_BAD_ARGUMENTS);
		}
	}
	int index = optind;
	while (index < argc) {
		fprintf(stderr, "Non-option argument %s\n", argv[index]);
		index++;
	}
	if (optind < argc)
		return (ERROR_BAD_ARGUMENTS);

	if (0 == flags.size())		flags = std::string("0xffffffff");
	if (0 == levels.size())		levels = std::string("0x4");
	if (-1 == size_in_mb)		size_in_mb = 250;
	if (0 == etl_file.size()) {
		TCHAR CurrentPath[MAX_PATH_LEN + 1] = L"";
		DWORD len = GetCurrentDirectory(MAX_PATH_LEN, CurrentPath);
		int size_needed = WideCharToMultiByte(CP_UTF8, 0,
			&CurrentPath[0], MAX_PATH_LEN, NULL, 0, NULL, NULL);
		std::string CwdPath(size_needed, 0);
		WideCharToMultiByte(CP_UTF8, 0, &CurrentPath[0],
			MAX_PATH_LEN, &CwdPath[0], size_needed, NULL, NULL);
		CwdPath.erase(len);
		etl_file = get_cwd() + ETL_FILE;
	}
	return (0);
}

void
sanitize(char *s)
{
	static char ok_chars[] = "abcdefghijklmnopqrstuvwxyz"
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "1234567890_-.@";
	char *cp = s;
	const char *end = s + strlen(s);
	for (cp += strspn(cp, ok_chars); cp != end; cp += strspn(cp, ok_chars)) {
		*cp = '_';
	}
}

int
zfs_log_session_create(int argc, char **argv)
{
	std::string flags;
	std::string levels;
	int size_in_mb = -1;
	std::string etl_file;
	char command[MAX_PATH_LEN];
	int ret;

	ret = arg_parser(argc, argv, flags, levels, size_in_mb, etl_file);
	if (ret) {
		printUsage();
		return (ret);
	}

	if (validate_args(flags.c_str(), levels.c_str(), size_in_mb,
		etl_file.c_str())) {
		fprintf(stderr, "Please check the provided values for "
			"the arguments\n");
		printUsage();
		return (1);
	}

	if (0 != session_exists()) { // If Session does not exist
		if (GetFileAttributesA(etl_file.c_str()) !=
			INVALID_FILE_ATTRIBUTES) { // ETL EXISTS
			ret = move_file(etl_file.c_str());
			if (ret)
				return (ret);
		}

		snprintf(command, MAX_PATH_LEN,
			"logman create trace %s -p {%s} %s %s"
			" -nb 1 1 -bs 1 -mode Circular -max %d -o \"%s\" ",
			LOGGER_SESSION, OPEN_ZFS_GUID, flags.c_str(),
			levels.c_str(), size_in_mb, etl_file.c_str());

		ret = system(command);
		if (ret != 0)
			fprintf(stderr, "There is an issue creating the "
				"session %s\n", LOGGER_SESSION);
		else
			fprintf(stderr, "Logman Session %s successfully "
				"created\n", LOGGER_SESSION);

		return (ret);
	} else
		fprintf(stderr, "Logman Session %s already exists\n",
			LOGGER_SESSION);

	return (0);
}

int
perf_counters(char *inf_path, int type)
{
	fs::path path = std::string(inf_path);
	std::string final_path;

	char driver_path[MAX_PATH_LEN] = { 0 };
	strncpy_s(driver_path, inf_path, MAX_PATH_LEN);
	char *slash = strrchr(driver_path, '\\');
	if (slash)
		*slash = '\0';
	else  
		slash = strrchr(driver_path, '/');
	if (slash)
		*slash = '\0';

	if (path.is_absolute())
		final_path = std::string(driver_path) + MANIFEST_FILE;
	else if (path.is_relative())
		final_path = get_cwd() + std::string("\\") + std::string(
			driver_path) + MANIFEST_FILE;
	else {
		fprintf(stderr, "Cannot determine path type for %s\n", inf_path);
		return (1);
	}

	char command[MAX_PATH_LEN] = { 0 };
	switch (type)
	{
	case MAN_INSTALL:
		sprintf_s(command, "lodctr /m:\"%s\"\n", final_path.c_str());
		break;
	case MAN_UNINSTALL:
		sprintf_s(command, "unlodctr /m:\"%s\"\n", final_path.c_str());
		break;
	default:
		break;
	}

	fprintf(stderr, "Executing %s\n", command);
	return (system(command));
}

int
perf_counters_install(char *inf_path)
{
	return (perf_counters(inf_path, MAN_INSTALL));
}


int
perf_counters_uninstall(char *inf_path)
{
	return (perf_counters(inf_path, MAN_UNINSTALL));
}

int
main(int argc, char *argv[])
{
	int ret = 0;

	if (argc < 2) {
		fprintf(stderr, "too few arguments \n");
		printUsage();
		return (ERROR_BAD_ARGUMENTS);
	}
	if (argc > 10) {
		fprintf(stderr, "too many arguments \n");
		printUsage();
		return (ERROR_BAD_ARGUMENTS);
	}

	// ew manual arg parsing when we have getopt?

	// Always uninstall, even for install.
	bool do_uninstall = false;
	bool do_install = false;
	bool zed_service = false;

	if (strcmp(argv[1], "uninstall") == 0) {
		do_uninstall = true;
	}

	if (strcmp(argv[1], "install") == 0) {
		do_uninstall = true;
		do_install = true;
	}

	if (do_uninstall || do_install) {
		if ((argc == 5) && strcmp(argv[2], "-z") == 0) {
			zed_service = true;
			argv++;
			argc--;
		}
	}

	if (do_uninstall) {
		if (argc == 4) {
			if (zed_service)
				uninstall_zed();

			ret = zfs_uninstall(argv[2]);
			if (0 == ret)
				ret = zvol_uninstall(argv[3]);
			if (0 == ret)
				zfs_log_session_delete();

			for (int timeout = 0; timeout < 20; timeout++) {
				// Wait for the service to stop
				SC_HANDLE schSCManager = OpenSCManager(
					NULL, NULL, SC_MANAGER_CONNECT);
				if (schSCManager == NULL)
					break;
				SC_HANDLE schService = OpenServiceA(
					schSCManager, "OpenZFS",
					SERVICE_QUERY_STATUS);
				if (schService == NULL) {
					CloseServiceHandle(schSCManager);
					break;
				}
				SERVICE_STATUS_PROCESS ssStatus;
				DWORD dwBytesNeeded;
				if (!QueryServiceStatusEx(
					schService,
					SC_STATUS_PROCESS_INFO,
					(LPBYTE)&ssStatus,
					sizeof(SERVICE_STATUS_PROCESS),
					&dwBytesNeeded)) {
					CloseServiceHandle(schService);
					CloseServiceHandle(schSCManager);
					break;
				}
				if (ssStatus.dwCurrentState ==
					SERVICE_STOPPED) {
					CloseServiceHandle(schService);
					CloseServiceHandle(schSCManager);
					break;
				}
				CloseServiceHandle(schService);
				CloseServiceHandle(schSCManager);
				fprintf(stderr,
					"Waiting for OpenZFS service to stop...\n");
				sleep(1);
			}

			printf("[1/4] Cleaning up extra installs of OpenZFS and OpenZVOL\n");
			clean_extra_installs();
			CleanupOpenZFSDriverPackages();

			printf("[4/4] Completed.\n");
		} else {
			fprintf(stderr, "Incorrect argument usage\n");
			printUsage();
			return (ERROR_BAD_ARGUMENTS);
		}
	}

	if (do_install) {

		if (do_uninstall)
			sleep(5);

		if (argc == 4) {
			zfs_install(argv[2]);
			zvol_install(argv[3]);
			if (zed_service)
				install_zed();
			fprintf(stderr, "Installation done.");
		} else {
			fprintf(stderr, "Incorrect argument usage\n");
			printUsage();
			return (ERROR_BAD_ARGUMENTS);
		}
	}

	if (do_uninstall || do_install) {
		if (!ret && reboot_indicated) {
			return (ERROR_SUCCESS_REBOOT_REQUIRED);
		}
		return (ret);
	}

	if (strcmp(argv[1], "trace") == 0) {
		if (argc == 3 && strcmp(argv[2], "-d") == 0)
			return (zfs_log_session_delete());
		else
			return (zfs_log_session_create(argc - 1, &argv[1]));

	} else if (strcmp(argv[1], "preflight") == 0) {

		ret = zfs_preflight();

	} else if (strcmp(argv[1], "postflight") == 0) {

		ret = zfs_postflight();

	} else {
		fprintf(stderr, "unknown argument %s\n", argv[1]);
		printUsage();
		return (ERROR_BAD_ARGUMENTS);
	}

	return (ret);
}

void
printUsage()
{
	fprintf(stderr, "\nUsage:\n\n");
	fprintf(stderr, "Install driver per INF DefaultInstall section:\n");
	fprintf(stderr, "zfsinstaller install [-z] OpenZFS.inf OpenZVOL.inf\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Uninstall driver per INF DefaultUninstall section:\n");
	fprintf(stderr, "zfsinstaller uninstall [-z] OpenZFS.inf OpenZVOL.inf\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Use -z to also install/uninstall zed service\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "zfsinstaller trace [-f Flags] | [-l Levels]"
		" | [-s SizeOfETLInMB] | [-p AbsolutePathOfETL]\n");
	fprintf(stderr, "Valid inputs for above arguments are as follows:\n");
	fprintf(stderr, "Flags (in hex)              "
		"Should be in interval [0x0, 0xffffffff]      "
		"Default (0xffffffff)\n");
	fprintf(stderr, "Levels (in hex)             "
		"Should be in interval [0x0, 0xff]            "
		"Default (0x4)\n");
	fprintf(stderr, "SizeOfETLInMB (in decimal)  "
		"Should be greater than 0                     "
		"Default (250)\n");
	fprintf(stderr, "AbsolutePathOfETL           "
		"Absolute Path including the Etl file name    "
		"Default ($CWD%s)\n", ETL_FILE.c_str());
	fprintf(stderr, "\n");
	fprintf(stderr, "zfsinstaller trace -d\n");
	fprintf(stderr, "-d                 To delete the logman session\n");
}

HDEVINFO
openDeviceInfo(char *inf, GUID *ClassGUID, char *ClassName, int namemax)
{
	HDEVINFO DeviceInfoSet = INVALID_HANDLE_VALUE;
	char InfPath[MAX_PATH];

	// Inf must be a full pathname
	if (GetFullPathNameA(inf, MAX_PATH, InfPath, NULL) >= MAX_PATH) {
		// inf pathname too long
		goto final;
	}

	// Use the INF File to extract the Class GUID.
	if (!SetupDiGetINFClassA(InfPath, ClassGUID, ClassName,
		namemax, 0)) {
		goto final;
	}

	// Create the container for the to-be-created
	// Device Information Element.
	DeviceInfoSet = SetupDiCreateDeviceInfoList(ClassGUID, 0);
	if (DeviceInfoSet == INVALID_HANDLE_VALUE) {
		goto final;
	}

	return (DeviceInfoSet);

	final:
	return (NULL);
}

DWORD
installRootDevice(char *inf, bool IsServiceRunning, const char *rootdev)
{
	HDEVINFO DeviceInfoSet = INVALID_HANDLE_VALUE;
	SP_DEVINFO_DATA DeviceInfoData;
	char hwIdList[LINE_LEN + 4];
	GUID ClassGUID;
	char ClassName[MAX_CLASS_NAME_LEN];
	int failcode = 12;

	DWORD flags = INSTALLFLAG_FORCE;
	BOOL reboot = FALSE;

	DeviceInfoSet = openDeviceInfo(inf, &ClassGUID, ClassName,
		MAX_CLASS_NAME_LEN);

	ZeroMemory(hwIdList, sizeof(hwIdList));
	if (FAILED(StringCchCopyA(hwIdList, LINE_LEN, rootdev))) {
		goto final;
	}

	// Now create the element.
	// Use the Class GUID and Name from the INF file.
	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	if (!SetupDiCreateDeviceInfoA(DeviceInfoSet,
		ClassName,
		&ClassGUID,
		NULL,
		0,
		DICD_GENERATE_ID,
		&DeviceInfoData)) {
		goto final;
	}

	// Add the HardwareID to the Device's HardwareID property.
	if (!SetupDiSetDeviceRegistryPropertyA(DeviceInfoSet,
		&DeviceInfoData,
		SPDRP_HARDWAREID,
		(LPBYTE)hwIdList,
		(DWORD)(strlen(hwIdList) + 1 + 1) * sizeof(char))) {
		goto final;
	}

	// If service is running.
	if (!IsServiceRunning) {
		//Transform the registry element into an actual devnode
		//in the PnP HW tree.
		if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE,
			DeviceInfoSet,
			&DeviceInfoData)) {
			goto final;
		}
	}

	failcode = 0;

	// According to devcon we also have to Update now as well.
	if (!UpdateDriverForPlugAndPlayDevicesA(NULL, rootdev, inf, flags,
	    &reboot)) {
		fprintf(stderr, "%s: UpdateDriverForPlugAndPlayDevicesA "
		    "failed: %lu\n", __func__, GetLastError());
	}

	if (reboot) {
		reboot_indicated = true;
		printf("Windows indicated a Reboot is required.\n");
	}

	final:

	if (DeviceInfoSet != INVALID_HANDLE_VALUE) {
		SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	}
	printf("%s: exit %d:0x%x\n", __func__, failcode, failcode);

	return (failcode);
}

static DWORD
Utf8ToWide(std::wstring &out, const char *in)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, in, -1, nullptr, 0);
	if (n <= 0)
		return (GetLastError());
	out.assign(n - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, in, -1, &out[0], n);
	return (ERROR_SUCCESS);
}

DWORD
installZVOLDevice(const char *infPathUtf8, BOOL /*ignored*/, const char * /*unused*/)
{
	// --- 0) Convert INF path and stage the package to get the published oemXX.inf ---
	std::wstring infW;
	if (DWORD e = Utf8ToWide(infW, infPathUtf8))
		return (e);

	WCHAR oemInf[MAX_PATH] = {};
	if (!SetupCopyOEMInfW(infW.c_str(),
		nullptr,        // source root
		SPOST_NONE,
		0,              // copy style
		oemInf,
		ARRAYSIZE(oemInf),
		nullptr,
		nullptr)) {
		DWORD e = GetLastError();
		fprintf(stderr, "SetupCopyOEMInfW failed: %lx\n", e);
		return (e);
	}

	// --- 1) Read the class from the staged INF (don’t hardcode) ---
	GUID infClassGuid{};
	WCHAR infClassName[64];
	if (!SetupDiGetINFClassW(oemInf, &infClassGuid, infClassName, ARRAYSIZE(infClassName), nullptr)) {
		DWORD e = GetLastError();
		fprintf(stderr, "SetupDiGetINFClassW failed: %lx\n", e);
		return (e);
	}

	// --- 2) Create a device info list in that class ---
	HDEVINFO hdi = SetupDiCreateDeviceInfoList(&infClassGuid, nullptr);
	if (hdi == INVALID_HANDLE_VALUE) {
		DWORD e = GetLastError();
		fprintf(stderr, "SetupDiCreateDeviceInfoList failed: %lx\n", e);
		return (e);
	}

	SP_DEVINFO_DATA dev{};
	dev.cbSize = sizeof (dev);

	// Create a root-enumerated devnode; Windows will generate \0000
	if (!SetupDiCreateDeviceInfoW(hdi,
		L"OpenZVOL",         // base name (NOT a devinst id)
		&infClassGuid,
		L"OpenZVOL",
		nullptr,
		DICD_GENERATE_ID,
		&dev)) {
		DWORD e = GetLastError();
		fprintf(stderr, "SetupDiCreateDeviceInfoW failed: %lx\n", e);
		SetupDiDestroyDeviceInfoList(hdi);
		return (e);
	}

	// --- 3) Set HWIDs (MULTI_SZ) and friendly name ---
	wchar_t hwids[] = L"ROOT\\OpenZVOL\0\0";
	if (!SetupDiSetDeviceRegistryPropertyW(hdi, &dev, SPDRP_HARDWAREID,
		reinterpret_cast<const BYTE *>(hwids),
		sizeof (hwids)))
	{
		DWORD e = GetLastError();
		fprintf(stderr, "Set HARDWAREID failed: %lx\n", e);
		SetupDiDestroyDeviceInfoList(hdi);
		return (e);
	}

	(void)SetupDiSetDeviceRegistryPropertyW(hdi, &dev, SPDRP_FRIENDLYNAME,
		reinterpret_cast<const BYTE *>(L"OpenZVOL"),
		DWORD((wcslen(L"OpenZVOL") + 1) * sizeof (WCHAR)));

	// --- 4) Register so the devnode becomes present (creates Enum\ROOT\OPENZVOL\0000) ---
	if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, hdi, &dev)) {
		DWORD e = GetLastError();
		fprintf(stderr, "DIF_REGISTERDEVICE failed: %lx\n", e);
		SetupDiDestroyDeviceInfoList(hdi);
		return (e);
	}

	// (Optional) show the actual instance id
	WCHAR instId[256];
	if (SetupDiGetDeviceInstanceIdW(hdi, &dev, instId, ARRAYSIZE(instId), nullptr)) {
		fwprintf(stderr, L"OpenZVOL devinst: %s\n", instId);
	}

	// --- 5) Bind the driver from *this* INF using UpdateDriverForPlugAndPlayDevicesW ---
	// This avoids SelectBestCompatDrv pitfalls (wrong list kind, class/HWID mismatch, etc.)
	BOOL reboot = FALSE;
	if (!UpdateDriverForPlugAndPlayDevicesW(nullptr,
		L"ROOT\\OpenZVOL",
		oemInf,                  // published oemXX.inf
		INSTALLFLAG_FORCE /*| INSTALLFLAG_NONINTERACTIVE*/,
		&reboot)) {
		DWORD e = GetLastError();
		fprintf(stderr, "UpdateDriverForPlugAndPlayDevicesW failed: %lx\n", e);
		SetupDiDestroyDeviceInfoList(hdi);
		return (e);
	}

	// --- 6) Start the devnode now (PnP would usually do this anyway) ---
	SP_PROPCHANGE_PARAMS pcp{};
	pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
	pcp.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
	pcp.StateChange = DICS_START;
	pcp.Scope = DICS_FLAG_GLOBAL;
	pcp.HwProfile = 0;

	if (SetupDiSetClassInstallParamsW(hdi, &dev,
		reinterpret_cast<SP_CLASSINSTALL_HEADER *>(&pcp), sizeof (pcp))) {
		(void)SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hdi, &dev);
	}

	SetupDiDestroyDeviceInfoList(hdi);
	fprintf(stderr, "installZVOLDevice completed successfully\n");
	return (ERROR_SUCCESS);
}

DWORD
uninstallRootDevice(char *inf, const char *rootdev)
{
	(void)inf;
	int failcode = 13;
	HDEVINFO DeviceInfoSet = INVALID_HANDLE_VALUE;
	SP_DEVINFO_DATA DeviceInfoData;
	DWORD DataT;
	char *p, *buffer = NULL;
	DWORD buffersize = 0;

	printf("%s: \n", __func__);

	DeviceInfoSet = SetupDiGetClassDevs(NULL, // All Classes
		0, 0, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	// All devices present on system
	if (DeviceInfoSet == INVALID_HANDLE_VALUE)
		goto final;

	printf("%s: looking for device rootnode to remove...\n", __func__);

	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	for (int i = 0; SetupDiEnumDeviceInfo(DeviceInfoSet, i,
		&DeviceInfoData); i++) {
		// Call once to get buffersize
		while (!SetupDiGetDeviceRegistryPropertyA(
			DeviceInfoSet,
			&DeviceInfoData,
			SPDRP_HARDWAREID,
			&DataT,
			(PBYTE)buffer,
			buffersize,
			&buffersize)) {

			if (GetLastError() == ERROR_INVALID_DATA) {
				// May be a Legacy Device with no HardwareID. Continue.
				break;
			} else if (GetLastError() ==
				ERROR_INSUFFICIENT_BUFFER) {
				// We need to change the buffer size.
				if (buffer)
					free(buffer);
				buffer = (char *)malloc(buffersize);
				if (buffer) ZeroMemory(buffer, buffersize);
			} else {
				// Unknown Failure.
				free(buffer);
				buffer = NULL;
				goto final;
			}
		}

		if (GetLastError() == ERROR_INVALID_DATA)
			continue;

		// Compare each entry in the buffer multi-sz list
		// with our HardwareID.
		for (p = buffer; *p && (p < &buffer[buffersize]);
			p += strlen(p) + sizeof(char)) {
			// printf("%s: comparing '%s' with '%s'\n",
			//	 __func__, "ROOT\\ZFSin", p);
			if (!_stricmp(rootdev, p)) {

				printf("%s: device found, removing ... \n",
					__func__);

				// Worker function to remove device.
				if (SetupDiCallClassInstaller(DIF_REMOVE,
					DeviceInfoSet, &DeviceInfoData)) {
					failcode = 0;
				}
				break;
			}
		}

		if (buffer) free(buffer);
		buffer = NULL;
		buffersize = 0;
	}

	final:

	if (DeviceInfoSet != INVALID_HANDLE_VALUE) {
		SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	}
	printf("%s: exit %d:0x%x\n", __func__, failcode, failcode);

	return (failcode);
}

// --- helpers ---------------------------------------------------------------

static bool GetStringPropW(HDEVINFO hdi, SP_DEVINFO_DATA *dev, DWORD prop, std::wstring &out)
{
	WCHAR buf[1024]; DWORD req = 0;
	if (!SetupDiGetDeviceRegistryPropertyW(hdi, dev, prop, nullptr, (PBYTE)buf, sizeof(buf), &req))
		return false;
	out.assign(buf);
	return true;
}

static void DisableDeviceNode(HDEVINFO hdi, SP_DEVINFO_DATA *dev)
{
	SP_PROPCHANGE_PARAMS pcp{};
	pcp.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
	pcp.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
	pcp.StateChange = DICS_DISABLE;
	pcp.Scope = DICS_FLAG_GLOBAL;
	pcp.HwProfile = 0;

	if (SetupDiSetClassInstallParamsW(hdi, dev,
		reinterpret_cast<SP_CLASSINSTALL_HEADER *>(&pcp), sizeof(pcp)))
	{
		(void)SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hdi, dev);
	}
}

static CONFIGRET QueryAndRemoveByInstanceId(PCWSTR instanceId, PNP_VETO_TYPE *vetoType, std::wstring &vetoName)
{
	DEVINST dn = 0;
	CONFIGRET cr = CM_Locate_DevNodeW(&dn, const_cast<PWSTR>(instanceId), CM_LOCATE_DEVNODE_NORMAL);
	if (cr != CR_SUCCESS) return cr;

	WCHAR name[256] = {};
	cr = CM_Query_And_Remove_SubTreeW(dn, vetoType, name, ARRAYSIZE(name), 0);
	if (cr == CR_REMOVE_VETOED) vetoName.assign(name);
	return cr;
}

// --- main routine ----------------------------------------------------------

DWORD QuiesceAndRemoveOpenZFS(_Out_opt_ bool *needsReboot /*=nullptr*/)
{
	if (needsReboot) *needsReboot = false;

	// Enumerate all present devices in all classes.
	HDEVINFO hdi = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
	if (hdi == INVALID_HANDLE_VALUE) return GetLastError();

	DWORD i = 0;
	SP_DEVINFO_DATA dev{ sizeof(dev) };
	bool anyFound = false;
	DWORD finalWin32 = ERROR_SUCCESS;

	while (SetupDiEnumDeviceInfo(hdi, i++, &dev))
	{
		std::wstring service;
		if (!GetStringPropW(hdi, &dev, SPDRP_SERVICE, service))
			continue;

		if (_wcsicmp(service.c_str(), L"OpenZFS") != 0)
			continue; // not ours

		anyFound = true;

		// Get instance ID for logging and CfgMgr ops
		WCHAR instId[256]; DWORD need = 0;
		if (!SetupDiGetDeviceInstanceIdW(hdi, &dev, instId, ARRAYSIZE(instId), &need))
			instId[0] = L'\0';

		fwprintf(stderr, L"[OpenZFS] targeting devnode %s\n", instId[0] ? instId : L"(unknown)");

		// 1) Try to disable the devnode (best effort)
		DisableDeviceNode(hdi, &dev);

		// 2) Query-and-remove (capture veto info)
		PNP_VETO_TYPE veto = PNP_VetoTypeUnknown;
		std::wstring vetoName;
		CONFIGRET cr = QueryAndRemoveByInstanceId(instId, &veto, vetoName);

		if (cr == CR_SUCCESS) {
			fwprintf(stderr, L"[OpenZFS] CM_Query_And_Remove_SubTreeW: removed %s\n", instId);
			continue;
		}

		if (cr == CR_REMOVE_VETOED) {
			fwprintf(stderr, L"[OpenZFS] removal vetoed (%d): %ls\n", (int)veto, vetoName.c_str());
			// 3) Give it a short grace period to clear (handles, etc.)
			//    Poll for up to ~3s (30 x 100ms). If still vetoed, mark reboot.
			for (int t = 0; t < 30; ++t) {
				Sleep(100);
				veto = PNP_VetoTypeUnknown; vetoName.clear();
				cr = QueryAndRemoveByInstanceId(instId, &veto, vetoName);
				if (cr == CR_SUCCESS) {
					fwprintf(stderr, L"[OpenZFS] removal succeeded after wait: %ls\n", instId);
					break;
				}
				if (cr != CR_REMOVE_VETOED) break;
			}
			if (cr == CR_REMOVE_VETOED) {
				if (needsReboot) *needsReboot = true;
				reboot_indicated = true;
				finalWin32 = ERROR_SHUTDOWN_IN_PROGRESS; // “needs reboot” surrogate
				fwprintf(stderr, L"[OpenZFS] still vetoed; will require reboot. Veto by: %ls\n",
					vetoName.empty() ? L"(unknown)" : vetoName.c_str());
			}
			continue;
		}

		// Other CfgMgr error: log and keep the last one
		fwprintf(stderr, L"[OpenZFS] CM_Query_And_Remove_SubTreeW failed: 0x%lx\n", (unsigned long)cr);
		finalWin32 = ERROR_GEN_FAILURE; // CR_TO_WIN32(cr); // helper macro in cfgmgr32.h; if unavailable, map to ERROR_GEN_FAILURE
	}

	if (hdi != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(hdi);

	// If we didn’t find any OpenZFS devnodes, that’s not an error.
	if (!anyFound) return ERROR_SUCCESS;

	return finalWin32;
}

DWORD
zfs_install(char *inf_path)
{

	DWORD error = 0;
	bool IsServiceRunning = false;
	// 128+4	If a reboot of the computer is necessary,
	// ask the user for permission before rebooting.

	if (_access(inf_path, F_OK) != 0) {
		char cwd[1024];
		_getcwd(cwd, sizeof (cwd));
		fprintf(stderr, "Unable to locate '%s' we are at '%s'\r\n",
			inf_path, cwd);
		return (-1);
	}

#ifdef _DEBUG
	fprintf(stderr, "Checking if OpenZFS service is already running...\n");
	system("sc query OpenZFS");
	fprintf(stderr, "\n\n");
#endif

	error = executeInfSection("OpenZFS_Install 128 ", inf_path);

	if (error)
		fprintf(stderr, "Installation failed, skip "
			"starting the service\r\n");

	if(!error)
		error = installRootDevice(inf_path, IsServiceRunning, ZFS_ROOTDEV);

	if (!error)
		perf_counters_install(inf_path);

#ifdef _DEBUG
	fprintf(stderr, "Checking status on OpenZFS service ...\n");
	system("sc query OpenZFS");
#endif

	return (error);
}

DWORD
zvol_install(char *inf_path)
{
	DWORD error = 0;
	bool IsServiceRunning = false;
	// 128+4	If a reboot of the computer is necessary,
	// ask the user for permission before rebooting.

	if (_access(inf_path, F_OK) != 0) {
		char cwd[1024];
		_getcwd(cwd, sizeof (cwd));
		fprintf(stderr, "Unable to locate '%s' we are at '%s'\r\n",
			inf_path, cwd);
		return (-1);
	}

#ifdef _DEBUG
	fprintf(stderr, "Checking if OpenZVOL.Service is already running..\n");
	system("sc query OpenZVOL");
	fprintf(stderr, "\n\n");
#endif

	if (!error)
		error = installZVOLDevice(inf_path, IsServiceRunning, ZVOL_ROOTDEV);

#ifdef _DEBUG
	fprintf(stderr, "Checking status on OpenZVOL service ... \n");
	system("sc query OpenZVOL");
#endif

	return (error);
}

DWORD
zfs_uninstall(char *inf_path)
{
	DWORD ret = 0;

	ret = send_zfs_ioc_unregister_fs();

	Sleep(2000);

#ifdef _DEBUG
	fprintf(stderr, "Checking if OpenZFS service is already running...\n");
	system("sc query OpenZFS");
	fprintf(stderr, "\n\n");
#endif
	bool needReboot = false;
	DWORD qret = QuiesceAndRemoveOpenZFS(&needReboot);
	printf("QuiesceAndRemoveOpenZFS: needReboot=%d\n", needReboot);

	/*
	 * Run INF cleanup and root-device removal regardless of whether the
	 * device was fully quiesced — these steps clean up registry/service
	 * entries and are safe to run even if the driver is still loaded.
	 */
	executeInfSection("DefaultUninstall 128 ", inf_path);
	uninstallRootDevice(inf_path, ZFS_ROOTDEV);
	perf_counters_uninstall(inf_path);

	/*
	 * Propagate quiesce failure: if the driver wasn't actually unloaded
	 * (e.g. needs reboot, or removal was vetoed), return non-zero so the
	 * caller knows not to proceed with zvol_uninstall while the
	 * zvol_os_wait_openzvol thread may still be running.
	 */
	if (ret == 0 && qret != ERROR_SUCCESS)
		ret = qret;

#ifdef _DEBUG
	fprintf(stderr, "Checking status on OpenZFS service ...\n");
	system("sc query OpenZFS");
#endif

	return (ret);
}



DWORD
zvol_uninstall(char *inf_path)
{
	DWORD ret = 0;

#ifdef _DEBUG
	fprintf(stderr, "Checking if OpenZVOL service is already running...\n");
	system("sc query OpenZVOL");
	fprintf(stderr, "\n\n");
#endif

	// 128+2	Always ask the users if they want to reboot.
	if (ret == 0)
		ret = executeInfSection("DefaultUninstall 128 ", inf_path);

	if (ret == 0) {
		ret = uninstallRootDevice(inf_path, ZVOL_ROOTDEV);
	}

#ifdef _DEBUG
	fprintf(stderr, "Checking status on OpenZVOL service ...\n");
	system("sc query OpenZVOL");
#endif

	return (ret);
}

DWORD
executeInfSection(const char *cmd, char *inf_path)
{

	DWORD error = 0;

	size_t len = strlen(cmd) + strlen(inf_path) + 1;
	size_t sz = 0;
	char buf[MAX_PATH];
	wchar_t wc_buf[MAX_PATH];

	sprintf_s(buf, "%s%s", cmd, inf_path);
	fprintf(stderr, "%s\n", buf);

	mbstowcs_s(&sz, wc_buf, _countof(wc_buf), buf, MAX_PATH);

	SetupSetNonInteractiveMode(TRUE);
	InstallHinfSection(NULL, NULL, wc_buf, 0);
	SetupSetNonInteractiveMode(FALSE);

	return (error);
}

DWORD
startService(char *serviceName)
{
	DWORD error = 0;
	SC_HANDLE servMgrHdl;
	SC_HANDLE zfsServHdl;

	servMgrHdl = OpenSCManager(NULL, NULL, GENERIC_READ | GENERIC_EXECUTE);

	if (!servMgrHdl) {
		fprintf(stderr, "OpenSCManager failed, error %lu\n",
			GetLastError());
		error = GetLastError();
		goto End;
	}

	zfsServHdl = OpenServiceA(servMgrHdl, serviceName,
		GENERIC_READ | GENERIC_EXECUTE);

	if (!zfsServHdl) {
		fprintf(stderr, "OpenServiceA failed, error %lu\n",
			GetLastError());
		error = GetLastError();
		goto CloseMgr;
	}

	if (!StartServiceA(zfsServHdl, NULL, NULL)) {
		if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
			fprintf(stderr, "Service is already running\n");
			error = GetLastError();
		} else {
			fprintf(stderr, "StartServiceA failed, error %lu\n",
				GetLastError());
			// error = GetLastError();
			goto CloseServ;
		}
	}

CloseServ:
	CloseServiceHandle(zfsServHdl);
CloseMgr:
	CloseServiceHandle(servMgrHdl);
End:
	return (error);
}

#define	ZFSIOCTL_TYPE 40000

DWORD
send_zfs_ioc_unregister_fs(void)
{
	HANDLE g_fd = CreateFile(L"\\\\.\\ZFS", GENERIC_READ | GENERIC_WRITE,
		0, NULL, OPEN_EXISTING, 0, NULL);

	DWORD bytesReturned;

	if (g_fd == INVALID_HANDLE_VALUE) {
		printf("Unable to open ZFS devnode, already uninstalled?\n");
		return (0);
	}

	// We use bytesReturned to hold "zfs_module_busy".
	BOOL ret = DeviceIoControl(
		g_fd,
		CTL_CODE(ZFSIOCTL_TYPE, ZFSIOCTL_BASE + ZFS_IOC_UNREGISTER_FS,
			METHOD_NEITHER, FILE_ANY_ACCESS),
		NULL,
		0,
		NULL,
		0,
		&bytesReturned,
		NULL);

	CloseHandle(g_fd);

	if (!ret)
		return (1);

	if (bytesReturned != 0) {
		fprintf(stderr, "ZFS: Unable to uninstall until all pools are "
			"exported: %lu pool(s)\r\n", bytesReturned);
		return (2);
	}

	return (0);
}


#ifndef SPDRP_INF_PATH
#define SPDRP_INF_PATH 0x00000029  // Internal but commonly used
#endif

//
// Sometimes we have multiple installs of OpenZFS.inf
// so after uninstall, let's go through and remove any
// extra installs.
//
void
clean_extra_installs(void)
{
	char infZFS[MAX_PATH];
	char infZVOL[MAX_PATH];
	char infFile[512];

	printf("[2/4] Scanning DriverRepository\n");

	snprintf(infZFS, sizeof (infZFS), "ROOT\\OpenZFS");
	snprintf(infZVOL, sizeof(infZVOL), "ROOT\\OpenZVOL");

	HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(
		nullptr,
		nullptr,
		nullptr,
		DIGCF_ALLCLASSES /* | DIGCF_PRESENT */ // ghost entries are not present.
	);

	if (deviceInfoSet == INVALID_HANDLE_VALUE)
		return;

	SP_DEVINFO_DATA deviceInfoData = {};
	deviceInfoData.cbSize = sizeof (SP_DEVINFO_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); ++i) {
		char hwid[MAX_PATH] = {};

		if (SetupDiGetDeviceRegistryPropertyA(
			deviceInfoSet, &deviceInfoData, SPDRP_HARDWAREID,
			nullptr, (PBYTE)hwid, sizeof (hwid), nullptr)) {

			if (strcasecmp(infZFS, hwid) == 0 ||
				strcasecmp(infZVOL, hwid) == 0) {
				// 🎯 Found a match — uninstall this driver

				printf("[2/4] Attempting to remove %s\n", hwid);
				*infFile = 0;

				// Lookup INF file, if any
				TCHAR regSubKey[MAX_PATH];
				if (SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData,
					SPDRP_DRIVER, NULL, (PBYTE)regSubKey, sizeof(regSubKey), NULL)) {

					// Open HKLM\SYSTEM\CurrentControlSet\Control\Class\<regSubKey>
					HKEY hKey;
					char fullPath[MAX_PATH] = "";
					snprintf(fullPath, sizeof(fullPath),
						"SYSTEM\\CurrentControlSet\\Control\\Class\\%ls", regSubKey);

					if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, fullPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
						DWORD infSize = sizeof(infFile);
						DWORD type = 0;
						if (RegQueryValueExA(hKey, "InfPath", NULL, &type, (LPBYTE)infFile, &infSize) == ERROR_SUCCESS) {
							printf("[2/4] Found INF path: %s\n", infFile);  // e.g., oem12.inf
						}
						RegCloseKey(hKey);
					}
				}

				// Remove driver
				SP_REMOVEDEVICE_PARAMS removeParams = {};
				removeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
				removeParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
				removeParams.Scope = DI_REMOVEDEVICE_GLOBAL;
				removeParams.HwProfile = 0;

				if (SetupDiSetClassInstallParamsA(deviceInfoSet, &deviceInfoData,
					&removeParams.ClassInstallHeader, sizeof(removeParams))) {

					if (SetupDiCallClassInstaller(DIF_REMOVE, deviceInfoSet, &deviceInfoData)) {
						printf("[2/4] Successfully removed device.\n");
					} else {
						printf("[2/4] Failed to call class installer: %lu\n", GetLastError());
					}
				} else {
					printf("[2/4] Failed to set class install params: %lu\n", GetLastError());
				}

				// Remove INF file
				if (*infFile) {
					SetupUninstallOEMInfA(infFile, SUOI_FORCEDELETE, NULL);
					printf("[2/4] Successfully removed INF file.\n");
				}
			} // strcasecmp
		} // if HARDWAREID
	} // for

	SetupDiDestroyDeviceInfoList(deviceInfoSet);
	printf("[2/4] Done.\n");
}


bool
hasOpenZFSInstallConfig(HKEY hKey)
{
	DWORD type = 0;
	DWORD dataSize = 0;

	// First, query the size of the value
	if (RegQueryValueExA(hKey, "Configurations", NULL, &type, NULL, &dataSize) != ERROR_SUCCESS)
		return false;

	if (type != REG_MULTI_SZ || dataSize == 0)
		return false;

	std::vector<char> buffer(dataSize);
	if (RegQueryValueExA(hKey, "Configurations", NULL, NULL, (LPBYTE)buffer.data(), &dataSize) != ERROR_SUCCESS)
		return false;

	const char *ptr = buffer.data();
	while (*ptr)
	{
		if (_stricmp(ptr, "OpenZFS_Install") == 0 || _stricmp(ptr, "OpenZVOL_Install") == 0 ||
			_stricmp(ptr, "OpenZVOL.Install") == 0)
			return true;
		ptr += strlen(ptr) + 1;
	}

	return false;
}

void
CleanupOpenZFSDriverPackages(void)
{
	HKEY infFilesKey;

	printf("[3/4] Scanning Registry for ghost OpenZFS installs...\n");

	LONG res = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
		"SYSTEM\\DriverDatabase\\DriverInfFiles", 0, KEY_READ, &infFilesKey);

	if (res != ERROR_SUCCESS)
		return;

	DWORD index = 0;
	char valueName[256];
	DWORD valueNameSize;

	while (true) {
		valueNameSize = sizeof(valueName);
		res = RegEnumKeyExA(infFilesKey, index++, valueName, &valueNameSize, NULL, NULL, NULL, NULL);
		if (res == ERROR_NO_MORE_ITEMS) break;
		if (res != ERROR_SUCCESS) continue;

		HKEY subKey;
		char fullPath[512];
		snprintf(fullPath, sizeof(fullPath), "SYSTEM\\DriverDatabase\\DriverInfFiles\\%s", valueName);

		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, fullPath, 0, KEY_READ, &subKey) == ERROR_SUCCESS) {
			char infName[256] = {};
			DWORD dataSize = sizeof(infName);
			DWORD type = 0;

			if (hasOpenZFSInstallConfig(subKey)) {
				printf("[3/4] Removing OpenZFS_Install configuration: %s\n", fullPath);
				char *justname;
				if ((justname = strrchr(fullPath, '\\')))
					justname++;
				else
					justname = fullPath;
				SetupUninstallOEMInfA(justname, SUOI_FORCEDELETE, 0);
			}


			if (RegQueryValueExA(subKey, "InfName", NULL, &type, (LPBYTE)infName, &dataSize) == ERROR_SUCCESS) {
				if (type == REG_SZ && (_stricmp(infName, "openzfs.inf") == 0 || _stricmp(infName, "openzvol.inf") == 0)) {
					printf("[3/4] Found INF path: %s\n", valueName);
					if (SetupUninstallOEMInfA(valueName, SUOI_FORCEDELETE, NULL)) {
						printf("[3/4] Successfully removed INF file and driver.\n");
					} else {
						printf("[3/4]  Failed to remove INF: %lu\n", GetLastError());
					}
				}
			}
			RegCloseKey(subKey);
		}
	}
	RegCloseKey(infFilesKey);
	printf("[3/4] Done.\n");
}



// #include <windows.h>
// #include <winsvc.h>
#include <shlobj.h>  // For SHGetFolderPath
// #include <strsafe.h>

bool
install_zed(void)
{
	wchar_t exePath[MAX_PATH];
	if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, 0, exePath))) {
		wprintf(L"Failed to get Program Files path.\n");
		return false;
	}

	// Append subfolder and executable
	if (FAILED(StringCchCatW(exePath, MAX_PATH, L"\\OpenZFS on Windows\\zed.exe"))) {
		wprintf(L"Path too long.\n");
		return false;
	}

	// Open SCM
	SC_HANDLE hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
	if (!hSCManager) {
		wprintf(L"OpenSCManager failed (%lu)\n", GetLastError());
		return false;
	}

	// Create the service
	SC_HANDLE hService = CreateServiceW(
		hSCManager,
		L"OpenZFS_zed",                 // Service name
		L"OpenZFS ZED Event Daemon",   // Display name
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START,
		SERVICE_ERROR_NORMAL,
		exePath,
		NULL, NULL, NULL, NULL, NULL
	);

	if (!hService) {
		DWORD err = GetLastError();
		if (err == ERROR_SERVICE_EXISTS) {
			wprintf(L"ZED service already exists, skipping creation.\n");
		} else {
			wprintf(L"CreateService failed (%lu)\n", err);
			CloseServiceHandle(hSCManager);
			return false;
		}
	} else {
		wprintf(L"ZED service created successfully.\n");
		StartService(hService, 0, NULL);

		CloseServiceHandle(hService);
	}

	CloseServiceHandle(hSCManager);
	return true;
}


bool
uninstall_zed()
{
	SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!hSCManager) {
		printf("Failed to open Service Control Manager (error %lu)\n", GetLastError());
		return false;
	}

	SC_HANDLE hService = OpenServiceA(hSCManager, "OpenZFS_zed", SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
	if (!hService) {
		printf("ZED service not found (may already be uninstalled)\n");
		CloseServiceHandle(hSCManager);
		return true;
	}

	// Try to stop the service if it's running
	SERVICE_STATUS status;
	if (ControlService(hService, SERVICE_CONTROL_STOP, &status)) {
		printf("Stopping ZED service...\n");
		Sleep(1000); // Optional: allow time for it to stop
	}

	if (!DeleteService(hService)) {
		printf("Failed to delete ZED service (error %lu)\n", GetLastError());
		CloseServiceHandle(hService);
		CloseServiceHandle(hSCManager);
		return false;
	}

	printf("ZED service uninstalled successfully.\n");
	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);

	sleep(3);

	// Optionally remove zed.txt log file
	DeleteFileA("C:\\Program Files\\OpenZFS on Windows\\zed.txt");

	return true;
}

static
DWORD RunHidden(const std::wstring &exe, const std::wstring &args)
{
	std::wstring cmd = L"\"" + exe + L"\" " + args;
	STARTUPINFOW si{}; si.cb = sizeof (si);
	PROCESS_INFORMATION pi{};
	DWORD ret = 0;
	DWORD flags = CREATE_UNICODE_ENVIRONMENT;
//	flags |= CREATE_NO_WINDOW;
	if (!CreateProcessW(nullptr, (LPWSTR) cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi))
		return GetLastError();
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &ret);
	CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
	printf("Ran: %ls, exit %lu\n", cmd.c_str(), ret);
	return (ret);
}

DWORD
RunHiddenCapture(const std::wstring &app,
	const std::wstring &args,
	std::wstring &output,
	size_t &line_count,
	DWORD timeout_ms = INFINITE)
{
	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE hRead = nullptr, hWrite = nullptr;
	if (!CreatePipe(&hRead, &hWrite, &sa, 0))
		return GetLastError();
	// child must not inherit the read end
	SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = NULL;
	si.hStdOutput = hWrite;
	si.hStdError = hWrite;

	PROCESS_INFORMATION pi{};
	// Build mutable command line: "app.exe" + space + args
	std::wstring cmd = L"\"" + app + L"\"";
	if (!args.empty()) { cmd += L" "; cmd += args; }
	std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
	cmdBuf.push_back(L'\0');

	BOOL ok = CreateProcessW(
		/*lpApplicationName*/ nullptr,           // use command line
		/*lpCommandLine   */ cmdBuf.data(),
		/*lpProcessAttr   */ nullptr,
		/*lpThreadAttr    */ nullptr,
		/*bInheritHandles */ TRUE,
		/*dwCreationFlags */ CREATE_NO_WINDOW,
		/*lpEnvironment   */ nullptr,
		/*lpCurrentDir    */ nullptr,
		/*lpStartupInfo   */ &si,
		/*lpProcessInfo   */ &pi);

	CloseHandle(hWrite); // parent must close its write end ASAP
	if (!ok) {
		CloseHandle(hRead);
		return GetLastError();
	}

	// Read the entire stream
	std::string raw;
	raw.reserve(1024);
	for (;;) {
		char buf[4096];
		DWORD got = 0;
		BOOL r = ReadFile(hRead, buf, sizeof(buf), &got, nullptr);
		if (!r) {
			if (GetLastError() == ERROR_BROKEN_PIPE) break; // child closed
			else break; // treat other errors as stream end
		}
		if (got) raw.append(buf, buf + got);
	}

	// Wait for process and get exit code
	WaitForSingleObject(pi.hProcess, timeout_ms);
	DWORD ec = 0;
	if (!GetExitCodeProcess(pi.hProcess, &ec))
		ec = DWORD(-1);

	CloseHandle(hRead);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	// Normalize newlines for counting: just count '\n'
	size_t lines = 0;
	for (char c : raw)
		if (c == '\n')
			++lines;
	// If there’s non-empty output without a trailing '\n', count the last line
	if (!raw.empty() && raw.back() != '\n')
		++lines;

	Utf8ToWide(output, raw.c_str());
	line_count = lines;
	return (ec);
}


/*
 * Check if zpool.exe command exists
 * Check if pool(s) are imported
 * Attempt exporting, save zpool.cache if needed
 */
int
zfs_preflight(void)
{
	int ret;

	const std::wstring zpool = L"C:\\Program Files\\OpenZFS On Windows\\zpool.exe";
	std::wstring output;
	size_t lines = 0;
	ret = RunHiddenCapture(zpool, L"list -H -o name", output, lines);
	if (ret) {
		printf("Preflight OK, ZFS not installed/loaded. (result %d)\n", ret);
		return (0);
	}

	if (lines == 0) {
		printf("Preflight OK, no pools imported.\n");
		return (0);
	}

	printf("Preflight: found %zu imported pool(s)\n", lines);

	// Copy the zpool.cache file if it exists
	const std::wstring cacheSrc = L"C:\\Windows\\System32\\drivers\\zpool.cache";
	const std::wstring cacheDst = L"C:\\Windows\\System32\\drivers\\zpool.cache.installer";
	if (GetFileAttributesW(cacheSrc.c_str()) != INVALID_FILE_ATTRIBUTES) {
		if (CopyFileW(cacheSrc.c_str(), cacheDst.c_str(), FALSE)) {
			printf("Saved zpool.cache to %ls\n", cacheDst.c_str());
		} else {
			printf("Failed to save zpool.cache: %lu\n", GetLastError());
		}
	} else {
		printf("No zpool.cache file found, skipping save.\n");
	}


	printf("Attempting to export all pools...\n");
	fflush(stdout);

	ret = RunHiddenCapture(zpool, L"export -a", output, lines);

	if (ret) {
		printf("Preflight FAILED: Failed to export pools, please export manually and retry. (result %d)\n", ret);
		return (1);
	}

	// Restore back the zpool.cache copy
	if (GetFileAttributesW(cacheDst.c_str()) != INVALID_FILE_ATTRIBUTES) {
		if (CopyFileW(cacheDst.c_str(), cacheSrc.c_str(), FALSE)) {
			printf("Restored zpool.cache from %ls\n", cacheDst.c_str());
			DeleteFileW(cacheDst.c_str());
		} else {
			printf("Failed to restore zpool.cache: %lu\n", GetLastError());
		}
	}

	printf("Preflight OK: Pool(s) exported.\n");
	return (0);
}

int
zfs_postflight(void)
{
	// If zpool.cache exists, attempt to (re-)import pools
	const std::wstring cacheFile = L"C:\\Windows\\System32\\drivers\\zpool.cache";
	if (GetFileAttributesW(cacheFile.c_str()) == INVALID_FILE_ATTRIBUTES) {

		printf("No zpool.cache file found, skipping import.\n");
		return (0);
	}
	printf("Postflight: zpool.cache found, attempting to import pools...\n");

	const std::wstring zpool = L"C:\\Program Files\\OpenZFS On Windows\\zpool.exe";
	std::wstring output;
	size_t lines = 0;
	int ret = RunHiddenCapture(zpool, L"import -a -c C:\\Windows\\System32\\drivers\\zpool.cache", output, lines);
	printf("Postflight: done.\n");
	return (0);
}
