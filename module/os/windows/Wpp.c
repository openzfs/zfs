#include <ntddk.h>
#include <Trace.h>

void
ZFSWppInit(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath)
{
    WPP_INIT_TRACING(pDriverObject, pRegistryPath);
}

void
ZFSWppCleanup(PDRIVER_OBJECT pDriverObject)
{
    WPP_CLEANUP(pDriverObject);
}