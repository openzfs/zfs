/* See: http://msdn.microsoft.com/library/en-us/shellcc/platform/shell/programmersguide/versions.asp */
#include <windows.h>
#include <shlwapi.h>
#ifdef __cplusplus
extern "C" {
#endif
/* __declspec(dllexport) */ HRESULT DllGetVersion (DLLVERSIONINFO2 *pdvi);
#ifdef __cplusplus
}
#endif

HRESULT DllGetVersion (DLLVERSIONINFO2 *pdvi)
{
	if ( !pdvi || (pdvi->info1.cbSize != sizeof (*pdvi)) )
		return (E_INVALIDARG);
	pdvi->info1.dwMajorVersion = 1;
	pdvi->info1.dwMinorVersion = 2;
	pdvi->info1.dwBuildNumber = 3;
	pdvi->info1.dwPlatformID = DLLVER_PLATFORM_WINDOWS;
	if (pdvi->info1.cbSize == sizeof (DLLVERSIONINFO2))
		pdvi->ullVersion = MAKEDLLVERULL (1, 2, 3, 2027);
	return S_OK;
}
