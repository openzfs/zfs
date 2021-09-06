
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
}

#include "zfsinstaller.h"

#include <ctime>
#include <string>
#define	_SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING 1
#include <experimental\filesystem>
namespace fs = std::experimental::filesystem;

#define	MAX_PATH_LEN 1024

//  Usage:
//    zfsinstaller install [inf] [installFolder]
//			defaults to something like %ProgramFiles%\ZFS)
//    zfsinstaller uninstall [inf] (could default to something
//			like %ProgramFiles%\ZFS\ZFSin.inf)
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
	int size_in_mb, const char *etl_file) {
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

std::string get_cwd() {
	CHAR cwd_path[MAX_PATH_LEN] = { 0 };
	DWORD len = GetCurrentDirectoryA(MAX_PATH_LEN, cwd_path);
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

		sprintf_s(command, "logman create trace %s -p {%s} %s %s"
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

int perf_counters(char* inf_path, int type) {
	int error = 0;
	fs::path path = std::string(inf_path);
	std::string final_path;

	char driver_path[MAX_PATH_LEN] = { 0 };
	strncpy_s(driver_path, inf_path, MAX_PATH_LEN);
	char* slash = strrchr(driver_path, '\\');
	*slash = '\0';

	if (path.is_absolute())
		final_path = std::string(driver_path) + MANIFEST_FILE;
	else if (path.is_relative())
		final_path = get_cwd() + std::string("\\") + std::string(
			driver_path) + MANIFEST_FILE;

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

int perf_counters_install(char* inf_path) {
	return (perf_counters(inf_path, MAN_INSTALL));
}


int perf_counters_uninstall(char* inf_path) {
	return (perf_counters(inf_path, MAN_UNINSTALL));
}

int
main(int argc, char *argv[])
{
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

	if (strcmp(argv[1], "install") == 0) {
		if (argc == 3) {
			zfs_install(argv[2]);
			fprintf(stderr, "Installation done.");
		} else {
			fprintf(stderr, "Incorrect argument usage\n");
			printUsage();
			return (ERROR_BAD_ARGUMENTS);
		}
	} else if (strcmp(argv[1], "uninstall") == 0) {
		if (argc == 3) {
			int ret = zfs_uninstall(argv[2]);
			if (0 == ret)
				return (zfs_log_session_delete());
			return (ret);
		} else {
			fprintf(stderr, "Incorrect argument usage\n");
			printUsage();
			return (ERROR_BAD_ARGUMENTS);
		}
	} else if (strcmp(argv[1], "trace") == 0) {
		if (argc == 3 && strcmp(argv[2], "-d") == 0)
			return (zfs_log_session_delete());
		else
			return (zfs_log_session_create(argc - 1, &argv[1]));
	} else {
		fprintf(stderr, "unknown argument %s\n", argv[1]);
		printUsage();
		return (ERROR_BAD_ARGUMENTS);
	}
	return (0);
}

void
printUsage() {
	fprintf(stderr, "\nUsage:\n\n");
	fprintf(stderr, "Install driver per INF DefaultInstall section:\n");
	fprintf(stderr, "zfsinstaller install inf_path\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Uninstall driver per INF DefaultUninstall section:\n");
	fprintf(stderr, "zfsinstaller uninstall inf_path\n");
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

DWORD zfs_install(char *inf_path) {

	DWORD error = 0;
	// 128+4	If a reboot of the computer is necessary,
	// ask the user for permission before rebooting.

	if (_access(inf_path, F_OK) != 0) {
		char cwd[1024];
		_getcwd(cwd, sizeof (cwd));
		fprintf(stderr, "Unable to locate '%s' we are at '%s'\r\n",
			inf_path, cwd);
		return (-1);
	}

	error = executeInfSection("OpenZFS_Install 128 ", inf_path);

	// Start driver service if not already running
	char serviceName[] = "OpenZFS";
	if (!error)
		error = startService(serviceName);
	else
		fprintf(stderr, "Installation failed, skip "
			"starting the service\r\n");

	if (!error)
		error = installRootDevice(inf_path);

	if (!error)
		perf_counters_install(inf_path);

	return (error);
}

DWORD
zfs_uninstall(char *inf_path)
{
	DWORD ret = 0;

	ret = send_zfs_ioc_unregister_fs();

	Sleep(2000);

	// 128+2	Always ask the users if they want to reboot.
	if (ret == 0)
		ret = executeInfSection("DefaultUninstall 128 ", inf_path);

	if (ret == 0) {
		ret = uninstallRootDevice(inf_path);
		perf_counters_uninstall(inf_path);
	}

	return (ret);
}


DWORD
executeInfSection(const char *cmd, char *inf_path) {

#ifdef _DEBUG
	system("sc query ZFSin");
	fprintf(stderr, "\n\n");
#endif

	DWORD error = 0;

	size_t len = strlen(cmd) + strlen(inf_path) + 1;
	size_t sz = 0;
	char buf[MAX_PATH];
	wchar_t wc_buf[MAX_PATH];

	sprintf_s(buf, "%s%s", cmd, inf_path);
	fprintf(stderr, "%s\n", buf);

	mbstowcs_s(&sz, wc_buf, len, buf, MAX_PATH);

	InstallHinfSection(
		NULL,
		NULL,
		wc_buf,
		0
	);


#ifdef _DEBUG
	system("sc query ZFSin");
#endif

	return (error);
	// if we want to have some more control on installation, we need to get
	// a bit deeper into the setupapi, something like the following...

	/*
	 * HINF inf = SetupOpenInfFile(
	 * L"C:\\master_test\\ZFSin\\ZFSin.inf", //PCWSTR FileName,
	 * NULL, //PCWSTR InfClass,
	 * INF_STYLE_WIN4, //DWORD  InfStyle,
	 * 0//PUINT  ErrorLine
	 * );
	 *
	 * if (!inf) {
	 * std::cout << "SetupOpenInfFile failed, err "
	 * << GetLastError() << "\n";
	 * return (-1);
	 * }
	 *
	 *
	 * int ret = SetupInstallFromInfSection(
	 *	NULL, //owner
	 *	inf, //inf handle
	 *	L"DefaultInstall",
	 *	SPINST_ALL, //flags
	 *	NULL, //RelativeKeyRoot
	 *	NULL, //SourceRootPath
	 *	SP_COPY_NEWER_OR_SAME | SP_COPY_IN_USE_NEEDS_REBOOT, //CopyFlags
	 *	NULL, //MsgHandler
	 *	NULL, //Context
	 *	NULL, //DeviceInfoSet
	 *	NULL //DeviceInfoData
	 * );
	 *
	 * if (!ret) {
	 *		std::cout << "SetupInstallFromInfSection failed, err "
	 *		<< GetLastError() << "\n";
	 * return (-1);
	 * }
	 *
	 * SetupCloseInfFile(inf);
	 */
}

DWORD
startService(char *serviceName)
{
	DWORD error = 0;
	SC_HANDLE servMgrHdl;
	SC_HANDLE zfsServHdl;

	servMgrHdl = OpenSCManager(NULL, NULL, GENERIC_READ | GENERIC_EXECUTE);

	if (!servMgrHdl) {
		fprintf(stderr, "OpenSCManager failed, error %d\n",
			GetLastError());
		error = GetLastError();
		goto End;
	}

	zfsServHdl = OpenServiceA(servMgrHdl, serviceName,
		GENERIC_READ | GENERIC_EXECUTE);

	if (!zfsServHdl) {
		fprintf(stderr, "OpenServiceA failed, error %d\n",
			GetLastError());
		error = GetLastError();
		goto CloseMgr;
	}

	if (!StartServiceA(zfsServHdl, NULL, NULL)) {
		if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
			fprintf(stderr, "Service is already running\n");
		} else {
			fprintf(stderr, "StartServiceA failed, error %d\n",
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

#include <strsafe.h>
#include <cfgmgr32.h>
#include <newdev.h>

#define	ZFS_ROOTDEV "Root\\OpenZFS"
// DevCon uses LoadLib() - but lets just static link
#pragma comment(lib, "Newdev.lib")

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
		sizeof (ClassName) / sizeof (ClassName[0]), 0)) {
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
installRootDevice(char *inf)
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

	ZeroMemory(hwIdList, sizeof (hwIdList));
	if (FAILED(StringCchCopyA(hwIdList, LINE_LEN, ZFS_ROOTDEV))) {
		goto final;
	}

	// Now create the element.
	// Use the Class GUID and Name from the INF file.
	DeviceInfoData.cbSize = sizeof (SP_DEVINFO_DATA);
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
		(DWORD) (strlen(hwIdList) + 1 + 1) * sizeof (char))) {
		goto final;
	}

	// Transform the registry element into an actual devnode
	// in the PnP HW tree.
	if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE,
		DeviceInfoSet,
		&DeviceInfoData)) {
		goto final;
	}

	failcode = 0;

	// According to devcon we also have to Update now as well.
	UpdateDriverForPlugAndPlayDevicesA(NULL, ZFS_ROOTDEV,
		inf, flags, &reboot);

	if (reboot) printf("Windows indicated a Reboot is required.\n");

final:

	if (DeviceInfoSet != INVALID_HANDLE_VALUE) {
		SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	}
	printf("%s: exit %d:0x%x\n", __func__, failcode, failcode);

	return (failcode);
}

DWORD
uninstallRootDevice(char *inf)
{
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

	DeviceInfoData.cbSize = sizeof (SP_DEVINFO_DATA);
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
				goto final;
			}
		}

		if (GetLastError() == ERROR_INVALID_DATA)
			continue;

		// Compare each entry in the buffer multi-sz list
		// with our HardwareID.
		for (p = buffer; *p && (p < &buffer[buffersize]);
			p += strlen(p) + sizeof (char)) {
			// printf("%s: comparing '%s' with '%s'\n",
			//	 __func__, "ROOT\\ZFSin", p);
			if (!_stricmp(ZFS_ROOTDEV, p)) {

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

#if 0




	ZeroMemory(hwIdList, sizeof (hwIdList));
	if (FAILED(StringCchCopyA(hwIdList, LINE_LEN, "ROOT\\ZFSin"))) {
			goto final;
	}

	printf("%s: CchCopy\n", __func__);

	// Now create the element.
	// Use the Class GUID and Name from the INF file.
	DeviceInfoData.cbSize = sizeof (SP_DEVINFO_DATA);
	if (!SetupDiCreateDeviceInfoA(DeviceInfoSet,
		ClassName,
		&ClassGUID,
		NULL,
		0,
		DICD_GENERATE_ID,
		&DeviceInfoData)) {
		goto final;
	}

	printf("%s: SetupDiCreateDeviceInfoA\n", __func__);

	rmdParams.ClassInstallHeader.cbSize = sizeof (SP_CLASSINSTALL_HEADER);
	rmdParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
	rmdParams.Scope = DI_REMOVEDEVICE_GLOBAL;
	rmdParams.HwProfile = 0;
	if (!SetupDiSetClassInstallParamsA(DeviceInfoSet, &DeviceInfoData,
		&rmdParams.ClassInstallHeader, sizeof (rmdParams)) ||
		!SetupDiCallClassInstaller(DIF_REMOVE, DeviceInfoSet,
			&DeviceInfoData)) {

		// failed to invoke DIF_REMOVE
		failcode = 14;
		goto final;
	}

	failcode = 0;

final:
	if (DeviceInfoSet != INVALID_HANDLE_VALUE) {
		SetupDiDestroyDeviceInfoList(DeviceInfoSet);
	}
	printf("%s: exit %d:0x%x\n", __func__, failcode, failcode);
	return (failcode);
}
#endif
