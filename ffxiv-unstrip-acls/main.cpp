#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include <AccCtrl.h>

#include <string>
#include <thread>

#include "minhook/MinHook.h"

using namespace std::chrono_literals;

// exports garbage
// garbage copied from windows headers so we don't get export conflicts

#define MAXPNAMELEN      32
typedef struct timecaps_tag {
    UINT    wPeriodMin;     /* minimum period supported  */
    UINT    wPeriodMax;     /* maximum period supported  */
} TIMECAPS, * PTIMECAPS, NEAR* NPTIMECAPS, FAR* LPTIMECAPS;
typedef _Return_type_success_(return == 0) UINT        MMRESULT;   /* error return code, 0 means no error */
typedef UINT        MMVERSION;  /* major (high byte), minor (low byte) */
typedef struct tagWAVEINCAPSW {
    WORD    wMid;                    /* manufacturer ID */
    WORD    wPid;                    /* product ID */
    MMVERSION vDriverVersion;        /* version of the driver */
    WCHAR   szPname[MAXPNAMELEN];    /* product name (NULL terminated string) */
    DWORD   dwFormats;               /* formats supported */
    WORD    wChannels;               /* number of channels supported */
    WORD    wReserved1;              /* structure packing */
} WAVEINCAPSW, * PWAVEINCAPSW, * NPWAVEINCAPSW, * LPWAVEINCAPSW;
typedef struct tagWAVEOUTCAPSW {
    WORD    wMid;                  /* manufacturer ID */
    WORD    wPid;                  /* product ID */
    MMVERSION vDriverVersion;      /* version of the driver */
    WCHAR   szPname[MAXPNAMELEN];  /* product name (NULL terminated string) */
    DWORD   dwFormats;             /* formats supported */
    WORD    wChannels;             /* number of sources supported */
    WORD    wReserved1;            /* packing */
    DWORD   dwSupport;             /* functionality supported by driver */
} WAVEOUTCAPSW, * PWAVEOUTCAPSW, * NPWAVEOUTCAPSW, * LPWAVEOUTCAPSW;


static DWORD(WINAPI* OrigTimeGetTime)();
extern "C" __declspec(dllexport) DWORD timeGetTime()
{
    return OrigTimeGetTime();
}

static MMRESULT (WINAPI* OrigTimeEndPeriod)(UINT uPeriod);
extern "C" __declspec(dllexport) MMRESULT timeEndPeriod(
    UINT uPeriod
)
{
    return OrigTimeEndPeriod(uPeriod);
}

static MMRESULT(WINAPI* OrigTimeBeginPeriod)(UINT uPeriod);
extern "C" __declspec(dllexport) MMRESULT timeBeginPeriod(
    UINT uPeriod
)
{
    return OrigTimeBeginPeriod(uPeriod);
}

static MMRESULT (WINAPI* OrigTimeGetDevCaps)(
    LPTIMECAPS ptc,
    UINT       cbtc
);
extern "C" __declspec(dllexport) MMRESULT timeGetDevCaps(
    LPTIMECAPS ptc,
    UINT       cbtc
)
{
    return OrigTimeGetDevCaps(ptc, cbtc);
}

static MMRESULT (WINAPI* OrigWaveInGetDevCapsW)(
    UINT_PTR uDeviceID,
    LPWAVEINCAPSW pwic,
    UINT cbwic
);
extern "C" __declspec(dllexport) MMRESULT waveInGetDevCapsW(
    UINT         uDeviceID,
    LPWAVEINCAPSW pwic,
    UINT         cbwic
)
{
    return OrigWaveInGetDevCapsW(uDeviceID, pwic, cbwic);
}

static UINT (WINAPI* origWaveInGetNumDevs)();
extern "C" __declspec(dllexport) UINT waveInGetNumDevs()
{
    return origWaveInGetNumDevs();
}

static UINT(WINAPI* origWaveOutGetNumDevs)();
extern "C" __declspec(dllexport) UINT waveOutGetNumDevs()
{
    return origWaveOutGetNumDevs();
}

static MMRESULT (WINAPI* origWaveOutGetDevCapsW)(
    UINT          uDeviceID,
    LPWAVEOUTCAPSW pwoc,
    UINT          cbwoc
);
extern "C" __declspec(dllexport) MMRESULT waveOutGetDevCapsW(
    UINT          uDeviceID,
    LPWAVEOUTCAPSW pwoc,
    UINT          cbwoc
)
{
    return origWaveOutGetDevCapsW(uDeviceID, pwoc, cbwoc);
}

//////// end exports garbage


typedef void (WINAPI* _OrigBuildExplicitAccessWithNameA)(PEXPLICIT_ACCESS_A, LPSTR, DWORD, ACCESS_MODE, DWORD);
_OrigBuildExplicitAccessWithNameA oBuildExplicitAccessWithNameA = nullptr;

DWORD lastRequestedPerms = 0;

void hkBuildExplicitAccessWithNameA(
    PEXPLICIT_ACCESS_A pExplicitAccess,
    LPSTR              pTrusteeName,
    DWORD              AccessPermissions,
    ACCESS_MODE        AccessMode,
    DWORD              Inheritance
)
{
    lastRequestedPerms = AccessPermissions;

    // reset process read/write bits
    auto newPerms = AccessPermissions | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE;

    printf("old perms: %x\n", AccessPermissions);
    printf("new perms: %x\n", newPerms);

    oBuildExplicitAccessWithNameA(pExplicitAccess, pTrusteeName, newPerms, AccessMode, Inheritance);
}

typedef HANDLE (WINAPI* _OrigOpenProcess)(DWORD, BOOL, DWORD);
_OrigOpenProcess oOpenProcess = nullptr;

HANDLE hkOpenProcess(
    DWORD dwDesiredAccess,
    BOOL  bInheritHandle,
    DWORD dwProcessId
)
{
    auto processHandle = oOpenProcess(PROCESS_QUERY_INFORMATION, false, dwProcessId);
    DWORD value = MAX_PATH;
    char buffer[MAX_PATH];
    if (processHandle)
    {
        QueryFullProcessImageName(processHandle, 0, buffer, &value);
    }

    printf("openprocess: perms: %ul process: %s (%ul)\n", dwDesiredAccess, buffer, dwProcessId);

    // only run this code for ffxiv_dx11.exe and ffxiv.exe because they use PROCESS_ALL_ACCESS on their own executables
    char drive[MAX_PATH];
    char dir[MAX_PATH];
    char fname[MAX_PATH];
    char ext[MAX_PATH];

    _splitpath_s(buffer, drive, dir, fname, ext);

    if (strcmp(fname, "ffxiv_dx11") || strcmp(fname, "ffxiv"))
    {
        printf("ignoring non-game process: %s\n", fname);
        return oOpenProcess(dwDesiredAccess, bInheritHandle, dwProcessId);
    }

    // we only care about the process flags
    auto masked = dwDesiredAccess & 0x2FFF;
    auto processMask = masked & ~lastRequestedPerms;

    printf("perms: %ul\n", processMask);

    if (processMask > 0)
    {
        printf("denied openprocess call\n");
        SetLastError(ERROR_ACCESS_DENIED);
        return NULL;
    }

    return oOpenProcess(dwDesiredAccess, bInheritHandle, dwProcessId);
}

typedef FARPROC(WINAPI* _OrigGetProcAddress)(HMODULE, LPCSTR);
_OrigGetProcAddress oGetProcAddress = nullptr;

HANDLE hkGetProcAddress(
    _In_ HMODULE hModule,
    _In_ LPCSTR lpProcName
)
{
    printf("module: %p, fn: %s\n", hModule, lpProcName);

    return oGetProcAddress(hModule, lpProcName);
}

bool HookFunc(const char* fnName, void* addr, void* hkFunc, void* orig)
{
    MH_STATUS status = MH_CreateHook(
        addr,
        hkFunc,
        reinterpret_cast<LPVOID*>(orig)
    );

    if (status != MH_OK)
    {
        printf("failed to create %s hook! err: %d\n", fnName, status);
        return false;
    }

    status = MH_EnableHook(addr);
    if (status != MH_OK)
    {
        printf("failed to enable %s hook! err: %d\n", fnName, status);
        return false;
    }

    return true;
}

bool HookFunc(const char* libName, const char* fnName, void* hkFunc, void* orig)
{
    auto lib = LoadLibrary(libName);
    if (!lib)
    {
        return false;
    }

    auto addr = GetProcAddress(lib, fnName);

    printf("fn: %p\n", addr);

    return HookFunc(fnName, addr, hkFunc, orig);
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        {
#ifdef _DEBUG
            AllocConsole();

            FILE* garbage;
            freopen_s(&garbage, "CONIN$", "r", stdin);
            freopen_s(&garbage, "CONOUT$", "w", stderr);
            freopen_s(&garbage, "CONOUT$", "w", stdout);
#endif

            char lib_path[MAX_PATH];
            GetSystemDirectory(lib_path, MAX_PATH);
            strcat_s(lib_path, "\\winmm.dll");

            auto lib = LoadLibrary(lib_path);
            if (!lib)
            {
                return FALSE;
            }

            *(void**)&OrigTimeGetTime = (void*)GetProcAddress(lib, "timeGetTime");
            *(void**)&OrigTimeEndPeriod = (void*)GetProcAddress(lib, "timeEndPeriod");
            *(void**)&OrigTimeBeginPeriod = (void*)GetProcAddress(lib, "timeBeginPeriod");
            *(void**)&OrigTimeGetDevCaps = (void*)GetProcAddress(lib, "timeGetDevCaps");
            *(void**)&OrigWaveInGetDevCapsW = (void*)GetProcAddress(lib, "waveInGetDevCapsW");
            *(void**)&origWaveInGetNumDevs = (void*)GetProcAddress(lib, "waveInGetNumDevs");
            *(void**)&origWaveOutGetNumDevs = (void*)GetProcAddress(lib, "waveOutGetNumDevs");
            *(void**)&origWaveOutGetDevCapsW = (void*)GetProcAddress(lib, "waveOutGetDevCapsW");

            MH_STATUS status;

            status = MH_Initialize();
            if (status != MH_OK)
            {
                printf("MinHook init failed\n");
                return FALSE;
            }

            HookFunc("advapi32.dll", "BuildExplicitAccessWithNameA", &hkBuildExplicitAccessWithNameA, &oBuildExplicitAccessWithNameA);

            HookFunc("OpenProcess", &OpenProcess, &hkOpenProcess, &oOpenProcess);
            HookFunc("GetProcAddress", &GetProcAddress, &hkGetProcAddress, &oGetProcAddress);


            break;
        }
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        {
            break;
        }

        case DLL_PROCESS_DETACH:
        {
            // disallow due to this operating as a proxy dll to original winapi functions
            return FALSE;
        }
    }
    return TRUE;
}

