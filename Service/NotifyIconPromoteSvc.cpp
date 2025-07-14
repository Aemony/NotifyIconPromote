// NotifyIconPromoteSvc.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

//#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>

#include <wtypes.h>
#include <stdlib.h>
#include <vector>

#include <ntsecapi.h>
#include <timeapi.h >

#define STATUS_SUCCESS 0x00000000

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Winmm.lib")

wchar_t SVCNAME[]  = L"NotifyIconPromote";  //     Base Service Name: NotifyIconPromote
wchar_t SVCNAME_[] = L"NotifyIconPromote_"; // Per-User Service Name: NotifyIconPromote_<LUID>

SERVICE_STATUS        gSvcStatus       = {  };
SERVICE_STATUS_HANDLE gSvcStatusHandle = {  };
HANDLE                ghSvcStopEvent   = NULL;
HANDLE                ghSvcPauseEvent  = NULL;
HANDLE                ghSvcResumeEvent = NULL;
std::vector<HANDLE>   gvhEvents;
HKEY                  ghkHKCUUserState = NULL;
bool                  gbUserState      = true;

VOID WINAPI SvcCtrlHandler   (DWORD dwCtrl);
VOID WINAPI SvcMain          (DWORD dwArgc, LPTSTR* lpszArgv);
int         GetSvcStatus     (VOID);

VOID        ReportSvcStatus  (DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint);
VOID        SvcInit          (VOID);
VOID        Util_ReportEvent (LPCTSTR szFunction, WORD wType, LPCTSTR szMessage = L"", LPCTSTR szDetails = L"");



// Registry Watch
struct RegistryWatch {
  RegistryWatch (         HKEY   hRootKey,
                 const wchar_t*  wszSubKey,
                 const wchar_t*  wszEventName,
                           BOOL  bWatchSubtree   = TRUE,
                          DWORD  dwNotifyFilter  = REG_NOTIFY_CHANGE_LAST_SET,
                           bool  bWOW6432Key     = false,   // Access a 32-bit key from either a 32-bit or 64-bit application.
                           bool  bWOW6464Key     = false ); // Access a 64-bit key from either a 32-bit or 64-bit application.

  ~RegistryWatch (void);

  LSTATUS registerNotify (void) const;
  void    reset          (void);

  struct {
    HKEY         root = { };
    wchar_t*     sub_key;
    BOOL         watch_subtree;
    DWORD        filter_mask;
    BOOL         wow64_32key; // Access a 32-bit key from either a 32-bit or 64-bit application.
    BOOL         wow64_64key; // Access a 64-bit key from either a 32-bit or 64-bit application.
  } _init;

  HKEY    _hKeyBase = { };
  HANDLE  _hEvent = NULL; // If the CreateEvent function fails, the return value is NULL.
};

RegistryWatch::RegistryWatch (HKEY hRootKey, const wchar_t* wszSubKey, const wchar_t* wszEventName, BOOL bWatchSubtree, DWORD dwNotifyFilter, bool bWOW6432Key, bool bWOW6464Key)
{
  _init.root          = hRootKey;
  _init.sub_key       = _wcsdup (wszSubKey);
  _init.watch_subtree = bWatchSubtree;
  _init.filter_mask   = dwNotifyFilter;
  _init.wow64_32key   = bWOW6432Key;
  _init.wow64_64key   = bWOW6464Key;

  _hEvent =
    CreateEvent (NULL, TRUE,
      FALSE, wszEventName);

  reset ( );
}

RegistryWatch::~RegistryWatch (void)
{
  free (_init.sub_key);
  RegCloseKey (_hKeyBase);
  CloseHandle (_hEvent);
  _hEvent = NULL;
}

LSTATUS
RegistryWatch::registerNotify (void) const
{
  return RegNotifyChangeKeyValue (_hKeyBase, _init.watch_subtree, _init.filter_mask, _hEvent, TRUE);
}

void
RegistryWatch::reset (void)
{
  RegCloseKey (_hKeyBase);

  if ((intptr_t)_hEvent > 0)
    ResetEvent (_hEvent);

  LSTATUS lStat =
    RegOpenKeyEx (_init.root, _init.sub_key, 0, KEY_NOTIFY
      | ((_init.wow64_32key) ? KEY_WOW64_32KEY : 0x0)
      | ((_init.wow64_64key) ? KEY_WOW64_64KEY : 0x0), &_hKeyBase);

  if (lStat == ERROR_SUCCESS)
    lStat = registerNotify ( );

  if (lStat != ERROR_SUCCESS)
  {
    RegCloseKey (_hKeyBase);
    CloseHandle (_hEvent);
    _hEvent = NULL;
  }
}





// Returns the current time
DWORD
Util_timeGetTime (void)
{
  static LARGE_INTEGER qpcFreq = { };
         LARGE_INTEGER li      = { };

  if ( qpcFreq.QuadPart == 1)
  {
    return timeGetTime ();
  }

  if (QueryPerformanceCounter (&li))
  {
    if (qpcFreq.QuadPart == 0 && QueryPerformanceFrequency (&qpcFreq) == FALSE)
    {   qpcFreq.QuadPart  = 1;

      return rand ();
    }

    return
      static_cast <DWORD> ( li.QuadPart /
                      (qpcFreq.QuadPart / 1000ULL) );
  }

  return static_cast <DWORD> (-1);
}

// Returns a pseudo handle interpreted as the current process handle
static HANDLE
Util_GetCurrentProcess (void)
{
  // A pseudo handle is a special constant, currently (HANDLE)-1, that is interpreted as the current process handle.
  // For compatibility with future operating systems, it is best to call GetCurrentProcess instead of hard-coding this constant value.
  static HANDLE
    handle = GetCurrentProcess ( );
  return handle;
}

static BOOL
Util_IsProcessOwned (DWORD dwProcessId)
{
  HANDLE hProcessSelf = Util_GetCurrentProcess ( ),
         hProcessProc = OpenProcess (PROCESS_QUERY_INFORMATION, FALSE, dwProcessId);

  BOOL owned = false;

  if (hProcessSelf != NULL &&
      hProcessProc != NULL)
  {
    HANDLE  hTokenSelf = NULL,
            hTokenProc = NULL;

    if (OpenProcessToken (hProcessSelf, TOKEN_QUERY, &hTokenSelf))
    {
      if (OpenProcessToken(hProcessProc, TOKEN_QUERY, &hTokenProc))
      {
        DWORD tokenSizeSelf = 0,
              tokenSizeProc = 0;
        GetTokenInformation (hTokenSelf, TokenUser, NULL, 0, &tokenSizeSelf);

        if (tokenSizeSelf > 0)
        {
          GetTokenInformation (hTokenProc, TokenUser, NULL, 0, &tokenSizeProc);

          if (tokenSizeProc > 0)
          {
            BYTE* dataSelf = new BYTE[tokenSizeSelf];
            BYTE* dataProc = new BYTE[tokenSizeProc];

            GetTokenInformation  (hTokenSelf, TokenUser, dataSelf, tokenSizeSelf, &tokenSizeSelf);
            GetTokenInformation  (hTokenProc, TokenUser, dataProc, tokenSizeProc, &tokenSizeProc);
            TOKEN_USER* pUserSelf = (TOKEN_USER*)dataSelf;
            TOKEN_USER* pUserProc = (TOKEN_USER*)dataProc;

            owned = EqualSid (pUserSelf->User.Sid, pUserProc->User.Sid);

            delete[] dataSelf;
            delete[] dataProc;
          }
        }
        CloseHandle (hTokenProc);
      }
      CloseHandle (hTokenSelf);
    }
    CloseHandle (hProcessProc);
  }

  return owned;
}

static BOOL WINAPI
Util_SetProcessInformation (HANDLE hProcess, PROCESS_INFORMATION_CLASS ProcessInformationClass, LPVOID ProcessInformation, DWORD ProcessInformationSize)
{
  // SetProcessInformation (Windows 8+)
  using SetProcessInformation_pfn =
    BOOL (WINAPI*) (HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);

  static SetProcessInformation_pfn
         SetProcessInformation =
        (SetProcessInformation_pfn) GetProcAddress (LoadLibraryEx(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "SetProcessInformation");

  if (SetProcessInformation == nullptr)
    return FALSE;

  return SetProcessInformation (hProcess, ProcessInformationClass, ProcessInformation, ProcessInformationSize);
}

// Sets the power throttling execution speed (EcoQoS) of a process
//   1 = enable; 0 = disable; -1 = auto-managed
static BOOL
Util_SetProcessPowerThrottling (HANDLE processHandle, INT state)
{
  PROCESS_POWER_THROTTLING_STATE throttlingState;
  ZeroMemory (&throttlingState, sizeof(throttlingState));

  throttlingState.Version     =                PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  throttlingState.ControlMask = (state > -1) ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;
  throttlingState.StateMask   = (state == 1) ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

  return Util_SetProcessInformation (processHandle, ProcessPowerThrottling, &throttlingState, sizeof(throttlingState));
}

//
// Purpose:
//   Logs messages to the event log
//
// Parameters:
//   szFunction - name of function that failed
//
// Return value:
//   None
//
// Remarks:
//   The service must have an entry in the Application event log.
//
VOID
Util_ReportEvent (LPCTSTR szFunction, WORD wType, LPCTSTR szMessage, LPCTSTR szDetails)
{
  HANDLE  hEventSource;
  LPCTSTR lpszStrings[1];
  TCHAR   Buffer[300];

  hEventSource = RegisterEventSource (NULL, SVCNAME);

  if( NULL != hEventSource )
  {
    if (wType == EVENTLOG_ERROR_TYPE)
      StringCchPrintf (Buffer, 256, TEXT("%s failed: %s%s\nLast-Error Code: %d"), szFunction, szMessage, szDetails, GetLastError ( ));
    else
      StringCchPrintf (Buffer, 256, TEXT("%s: %s%s"), szFunction, szMessage, szDetails);

    lpszStrings[0] = Buffer;
  //lpszStrings[1] = SVCNAME;

    DWORD dwEventID = (wType == EVENTLOG_ERROR_TYPE)       ? 1 : // Error
                      (wType == EVENTLOG_INFORMATION_TYPE) ? 0 : // Information
                                                             0 ; // Success


    ReportEvent (hEventSource,        // event log handle
                  wType,              // event type
                  0,                  // event category
                  dwEventID,                  // event identifier
                  NULL,               // no security identifier
                  1,                  // size of lpszStrings array
                  0,                  // no binary data
                  lpszStrings,        // array of strings
                  NULL);              // no binary data

    DeregisterEventSource (hEventSource);
  }
}


static void
PromoteNotificationIcons (void)
{
  // Do nothing if the user setting is disabled.
  if (! gbUserState)
    return;

  // Constants
  static const DWORD dwIsPromoted = 1;

  HKEY  hKey;
  DWORD dwIndex  = 0,
        dwResult = 0,
        dwSize   = 0;
  WCHAR szSubKey[MAX_PATH] = { };
  WCHAR szData[MAX_PATH] = { };

  if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(Control Panel\NotifyIconSettings\)", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
  {
    if (RegQueryInfoKeyW (hKey, NULL, NULL, NULL, &dwIndex, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
      while (dwIndex > 0)
      {
        dwIndex--;

        dwSize = sizeof(szSubKey) / sizeof(WCHAR);
        dwResult = RegEnumKeyExW (hKey, dwIndex, szSubKey, &dwSize, NULL, NULL, NULL, NULL);

        if (dwResult == ERROR_NO_MORE_ITEMS)
          break;

        if (dwResult == ERROR_SUCCESS)
        {
          // Only act if the IsPromoted value does not exist, as this ensures the user can still toggle the icons through the Settings app
          if (RegGetValueW (hKey, szSubKey, L"IsPromoted", RRF_RT_REG_DWORD, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND)
          {
            RegSetKeyValueW  (hKey, szSubKey, L"IsPromoted", REG_DWORD, &dwIsPromoted, sizeof(DWORD));
            Util_ReportEvent (TEXT("PromoteNotificationIcons"), EVENTLOG_SUCCESS, TEXT("Successfully promoted notification icon "), szSubKey);
          }
        }
      }
    }

    RegCloseKey (hKey);
  }
}

static bool
SetUserSetting (bool state)
{
  LSTATUS lStat = RegSetValueEx (ghkHKCUUserState, L"Enabled", 0, REG_BINARY, (PBYTE)&state, sizeof (bool));
  if (ERROR_SUCCESS == lStat)
  {
    if (state)
      Util_ReportEvent (TEXT("SetUserSetting"), EVENTLOG_SUCCESS, L"Enabled automatic icon promotion.");
    else
      Util_ReportEvent (TEXT("SetUserSetting"), EVENTLOG_SUCCESS, L"Disabled automatic icon promotion.");

    gbUserState = state;
  }

  else
  {
    LPWSTR messageBuffer = nullptr;

    size_t size = FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                 FORMAT_MESSAGE_FROM_SYSTEM     |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, lStat, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&messageBuffer, 0, NULL);

    Util_ReportEvent (TEXT("SetUserSetting"), EVENTLOG_ERROR_TYPE, L"Setting the registry state failed:", messageBuffer);

    LocalFree (messageBuffer);
  }

  return state;
}

static bool
GetUserSetting (void)
{
  DWORD  dwSize = sizeof (bool);
  LSTATUS lStat = RegGetValue (ghkHKCUUserState, NULL, L"Enabled", RRF_RT_REG_BINARY, NULL, &gbUserState, &dwSize);

  if (lStat == ERROR_SUCCESS)
    return gbUserState;

  if (lStat == ERROR_FILE_NOT_FOUND)
    return (SetUserSetting (gbUserState));

  return false;
}

static void
InitUserSetting (void)
{
  // I remembered that some users on a shared system might actually want this behavior... Whoops!
  // Let us use a HKCU registry value to control the behavior of the service.
  
  // Create/open the user-specific registry subkey and retrieve/set the registry value
  if (ERROR_SUCCESS == RegCreateKeyEx (HKEY_CURRENT_USER, LR"(SOFTWARE\Notification Icon Promoter)", 0, NULL, NULL, (KEY_NOTIFY | KEY_QUERY_VALUE | KEY_SET_VALUE), NULL, &ghkHKCUUserState, NULL))
    GetUserSetting ( );
}


//
// Purpose:
//   Entry point for the process
//
// Parameters:
//   None
//
// Return value:
//   None, defaults to 0 (zero)
//
int __cdecl _tmain (int argc, TCHAR* argv[])
{
  SERVICE_TABLE_ENTRY DispatchTable[] =
  { { SVCNAME, (LPSERVICE_MAIN_FUNCTION) SvcMain } };

  // This call returns when the service has stopped.
  // The process should simply terminate when the call returns.
  if (StartServiceCtrlDispatcher (DispatchTable))
  {
    // Do nothing since we have already run as a service.
  }

  else if (GetLastError ( ) == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
  {
    // We are actually running as a console application.

    // Create/open the per-user key
    InitUserSetting ( );

    // Do a one-time adjustment
    PromoteNotificationIcons ( );

    // 4 - Installed and running
    // 3 - Installed but not running (not currently used)
    // 2 - Installed, but unknown status
    // 1 - Not installed or running (default)
    // 0 - Unknown error

    int iStatus = GetSvcStatus ( );

    if (iStatus == 4) // Installed and running
    {
      std::cout << "The background service is installed and running." << std::endl;

      int choice = 0;

      if (gbUserState)
        choice = MessageBox (NULL, L"Automatic icon promotion is ENABLED and active.\nDo you want to DEACTIVATE the functionality?", L"Notification Icon Promoter", MB_YESNOCANCEL | MB_ICONQUESTION);
      else
        choice = MessageBox (NULL, L"Automatic icon promotion is DISABLED and inactive.\nDo you want to ACTIVATE the functionality?", L"Notification Icon Promoter", MB_YESNOCANCEL | MB_ICONQUESTION);

      if (choice == IDYES)
      {
        if (SetUserSetting (! gbUserState))
          MessageBox (NULL, L"Automatic icon promotion is now ENABLED.", L"Notification Icon Promoter", MB_OK | MB_ICONINFORMATION);
        else
          MessageBox (NULL, L"Automatic icon promotion is now DISABLED.", L"Notification Icon Promoter", MB_OK | MB_ICONINFORMATION);

        // Do another one-time adjustment
        PromoteNotificationIcons ( );
      }
    }

    else if (iStatus == 3) // Installed but not running (not currently used)
    {
      std::cout << "The background service is installed but not running." << std::endl;
      std::cout << "    Restart the computer to start it up again.      " << std::endl;
    }

    else if (iStatus == 2) // Installed, but unknown status
    {
      std::cout << "The background service is installed but not running." << std::endl;
      std::cout << "    Restart the computer to start it up again.      " << std::endl;
    }

    else if (iStatus == 1) // Not installed or running (default)
    {
      std::cout << "The background service is not installed." << std::endl;
      std::cout << "Please (re)run the setup to install it. " << std::endl;
    }

    else if (iStatus == 0) // Unknown error
    {
      Util_ReportEvent (TEXT("GetSvcStatus"), EVENTLOG_ERROR_TYPE);
      std::cout << "An unexpected error occur when trying to check the service status." << std::endl;
    }

    else // Unknown error
    {
      Util_ReportEvent (TEXT("GetSvcStatus"), EVENTLOG_ERROR_TYPE, L"Catastrophic error?! This should never occur!");
      std::cout << "Catastrophic error?! This should never occur!" << std::endl;
    }

    if (iStatus != 4)
    {
      std::cout << std::endl;
      std::cout << "Press Enter to close the window." << std::endl;
      std::cin.get();
    }

  } else {
    // An unexpected error occurred when trying to start as a service?
    Util_ReportEvent (TEXT("StartServiceCtrlDispatcher"), EVENTLOG_ERROR_TYPE);
  }

  // We can now close the user-specific registry subkey as well
  RegCloseKey (ghkHKCUUserState);

  return 0;
}


//
// Purpose:
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
//
// Return value:
//   None.
//
VOID WINAPI SvcMain (DWORD dwArgc, LPTSTR* lpszArgv)
{
  // Register the handler function for the service
  gSvcStatusHandle = RegisterServiceCtrlHandler (SVCNAME, SvcCtrlHandler);

  if (! gSvcStatusHandle)
  {
    Util_ReportEvent (TEXT("RegisterServiceCtrlHandler"), EVENTLOG_ERROR_TYPE);
    return;
  }

  // These SERVICE_STATUS members remain as set here
  gSvcStatus.dwServiceType             = SERVICE_USER_OWN_PROCESS;
  gSvcStatus.dwServiceSpecificExitCode = 0;
  gSvcStatus.dwControlsAccepted        = SERVICE_CONTROL_INTERROGATE | SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_STOP;

  // Report initial status to the SCM
  ReportSvcStatus  (SERVICE_START_PENDING, NO_ERROR, 3000);
  Util_ReportEvent (TEXT("SvcMain"), EVENTLOG_INFORMATION_TYPE, TEXT("The Notification Icon Promoter Service is starting..."));

  // Perform service-specific initialization and work.
  SvcInit ( );
}


//
// Purpose:
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
//
// Return value:
//   None
//
static VOID SvcInit (VOID)
{
  // Be sure to periodically call ReportSvcStatus() with
  //   SERVICE_START_PENDING. If initialization fails, call
  //     ReportSvcStatus with SERVICE_STOPPED.

  // Creates events that the control handler function, SvcCtrlHandler,
  //   will signal when receiving various control codes from Windows.
  ghSvcStopEvent   = CreateEvent (NULL, TRUE, FALSE, NULL);
  ghSvcPauseEvent  = CreateEvent (NULL, TRUE, FALSE, NULL);
  ghSvcResumeEvent = CreateEvent (NULL, TRUE, FALSE, NULL);

  if (ghSvcStopEvent == NULL)
  {
    ReportSvcStatus  (SERVICE_STOPPED, GetLastError ( ), 0);
    Util_ReportEvent (TEXT("SvcInit"), EVENTLOG_ERROR_TYPE, TEXT("Failed to create the STOP event object."));
    return;
  }

  if (ghSvcPauseEvent == NULL)
  {
    ReportSvcStatus  (SERVICE_STOPPED, GetLastError ( ), 0);
    Util_ReportEvent (TEXT("SvcInit"), EVENTLOG_ERROR_TYPE, TEXT("Failed to create the PAUSE event object."));
    return;
  }

  if (ghSvcResumeEvent == NULL)
  {
    ReportSvcStatus  (SERVICE_STOPPED, GetLastError ( ), 0);
    Util_ReportEvent (TEXT("SvcInit"), EVENTLOG_ERROR_TYPE, TEXT("Failed to create the RESUME event object."));
    return;
  }

  // Preliminary check
  // This ensures that the service works on the first login for new accounts on the system
  DWORD timer    = Util_timeGetTime ( ); // Will abort when 60 seconds have passed...
  DWORD timerEnd = timer + 60000;
  HKEY  hkSubkey = NULL;
  while (ERROR_SUCCESS != RegOpenKeyEx (HKEY_CURRENT_USER, LR"(Control Panel\NotifyIconSettings)", 0, KEY_NOTIFY, &hkSubkey))
  {
    ReportSvcStatus (SERVICE_START_PENDING, NO_ERROR, 6000);
    Sleep (5);

    // Abort when 60 seconds have passed
    if (timerEnd < Util_timeGetTime ( ))
    {
      ReportSvcStatus  (SERVICE_STOPPED, GetLastError ( ), 0);
      Util_ReportEvent (TEXT("SvcInit"), EVENTLOG_ERROR_TYPE, TEXT("Failed to open HKCU\\Control Panel\\NotifyIconSettings for registry notifications!"));
      return;
    }
  }
  RegCloseKey (hkSubkey);

  // Create/open the per-user key
  InitUserSetting ( );

  // Set up the registry watch for the notification icons
  RegistryWatch rwNotifyIcons (HKEY_CURRENT_USER, LR"(Control Panel\NotifyIconSettings)", L"NIPNotifyIconSettingsNotify");

  // Set up the registry watch for the user setting
  RegistryWatch rwUserSetting (HKEY_CURRENT_USER, LR"(SOFTWARE\Notification Icon Promoter)", L"NIPUserSettingsNotify");

  // Enable Efficiency Mode in Windows 11 (requires idle (low) priority + EcoQoS)
  Util_SetProcessPowerThrottling (Util_GetCurrentProcess ( ), 1);
  SetPriorityClass               (Util_GetCurrentProcess ( ), IDLE_PRIORITY_CLASS);

  // Begin background processing mode
  SetThreadPriority              (GetCurrentThread ( ),       THREAD_MODE_BACKGROUND_BEGIN);

  // If the registry watch could be set up properly
  if (rwNotifyIcons._hEvent != NULL)
  {
    gvhEvents.push_back (rwNotifyIcons._hEvent); // WAIT_OBJECT_0 + 0
    gvhEvents.push_back (ghSvcStopEvent);        // WAIT_OBJECT_0 + 1
    gvhEvents.push_back (ghSvcPauseEvent);       // WAIT_OBJECT_0 + 2
    gvhEvents.push_back (rwUserSetting._hEvent); // WAIT_OBJECT_0 + 3
  //gvhEvents.push_back (ghSvcResumeEvent);      // Not used in this way
   
    // Report running status when initialization is complete.
    ReportSvcStatus  (SERVICE_RUNNING, NO_ERROR, 0);

    if (gbUserState)
      Util_ReportEvent (TEXT("SvcInit"), EVENTLOG_SUCCESS, TEXT("The Notification Icon Promoter Service started."));
    else
      Util_ReportEvent (TEXT("SvcInit"), EVENTLOG_SUCCESS, TEXT("The Notification Icon Promoter Service started, although automatic promotion is disabled per the user-specific registry value."));

    // A one-time initial run to promote any unconfigured notification icon to be visible
    PromoteNotificationIcons ( );

    bool abort = false;
    do
    {
      switch (WaitForMultipleObjects (static_cast<DWORD>(gvhEvents.size()), gvhEvents.data(), FALSE, INFINITE))
      {
        case WAIT_OBJECT_0 + 0: // Registry event was signaled
          PromoteNotificationIcons ( );
          rwNotifyIcons.reset ( );
          break;

        case WAIT_OBJECT_0 + 1: // Stop event was signaled
          ReportSvcStatus     (SERVICE_STOPPED, NO_ERROR, 0);
          Util_ReportEvent    (TEXT("SvcInit"), EVENTLOG_INFORMATION_TYPE, TEXT("The Notification Icon Promoter Service stopped."));
          abort = true;
          break;

        case WAIT_OBJECT_0 + 2: // Pause event was signaled
          ReportSvcStatus     (SERVICE_PAUSED, NO_ERROR, 0);
          Util_ReportEvent    (TEXT("SvcInit"), EVENTLOG_INFORMATION_TYPE, TEXT("The Notification Icon Promoter Service paused."));

          // Wait on the resume event until it has been signaled
          ResetEvent          (ghSvcResumeEvent);
          WaitForSingleObject (ghSvcResumeEvent, INFINITE);

          ReportSvcStatus     (SERVICE_RUNNING, NO_ERROR, 0);
          Util_ReportEvent    (TEXT("SvcInit"), EVENTLOG_INFORMATION_TYPE, TEXT("The Notification Icon Promoter Service resumed."));

          // Reset the pause event
          ResetEvent          (ghSvcPauseEvent);
          break;

        case WAIT_OBJECT_0 + 3: // User-specific setting was changed, update our internal state!
          GetUserSetting ( );
          if (gbUserState)
            Util_ReportEvent  (TEXT("SvcInit"), EVENTLOG_SUCCESS, TEXT("The Notification Icon Promoter Service enabled automatic promotion."));
          else
            Util_ReportEvent  (TEXT("SvcInit"), EVENTLOG_SUCCESS, TEXT("The Notification Icon Promoter Service disabled automatic promotion per the user-specific registry value."));
          rwUserSetting.reset();
          break;

        case WAIT_TIMEOUT:      // Timed out when waiting on the objects
          ReportSvcStatus     (SERVICE_STOPPED, ERROR_TIMEOUT, 0);
          Util_ReportEvent    (TEXT("SvcInit"), EVENTLOG_ERROR_TYPE, TEXT("WaitForMultipleObjects timed out."));
          abort = true;
          break;

        case WAIT_FAILED:       // Return value is invalid.
          ReportSvcStatus     (SERVICE_STOPPED, GetLastError ( ), 0);
          Util_ReportEvent    (TEXT("SvcInit"), EVENTLOG_ERROR_TYPE, TEXT("WaitForMultipleObjects has failed."));
          abort = true;
          break;

        default:                // Unexpected return value.
          ReportSvcStatus     (SERVICE_STOPPED, GetLastError ( ), 0);
          Util_ReportEvent    (TEXT("SvcInit"), EVENTLOG_ERROR_TYPE, TEXT("WaitForMultipleObjects returned an unexpected value."));
          abort = true;
          break;
      }
    } while (! abort);
  }

  else {
    ReportSvcStatus  (SERVICE_STOPPED, GetLastError ( ), 0);
    Util_ReportEvent (TEXT("SvcInit"), EVENTLOG_ERROR_TYPE, TEXT("Failed to initialize registry change notifications."));
  }

  return;
}


//
// Purpose:
//   Starts the Windows service, if it is installed
//
// Parameters:
//   None
//
// Return value:
//   None
//
int GetSvcStatus (VOID)
{
  // 4 - Installed and running
  // 3 - Installed but not running (not currently used)
  // 2 - Installed, but unknown status
  // 1 - Not installed or running (default)
  // 0 - Unknown error
  int status = 1;

  SERVICE_STATUS_PROCESS ssStatus{};
  DWORD                  dwBytesNeeded;
  SC_HANDLE              schSCManager;

  std::vector<std::wstring> vszSvcName;

  // Get a handle to the SCM database.
  schSCManager = OpenSCManager (
      NULL,                            // local computer
      NULL,                            // servicesActive database
      GENERIC_READ); // full access rights

  if (NULL == schSCManager)
  {
      //printf ("OpenSCManager failed (%d)\n", GetLastError ( ));
      return 0;
  }

  // Enumerate services
  DWORD bytesNeeded      = 0;
  DWORD bufferSize       = 0;
  DWORD servicesReturned = 0;
  DWORD resumeHandle     = 0;

  // First call to get buffer size needed
  EnumServicesStatus (
    schSCManager,
    SERVICE_WIN32,
    SERVICE_STATE_ALL,
    NULL,
    0,
    &bytesNeeded,
    &servicesReturned,
    &resumeHandle
  );

  bufferSize = bytesNeeded;

  LPENUM_SERVICE_STATUS lpServiceStatus =
 (LPENUM_SERVICE_STATUS) HeapAlloc (
      GetProcessHeap ( ),
      HEAP_ZERO_MEMORY,
      bufferSize
  );

  if (! lpServiceStatus)
  {
    //printf ("HeapAlloc failed.\n");
    CloseServiceHandle (schSCManager);
    return 0;
  }

  if (! EnumServicesStatus (
          schSCManager,
          SERVICE_WIN32,
          SERVICE_STATE_ALL,
          lpServiceStatus,
          bufferSize,
          &bytesNeeded,
          &servicesReturned,
          &resumeHandle))
  {
    //printf ("EnumServicesStatus failed (%d)\n", GetLastError ( ));
    HeapFree (GetProcessHeap ( ), 0, lpServiceStatus);
    CloseServiceHandle (schSCManager);
    return 0;
  }

  //printf ("Services found : %d\n", servicesReturned);
  for (DWORD i = 0; i < servicesReturned; ++i)
  {
    if (wcsstr (lpServiceStatus[i].lpServiceName, SVCNAME_) != NULL)
    {
      vszSvcName.push_back(std::wstring(lpServiceStatus[i].lpServiceName));

      status = 2;

      /*
      wprintf (L"  Service Name: %s\n", lpServiceStatus[i].lpServiceName);
      wprintf (L"  Display Name: %s\n", lpServiceStatus[i].lpDisplayName);
      wprintf (L"  Status: %d\n",       lpServiceStatus[i].ServiceStatus.dwCurrentState);
      wprintf (L"-----------------------------\n");
      */
    }
  }

  HeapFree (GetProcessHeap ( ), 0, lpServiceStatus);

  // Check the status on the individual per-user services
  for (auto &svcName : vszSvcName)
  {
    SC_HANDLE schService = OpenService (
              schSCManager,
              svcName.c_str(),
              SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS
    );

    if (schService == NULL)
    {
      //printf("OpenService failed (%d)\n", GetLastError());
      continue;
    }

    if (! QueryServiceStatusEx (
            schService,                     // handle to service
            SC_STATUS_PROCESS_INFO,         // information level
            (LPBYTE) &ssStatus,             // address of structure
            sizeof(SERVICE_STATUS_PROCESS), // size of structure
            &dwBytesNeeded ) )              // size needed if buffer is too small
    {
      //printf ("QueryServiceStatusEx failed (%d)\n", GetLastError ( ));
      CloseServiceHandle (schService);
      continue;
    }

    // Service is running
    if (ssStatus.dwCurrentState != SERVICE_STOPPED && ssStatus.dwCurrentState != SERVICE_STOP_PENDING)
    {
      if (ssStatus.dwProcessId != 0)
      {
        BOOL ownership = Util_IsProcessOwned (ssStatus.dwProcessId);

        //printf ("ssStatus.dwProcessId: %d\n", ssStatus.dwProcessId);
        //printf ("Ownership: %d\n", ownership);

        if (ownership == 1)
        {
          // Determine whether the service is running.
          if (ssStatus.dwCurrentState == SERVICE_RUNNING)
          {
            //printf ("Service is already running.\n");
            status = 4;
          }
        }
      }
    }

    CloseServiceHandle (schService);
  }

  CloseServiceHandle (schSCManager);

  // If we still have not validated its installation status yet, fall back to the registry
  if (status == 1)
  {
    if (ERROR_SUCCESS == RegGetValue (HKEY_LOCAL_MACHINE, LR"(SYSTEM\CurrentControlSet\Services\NotifyIconPromote)", L"ImagePath", RRF_RT_REG_EXPAND_SZ, NULL, NULL, NULL))
    {
      // ImagePath exists for the Per-User Service, so assume the service is installed.
      // TODO: Do more in-depth check (e.g. match ImagePath to our own path).
      status = 2;
    }
  }

  return status;
}


//
// Purpose:
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation,
//     in milliseconds
//
// Return value:
//   None
//
VOID ReportSvcStatus (DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
  static DWORD dwCheckPoint = 1;

  // Fill in the SERVICE_STATUS structure.
  gSvcStatus.dwCurrentState  = dwCurrentState;
  gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
  gSvcStatus.dwWaitHint      = dwWaitHint;

  if (dwCurrentState == SERVICE_START_PENDING)
    gSvcStatus.dwControlsAccepted = 0;
  else gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

  if ((dwCurrentState == SERVICE_RUNNING) ||
      (dwCurrentState == SERVICE_STOPPED))
    gSvcStatus.dwCheckPoint = 0;
  else gSvcStatus.dwCheckPoint = dwCheckPoint++;

  // Report the status of the service to the SCM.
  SetServiceStatus (gSvcStatusHandle, &gSvcStatus);
}


//
// Purpose:
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
//
// Return value:
//   None
//
VOID WINAPI SvcCtrlHandler (DWORD dwCtrl)
{
  // Handle the requested control code.

  switch (dwCtrl)
  {
    case SERVICE_CONTROL_STOP:
      ReportSvcStatus (SERVICE_STOP_PENDING, NO_ERROR, 0);

      // Signal the service to stop.
      SetEvent        (ghSvcStopEvent);
      ReportSvcStatus (gSvcStatus.dwCurrentState, NO_ERROR, 0);
      return;

    case SERVICE_CONTROL_PAUSE:
      ReportSvcStatus (SERVICE_PAUSE_PENDING, NO_ERROR, 0);

      // Signal the service to pause.
      SetEvent        (ghSvcPauseEvent);
      break;

    case SERVICE_CONTROL_CONTINUE:
      ReportSvcStatus (SERVICE_CONTINUE_PENDING, NO_ERROR, 0);

      // Signal the service to resume.
      SetEvent        (ghSvcResumeEvent);
      break;

    // Notifies a service that it should report its current status information to the service control manager.
    // Note that this control is not generally useful as the SCM is aware of the current state of the service.
    case SERVICE_CONTROL_INTERROGATE:
      SetServiceStatus (gSvcStatusHandle, &gSvcStatus);
      break;

    default:
      break;
  }
}