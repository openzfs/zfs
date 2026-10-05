/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
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
  * Copyright 2025 Jorgen Lundman <lundman@lundman.net>.
  */



//
// This code is rediculous.
//

#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#pragma comment(lib, "wbemuuid.lib")

extern "C" HRESULT OfflineDisk(const char *devicePath);
extern "C" HRESULT OnlineDisk(const char *devicePath);

// Call with diskPath = "\\\\.\\PhysicalDrive2"
// offline == TRUE >> Offline(), FALSE >> Online()
HRESULT
process_disk(const char *diskPath, BOOL offline)
{
	HRESULT hr;
	IWbemLocator *pLoc = NULL;
	IWbemServices *pSvc = NULL;
	IEnumWbemClassObject *pEnum = NULL;
	IWbemClassObject *pObj = NULL;

	// 1) Initialize COM
	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) return hr;

	// 2) Set general COM security levels
	hr = CoInitializeSecurity(
		NULL,                       // security descriptor
		-1,                         // COM negotiates 
		NULL,                       // auth services
		NULL,                       // reserved
		RPC_C_AUTHN_LEVEL_DEFAULT,  // default authentication 
		RPC_C_IMP_LEVEL_IMPERSONATE,// default Impersonation  
		NULL,                       // authentication list
		EOAC_NONE,                  // capabilities 
		NULL                        // reserved
	);
	if (FAILED(hr)) goto cleanup;

	// 3) Obtain the initial locator to WMI
	hr = CoCreateInstance(
		CLSID_WbemLocator,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWbemLocator, (LPVOID *)&pLoc);
	if (FAILED(hr))
		goto cleanup;

	// 4) Connect to the ROOT\CIMV2 namespace
	hr = pLoc->ConnectServer(
		_bstr_t(L"ROOT\\CIMV2"), // WMI namespace
		NULL,                    // user 
		NULL,                    // password 
		0,                       // locale 
		NULL,                    // security flags
		0,                       // authority 
		0,                       // context 
		&pSvc                    // IWbemServices pointer
	);
	if (FAILED(hr)) goto cleanup;

	// 5) Set security levels on the proxy
	hr = CoSetProxyBlanket(
		pSvc,                        // proxy
		RPC_C_AUTHN_WINNT,           // auth service
		RPC_C_AUTHZ_NONE,            // authorization service
		NULL,                        // principal
		RPC_C_AUTHN_LEVEL_CALL,      // auth level
		RPC_C_IMP_LEVEL_IMPERSONATE, // impersonation
		NULL,                        // identity
		EOAC_NONE                    // capabilities
	);
	if (FAILED(hr))
		goto cleanup;

	// 6) Query for the specific disk by DeviceID
	{
		wchar_t query[256];
		swprintf(query, sizeof(query) / sizeof(wchar_t),
			L"SELECT * FROM Win32_DiskDrive WHERE DeviceID = '%s'", diskPath);

		hr = pSvc->ExecQuery(
			bstr_t("WQL"),
			bstr_t(query),
			WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
			NULL,
			&pEnum);
		if (FAILED(hr)) goto cleanup;
	}

	// 7) Get the first result (there should only be one)
	ULONG returned = 0;
	hr = pEnum->Next(WBEM_INFINITE, 1, &pObj, &returned);
	if (FAILED(hr) || returned == 0) {
		hr = WBEM_E_NOT_FOUND;
		goto cleanup;
	}

	// 8) Invoke Offline() or Online()
	{
		BSTR method = offline ? SysAllocString(L"Offline") : SysAllocString(L"Online");
		hr = pSvc->ExecMethod(
			NULL,           // object path; if NULL, must set __PATH in pInParams
			method,
			0,
			NULL,
			NULL,
			NULL,
			NULL);
		SysFreeString(method);
	}

cleanup:
	if (pObj)    pObj->Release();
	if (pEnum)   pEnum->Release();
	if (pSvc)    pSvc->Release();
	if (pLoc)    pLoc->Release();
	CoUninitialize();
	return hr;
}



HRESULT
OfflineDisk(const char *devicePath)
{
	return (process_disk(devicePath, TRUE));
}

HRESULT
OnlineDisk(const char *devicePath)
{
	return (process_disk(devicePath, FALSE));
}

