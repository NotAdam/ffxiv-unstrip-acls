#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include <AccCtrl.h>

#include <string>

#include "minhook/MinHook.h"

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

void hkBuildExplicitAccessWithNameA(
    PEXPLICIT_ACCESS_A pExplicitAccess,
    LPSTR              pTrusteeName,
    DWORD              AccessPermissions,
    ACCESS_MODE        AccessMode,
    DWORD              Inheritance
)
{
    // reset process read/write bits
    auto newPerms = AccessPermissions | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE;

    printf("old perms: %x\n", AccessPermissions);
    printf("new perms: %x\n", newPerms);

    oBuildExplicitAccessWithNameA(pExplicitAccess, pTrusteeName, newPerms, AccessMode, Inheritance);
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
        

            // blah

            MH_STATUS status;

            status = MH_Initialize();
            if (status != MH_OK)
            {
                printf("MinHook init failed\n");
                return FALSE;
            }

            auto advapi = LoadLibrary("advapi32.dll");
            if (advapi)
            {
                auto buildExplicitAccess = GetProcAddress(advapi, "BuildExplicitAccessWithNameA");

                printf("fn: %p\n", buildExplicitAccess);

                status = MH_CreateHook(
                    buildExplicitAccess,
                    &hkBuildExplicitAccessWithNameA,
                    reinterpret_cast<LPVOID*>(&oBuildExplicitAccessWithNameA)
                );

                if (status != MH_OK)
                {
                    printf("failed to create BuildExplicitAccessWithNameA hook! err: %d\n", status);
                    return FALSE;
                }

                status = MH_EnableHook(buildExplicitAccess);
                if (status != MH_OK)
                {
                    printf("failed to enable BuildExplicitAccessWithNameA hook! err: %d\n", status);
                    return FALSE;
                }
            }
            else
            {
                printf("wtf?\n");
                return FALSE;
            }

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

