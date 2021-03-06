#include <Windows.h>
#include <stdio.h>
#include <dxgi.h>
#include <Shlwapi.h>
#include <string>

#include "MinHook.h"
#include "Extras\OVR_CAPI_Util.h"
#include "OVR_Version.h"

typedef FARPROC(WINAPI* _GetProcAddress)(HMODULE hModule, LPCSTR lpProcName);
typedef HMODULE(WINAPI* _LoadLibrary)(LPCWSTR lpFileName);
typedef HANDLE(WINAPI* _OpenEvent)(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName);
typedef HRESULT(WINAPI* _CreateDXGIFactory)(REFIID riid, void **ppFactory);

_GetProcAddress TrueGetProcAddress;
_LoadLibrary TrueLoadLibrary;
_OpenEvent TrueOpenEvent;
_CreateDXGIFactory DXGIFactory;

HMODULE revModule;
WCHAR revModuleName[MAX_PATH];
WCHAR ovrModuleName[MAX_PATH];

OVR_PUBLIC_FUNCTION(ovrResult)
ovr_Unsupported()
{
	return ovrError_Unsupported;
}

FARPROC WINAPI HookGetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
	FARPROC proc = TrueGetProcAddress(hModule, lpProcName);

	if (hModule == revModule && !proc)
	{
		OutputDebugStringA("Unsupported: ");
		OutputDebugStringA(lpProcName);
		OutputDebugStringA("\n");
		return (FARPROC)ovr_Unsupported;
	}

	return proc;
}

HRESULT WINAPI HookDXGIFactory(REFIID riid, void **ppFactory)
{
	// We need shared texture support for OpenVR, so force DXGI 1.0 games to use DXGI 1.1
	IDXGIFactory1* pDXGIFactory;
	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&pDXGIFactory);
	if (FAILED(hr))
		return hr;
	return pDXGIFactory->QueryInterface(riid, ppFactory);
}

HANDLE WINAPI HookOpenEvent(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCWSTR lpName)
{
	// Don't touch this, it heavily affects performance in Unity games.
	if (wcscmp(lpName, OVR_HMD_CONNECTED_EVENT_NAME) == 0)
		return ::CreateEventW(NULL, TRUE, TRUE, NULL);

	return TrueOpenEvent(dwDesiredAccess, bInheritHandle, lpName);
}

HMODULE WINAPI HookLoadLibrary(LPCWSTR lpFileName)
{
	LPCWSTR name = PathFindFileNameW(lpFileName);
	LPCWSTR ext = PathFindExtensionW(name);
	size_t length = ext - name;

	// Load our own library again so the ref count is incremented.
	if (wcsncmp(name, ovrModuleName, length) == 0)
		return TrueLoadLibrary(revModuleName);

	// We've already injected OpenVR, block attempts to override it.
	if (wcsncmp(name, L"openvr_api.dll", length) == 0)
		return NULL;

	return TrueLoadLibrary(lpFileName);
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
#if defined(_WIN64)
	const char* pBitDepth = "64";
#else
	const char* pBitDepth = "32";
#endif
	switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			revModule = (HMODULE)hModule;
			GetModuleFileName(revModule, revModuleName, MAX_PATH);
			swprintf(ovrModuleName, MAX_PATH, L"LibOVRRT%hs_%d.dll", pBitDepth, OVR_MAJOR_VERSION);
			MH_Initialize();
#if 0
			MH_CreateHook(GetProcAddress, HookGetProcAddress, (PVOID*)&TrueGetProcAddress);
#endif
			MH_CreateHook(LoadLibraryW, HookLoadLibrary, (PVOID*)&TrueLoadLibrary);
			MH_CreateHook(OpenEventW, HookOpenEvent, (PVOID*)&TrueOpenEvent);
			MH_CreateHookApi(L"dxgi.dll", "CreateDXGIFactory", HookDXGIFactory, (PVOID*)&DXGIFactory);
			MH_EnableHook(MH_ALL_HOOKS);
			break;
		case DLL_PROCESS_DETACH:
			MH_Uninitialize();
			break;
		default:
			break;
	}
	return TRUE;
}
