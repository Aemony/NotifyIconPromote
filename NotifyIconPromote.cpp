// NotifyIconPromote.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define WIN32_LEAN_AND_MEAN
#include <wtypes.h>
#include <stdlib.h>



// Registry Watch
struct SKIF_RegistryWatch {
   SKIF_RegistryWatch (HKEY hRootKey,
             const wchar_t* wszSubKey,
             const wchar_t* wszEventName,
                       BOOL bWatchSubtree  = TRUE,
                      DWORD dwNotifyFilter = REG_NOTIFY_CHANGE_LAST_SET,
                       bool bWOW6432Key    = false,  // Access a 32-bit key from either a 32-bit or 64-bit application.
                       bool bWOW6464Key    = false); // Access a 64-bit key from either a 32-bit or 64-bit application.

  ~SKIF_RegistryWatch    (void);

  LSTATUS registerNotify (void);
  void reset             (void);
  bool isSignaled        (void);

  struct {
    HKEY         root        = { };
    wchar_t*     sub_key;
    BOOL         watch_subtree;
    DWORD        filter_mask;
    BOOL         wow64_32key; // Access a 32-bit key from either a 32-bit or 64-bit application.
    BOOL         wow64_64key; // Access a 64-bit key from either a 32-bit or 64-bit application.
  } _init;

  HKEY    _hKeyBase    = { };
  HANDLE  _hEvent      = NULL; // If the CreateEvent function fails, the return value is NULL.
};

SKIF_RegistryWatch::SKIF_RegistryWatch (HKEY hRootKey, const wchar_t* wszSubKey, const wchar_t* wszEventName, BOOL bWatchSubtree, DWORD dwNotifyFilter, bool bWOW6432Key, bool bWOW6464Key)
{
  _init.root          = hRootKey;
  _init.sub_key       = _wcsdup (wszSubKey);
  _init.watch_subtree = bWatchSubtree;
  _init.filter_mask   = dwNotifyFilter;
  _init.wow64_32key   = bWOW6432Key;
  _init.wow64_64key   = bWOW6464Key;

  _hEvent             =
      CreateEvent ( nullptr, TRUE,
                            FALSE, wszEventName );

  reset ();
}

SKIF_RegistryWatch::~SKIF_RegistryWatch (void)
{
  free (_init.sub_key);
  RegCloseKey (_hKeyBase);
  CloseHandle (_hEvent);
  _hEvent = NULL;
}

LSTATUS
SKIF_RegistryWatch::registerNotify (void)
{
  return RegNotifyChangeKeyValue (_hKeyBase, _init.watch_subtree, _init.filter_mask, _hEvent, TRUE);
}

void
SKIF_RegistryWatch::reset (void)
{
  RegCloseKey (_hKeyBase);

  if ((intptr_t)_hEvent > 0)
    ResetEvent (_hEvent);

  LSTATUS lStat =
    RegOpenKeyEx (_init.root, _init.sub_key, 0, KEY_NOTIFY
                              | ((_init.wow64_32key)  ?  KEY_WOW64_32KEY : 0x0)
                              | ((_init.wow64_64key)  ?  KEY_WOW64_64KEY : 0x0), &_hKeyBase);

  if (lStat == ERROR_SUCCESS)
    lStat = registerNotify ( );

  if (lStat != ERROR_SUCCESS)
  {
    RegCloseKey (_hKeyBase);
    CloseHandle (_hEvent);
    _hEvent = NULL;
  }
}

bool
SKIF_RegistryWatch::isSignaled (void)
{
  if (_hEvent == NULL)
    return false;

  bool signaled =
    WaitForSingleObjectEx (
      _hEvent, INFINITE, FALSE
    ) == WAIT_OBJECT_0;

  if (signaled)
    reset ();

  return signaled;
}


// Returns a pseudo handle interpreted as the current process handle
static HANDLE
SKIF_Util_GetCurrentProcess (void)
{
  // A pseudo handle is a special constant, currently (HANDLE)-1, that is interpreted as the current process handle.
  // For compatibility with future operating systems, it is best to call GetCurrentProcess instead of hard-coding this constant value.
  static HANDLE
         handle = GetCurrentProcess ( );
  return handle;
}

static BOOL
WINAPI
SKIF_Util_SetProcessInformation (HANDLE hProcess, PROCESS_INFORMATION_CLASS ProcessInformationClass, LPVOID ProcessInformation, DWORD ProcessInformationSize)
{
  // SetProcessInformation (Windows 8+)
  using SetProcessInformation_pfn =
    BOOL (WINAPI *)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);

  static SetProcessInformation_pfn
    SKIF_SetProcessInformation =
        (SetProcessInformation_pfn)GetProcAddress (LoadLibraryEx (L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "SetProcessInformation");

  if (SKIF_SetProcessInformation == nullptr)
    return FALSE;

  return SKIF_SetProcessInformation (hProcess, ProcessInformationClass, ProcessInformation, ProcessInformationSize);
}

// Sets the power throttling execution speed (EcoQoS) of a process
//   1 = enable; 0 = disable; -1 = auto-managed
static BOOL
SKIF_Util_SetProcessPowerThrottling (HANDLE processHandle, INT state)
{
  PROCESS_POWER_THROTTLING_STATE throttlingState;
  ZeroMemory(&throttlingState, sizeof(throttlingState));

  throttlingState.Version     =                PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  throttlingState.ControlMask = (state > -1) ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
  throttlingState.StateMask   = (state == 1) ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

  return SKIF_Util_SetProcessInformation (processHandle, ProcessPowerThrottling, &throttlingState, sizeof(throttlingState));
}

static void
PromoteNotificationIcons (void)
{
  // Constants
  static const DWORD dwIsPromoted = 1;

  HKEY  hKey;
  DWORD dwIndex  = 0,
        dwResult = 0,
        dwSize   = 0;
  WCHAR szSubKey[MAX_PATH] = { };
  WCHAR szData  [MAX_PATH] = { };

  if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(Control Panel\NotifyIconSettings\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
  {
    if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, &dwIndex, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      while (dwIndex > 0)
      {
        dwIndex--;

        dwSize   = sizeof(szSubKey) / sizeof(WCHAR);
        dwResult = RegEnumKeyExW (hKey, dwIndex, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS)
        {
          // Only act if the IsPromoted value does not exist, as this ensures the user can still toggle the icons through the Settings app
          if (RegGetValueW (hKey, szSubKey, L"IsPromoted", RRF_RT_REG_DWORD, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND)
          {
            RegSetKeyValueW (hKey, szSubKey, L"IsPromoted", REG_DWORD, &dwIsPromoted, sizeof(DWORD));
          }
        }
      }
    }

    RegCloseKey (hKey);
  }
}

// Main function
int APIENTRY wWinMain (_In_     HINSTANCE hInstance,
                       _In_opt_ HINSTANCE hPrevInstance,
                       _In_     LPWSTR    lpCmdLine,
                       _In_     int       nCmdShow)
{
  UNREFERENCED_PARAMETER (hInstance);
  UNREFERENCED_PARAMETER (hPrevInstance);
  UNREFERENCED_PARAMETER (lpCmdLine);
  UNREFERENCED_PARAMETER (nCmdShow);

  // Enable Efficiency Mode in Windows 11 (requires idle (low) priority + EcoQoS)
  SKIF_Util_SetProcessPowerThrottling (SKIF_Util_GetCurrentProcess ( ), 1);
  SetPriorityClass (SKIF_Util_GetCurrentProcess ( ), IDLE_PRIORITY_CLASS );

  // Set up the registry watch
  SKIF_RegistryWatch regWatch (HKEY_CURRENT_USER, LR"(Control Panel\NotifyIconSettings)", L"NotifyIconSettingsNotify");

  // A one-time initial run to promote any unconfigured notification icon to be visible
  PromoteNotificationIcons ( );

  // Enter the loop where we just watch for registry changes...
  while (true)
  {
    if (regWatch.isSignaled ( ))
      PromoteNotificationIcons ( );
    else
      break; // Abort the execution if the wait event failed to be set up
  }
}