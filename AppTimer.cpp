//=========================================================================
//                      App Timer
//=========================================================================
// by      : INSANE
// created : 12/01/2026
//
// purpose : Closes applications after set time.
//-------------------------------------------------------------------------
#include <cstdint>
#include <windows.h>
#include <TlHelp32.h>
#include <assert.h>
#include <processthreadsapi.h>
#include <chrono>
#include <thread>   


// Colors in terminal
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[93m"
#define RESET   "\033[0m"
#define CYAN    "\033[96m"


// Globals...
namespace AppTimer
{
    const char* g_szHelpCommand = 
        R"(
        AppTimer help

        -h    : Help string
        -t    : Set target process's name
        -c    : Set timer amount in minutes.
        -m    : Max CPU utilization before closing itself.
        -l    : True or false. Print logs or not.
        -p    : Dump list of all running processes.

        )";

        namespace CLA 
        {
            const char* g_szCLATarget    = "-t";
            const char* g_szCLATimer     = "-c";
            const char* g_szMaxCPU       = "-m";
            const char* g_szLog          = "-l";
            const char* g_szHelp         = "-h";
            const char* g_szDumpProcList = "-p";
        }
    const char* g_szTargetApp  = nullptr;
    int         g_iTimer       = -1;
    float       g_flMaxCpuUtil = 1.0f; // default is 1%. should be good enough.
    bool        g_bPrintLogs   = true;


    ///////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////
    struct Timer_t
    {
        Timer_t() {}

        ///////////////////////////////////////////////////////////////////////////
        static inline uint64_t FileTimeToULL(FILETIME ft)
        {
            return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | static_cast<uint64_t>(ft.dwLowDateTime);
        }

        ///////////////////////////////////////////////////////////////////////////
        inline void UpdateProcTime() { GetProcessTimes(GetCurrentProcess(), &m_ftCreationTime, &m_ftExitTime, &m_ftKernelTime, &m_ftUserTime); }


        ///////////////////////////////////////////////////////////////////////////
        Timer_t& operator=(const Timer_t& other)
        {
            m_ftCreationTime = other.m_ftCreationTime;
            m_ftExitTime     = other.m_ftExitTime;
            m_ftKernelTime   = other.m_ftKernelTime;
            m_ftUserTime     = other.m_ftUserTime;

            return *this;
        }


        inline uint64_t KernelTime()   const { return FileTimeToULL(m_ftKernelTime);   }
        inline uint64_t UserTime()     const { return FileTimeToULL(m_ftUserTime);     }
        inline uint64_t CreationTime() const { return FileTimeToULL(m_ftCreationTime); }
        inline uint64_t ExitTime()     const { return FileTimeToULL(m_ftExitTime);     }

        FILETIME m_ftCreationTime;
        FILETIME m_ftExitTime; // useless.
        FILETIME m_ftKernelTime;
        FILETIME m_ftUserTime;
    };


    ///////////////////////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////
    static bool GetTargetProcessID(const char* szTargetProc, PROCESSENTRY32* pProcOut);
    static bool KillProcess(DWORD pID);
    static bool HandleCLA(int nArgs, char** args);
    static void DumpProcList();
}


///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
int main(int nArgs, char** args)
{
    bool bShouldRun = AppTimer::HandleCLA(nArgs, args);
    
    if (bShouldRun == false)
        return 0;


    // Verify Arguments before running. ( This shouldn't get triggered cause we are hanlding arguments in HandleCLA properly. Hence we are using assert and not if-elses )
    assert(AppTimer::g_szTargetApp != nullptr && AppTimer::g_iTimer > 0 && AppTimer::g_flMaxCpuUtil > 0.0f && "Invalid arguments set by HandleCLA!");


    // CPU core count on this machine ( used to calculate CPU utilization. )
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);


    // Timer objects
    std::chrono::high_resolution_clock::time_point lastCheckTime = std::chrono::high_resolution_clock::now();
    AppTimer::Timer_t lastCheckProcTimer; lastCheckProcTimer.UpdateProcTime();
    double iTimer = static_cast<double>(AppTimer::g_iTimer);


    printf(GREEN "[ AppTimer ] Starting:\nTarget Proc : %s, Timer : %d minutes, Max CPU Util : %.2f, Logs : %s, Cores : %lu\n" RESET, 
            AppTimer::g_szTargetApp, AppTimer::g_iTimer, AppTimer::g_flMaxCpuUtil, AppTimer::g_bPrintLogs == true ? " True" : "False", sysInfo.dwNumberOfProcessors);

    // Main loop
    while(true)
    {
        std::this_thread::sleep_for(std::chrono::minutes(1llu));


        // Now time / timer.
        std::chrono::high_resolution_clock::time_point timeNow = std::chrono::high_resolution_clock::now();
        AppTimer::Timer_t procTimer; procTimer.UpdateProcTime();


        // Time elapsed.
        uint64_t iTimeElapsedInMs     = std::chrono::duration_cast<std::chrono::milliseconds>(timeNow - lastCheckTime).count();
        uint64_t iTimeElapsedInNs     = std::chrono::duration_cast<std::chrono::nanoseconds> (timeNow - lastCheckTime).count();
        uint64_t iExecTimeElapsedInNs = (procTimer.KernelTime() + procTimer.UserTime()) - (lastCheckProcTimer.KernelTime() + lastCheckProcTimer.UserTime());
        iExecTimeElapsedInNs *= 100llu; // FILETIME is count of "100 nanosecond" ticks, and not absolute time. Change it to nanoseconds.


        // Update timer.
        iTimer -= static_cast<double>(iTimeElapsedInMs) / (1000.0 * 60.0);
        iTimer  = iTimer < 0.0 ? 0.0 : iTimer;


        // Calculate CPU utilization.
        double flCpuUtil = static_cast<double>(iExecTimeElapsedInNs) / static_cast<double>(iTimeElapsedInNs);
        flCpuUtil /= static_cast<double>(sysInfo.dwNumberOfProcessors); // Devide by total cores to get the total machine's utilization.
        flCpuUtil *= 100.0; // To convert it to percentage.


        // Remember last check time.
        lastCheckTime      = timeNow;
        lastCheckProcTimer = procTimer;


        if(AppTimer::g_bPrintLogs == true)
        {
            printf(CYAN "[ AppTimer ] %s Time Left : %.2f / %.2f minutes. CPU Util : %.2f\n", RESET, iTimer, static_cast<float>(AppTimer::g_iTimer), flCpuUtil);
        }

        
        // Stay under max CPU util or leave.
        if(flCpuUtil > AppTimer::g_flMaxCpuUtil)
        {
            printf(RED "WARNING : CPU Utilization is [ %.2f ]. Max CPU Util set to [ %.2f ]\n" RESET, flCpuUtil, AppTimer::g_flMaxCpuUtil);
            return 0;
        }


        if(iTimer <= 0.0)
            break;
    }


    PROCESSENTRY32 proc;
    bool bTargetFound = AppTimer::GetTargetProcessID(AppTimer::g_szTargetApp, &proc);
    if(bTargetFound == true)
    {
        AppTimer::KillProcess(proc.th32ProcessID);
        printf(GREEN "Process [ %s ] terminated after [ %d ] minutes!\n" RESET, AppTimer::g_szTargetApp, AppTimer::g_iTimer);
    }
    else // Incase target was closed before timer ending.
    {
        printf(RED "Target process [ %s ] is not running.\n" RESET, AppTimer::g_szTargetApp);
    }


    printf("Closing...");
    return 0;
}


///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
static bool AppTimer::GetTargetProcessID(const char* szTargetProc, PROCESSENTRY32* pProcOut)
{
    assert(szTargetProc[0] != '\0' && "Target process's name can't be empty");
    assert(pProcOut != nullptr && "Invalid process out");
    if(pProcOut == nullptr || szTargetProc[0] == '\0')
        return false;

    
    // acquire process snap shot
    HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if(hSnapShot == INVALID_HANDLE_VALUE)
    {
        printf("Failed to create snapshot.\n");
        return false;
    }


    // Initialize process struct.
    pProcOut->dwSize = sizeof(PROCESSENTRY32);


    // Get First process from the snapshot.
    bool bProcFound = Process32First(hSnapShot, pProcOut);
    if(bProcFound == false)
    {
        printf("Process32First failed!\n");
        return false;
    }


    // Iterate all Processes.
    bool bMatchFound = false;
    do 
    {
        if(strcmp(szTargetProc, pProcOut->szExeFile) == 0)
        {
            bMatchFound = true;
            break;
        }

    } while(Process32Next(hSnapShot, pProcOut) != false);


    // Close the handle!
    CloseHandle(hSnapShot);

    return bMatchFound;
}


///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
static bool AppTimer::KillProcess(DWORD pID)
{
    HANDLE hTarget = OpenProcess(PROCESS_TERMINATE, FALSE, pID);

    // Target process doesn't exist.
    if(hTarget == INVALID_HANDLE_VALUE)
        return false;

    // KILL THE PROCESS!
    bool bClosed = TerminateProcess(hTarget, 1);
    CloseHandle(hTarget);
    return bClosed;
}


///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
static bool AppTimer::HandleCLA(int nArgs, char** args)
{
    // Atleast gotta set the target app's name.
    if (nArgs <= 1)
    {
        printf("%s", AppTimer::g_szHelpCommand);
        return false;
    }

    
    // If any arg is help, don't run.
    for(int i = 1; i < nArgs; i++)
    {
        char* szArg = args[i];


        // Invlaid arguments?
        if(szArg[0] == '\0')
        {
            printf("Invalid argument. use -h for help\n");
            return false;
        }


        if(szArg[0] != '-')
        {
            printf("Invalid argument [ %s ]\n", szArg);
            return false;
        }


        if(strcmp(AppTimer::CLA::g_szHelp, szArg) == 0)
        {
            printf("%s", AppTimer::g_szHelpCommand);
            return false;
        }
        else if(strcmp(AppTimer::CLA::g_szDumpProcList, szArg) == 0)
        {
            AppTimer::DumpProcList();
            return false;
        }
        else
        {
            // Must have the arguments value in front.
            if (i + 1 >= nArgs)
            {
                printf("Value for arg [ %s ] not found. Use -h for help\n", szArg);
                return false;
            }

            char* szValue = args[i + 1];

            if(strcmp(AppTimer::CLA::g_szCLATarget, szArg) == 0)
            {

                PROCESSENTRY32 proc;
                bool bProcFound = AppTimer::GetTargetProcessID(szValue, &proc);

                if(bProcFound == false)
                {
                    printf("Process [ %s ] is not running.\n", szValue);
                    return false;
                }

                AppTimer::g_szTargetApp = szValue;
            }
            else if(strcmp(AppTimer::CLA::g_szCLATimer, szArg) == 0)
            {
                int iTimer = atoi(szValue);

                // valid timer?
                if(iTimer < 1)
                {
                    printf("Timer for [ %d ] minute can't be set. Timer must be >= 1.\n", iTimer);
                    return false;
                }

                AppTimer::g_iTimer = iTimer;
            }
            else if(strcmp(AppTimer::CLA::g_szLog, szArg) == 0)
            {
                // To lower case first.
                char* szValueDup = szValue;
                while(*szValueDup != '\0') { if(*szValueDup >= 'A' && *szValueDup <= 'Z') *szValueDup = *szValueDup - 'A' + 'a'; szValueDup++; }

                if(strcmp(szValue, "true") == 0)
                {
                    AppTimer::g_bPrintLogs = true;
                }
                else if(strcmp(szValue, "false") == 0)
                {
                    AppTimer::g_bPrintLogs = false;
                }
                else
                {
                    printf("Invalid value [ %s ] for -l argument.\n", szValue);
                    return false;
                }
            }
            else if(strcmp(AppTimer::CLA::g_szMaxCPU, szArg) == 0)
            {
                float flCpuUtil = atof(szValue);

                // Invalid cpu utilizatoin.
                if(flCpuUtil <= 0.0f)
                {
                    printf("Invalid CPU utilization value [ %.2f ]. %s\n", flCpuUtil, 
                            flCpuUtil < 0.0f ? "Your CPU can do - utilization!? Dayem? Those temu niggas did u bad bro." : "U using super computer bitch nigga?!");

                    return false;
                }


                // Warm if CPU utilizatoin is not practical or harendous.
                if(flCpuUtil >= 25.0f)
                {
                    printf("Warning : %.2f%% is not practical. This process will never reach that level. Try something between 0.1%% to 5%% next time.", flCpuUtil);
                }


                AppTimer::g_flMaxCpuUtil = flCpuUtil;
            }
            else
            {
                printf("Invalid argument. Use -h for help\n");
                return false;
            }

            // Assuming we have consumed the next argument. skip the next arg.
            i++;
        }

    }


    return true;
}


///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////
static void AppTimer::DumpProcList()
{
    // acquire process snap shot
    HANDLE hSnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if(hSnapShot == INVALID_HANDLE_VALUE)
    {
        printf("Failed to create snapshot.\n");
        return;
    }


    // Initialize process struct.
    PROCESSENTRY32 proc;
    proc.dwSize = sizeof(PROCESSENTRY32);


    // Get First process from the snapshot.
    bool bProcFound = Process32First(hSnapShot, &proc);
    if(bProcFound == false)
    {
        printf("Process32First failed!\n");
        return;
    }


    // Iterate all Processes.
    do 
    {
        printf("PID : %6lu, Name : %s\n", proc.th32ProcessID, proc.szExeFile);

    } while(Process32Next(hSnapShot, &proc) != false);


    // Close the handle!
    CloseHandle(hSnapShot);

    return;
}
