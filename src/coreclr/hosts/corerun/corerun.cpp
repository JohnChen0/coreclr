// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.


//
// .A simple CoreCLR host that runs on CoreSystem.
//

#include "windows.h"
#include <stdio.h>
#include "mscoree.h"
#include <Logger.h>
#include "palclr.h"
#include <string>
using namespace std;

// Utility macro for testing whether or not a flag is set.
#define HAS_FLAG(value, flag) (((value) & (flag)) == (flag))

// Environment variable for setting whether or not to use Server GC.
// Off by default.
static const wchar_t *serverGcVar = W("CORECLR_SERVER_GC");

// Environment variable for setting whether or not to use Concurrent GC.
// On by default.
static const wchar_t *concurrentGcVar = W("CORECLR_CONCURRENT_GC");

// The name of the CoreCLR native runtime DLL.
static const wchar_t *coreCLRDll = W("CoreCLR.dll");

// The location where CoreCLR is expected to be installed. If CoreCLR.dll isn't
//  found in the same directory as the host, it will be looked for here.
static const wchar_t *coreCLRInstallDirectory = W("%windir%\\system32\\");

static DWORD GetModuleFileNameWrapper(HMODULE hModule, wstring &path)
{
    DWORD size = 0;
    DWORD length;
    do
    {
        size = (size == 0) ? MAX_LONGPATH : 2 * size;
        path.resize(size);
    } while ((length = ::GetModuleFileNameW(hModule, &path.front(), (DWORD)path.size())) == size);
    path.resize(length);
    return length;
}

// Encapsulates the environment that CoreCLR will run in, including the TPALIST
class HostEnvironment 
{
    // The path to this module
    wstring m_hostPath;

    // The path to the directory containing this module
    wstring m_hostDirectoryPath;

    // The name of this module, without the path
    const wchar_t *m_hostExeName;

    // The list of paths to the assemblies that will be trusted by CoreCLR
    wstring m_tpaList;

    ICLRRuntimeHost2* m_CLRRuntimeHost;

    HMODULE m_coreCLRModule;

    Logger *m_log;


    // Attempts to load CoreCLR.dll from the given directory.
    // On success pins the dll, sets m_coreCLRDirectoryPath and returns the HMODULE.
    // On failure returns nullptr.
    HMODULE TryLoadCoreCLR(const wchar_t* directoryPath) {

        wstring coreCLRPath(directoryPath);
        coreCLRPath.append(coreCLRDll);

        *m_log << W("Attempting to load: ") << coreCLRPath.c_str() << Logger::endl;

        HMODULE result = ::LoadLibraryExW(coreCLRPath.c_str(), NULL, 0);
        if (!result) {
            *m_log << W("Failed to load: ") << coreCLRPath.c_str() << Logger::endl;
            *m_log << W("Error code: ") << GetLastError() << Logger::endl;
            return nullptr;
        }

        // Pin the module - CoreCLR.dll does not support being unloaded.
        HMODULE dummy_coreCLRModule;
        if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, coreCLRPath.c_str(), &dummy_coreCLRModule)) {
            *m_log << W("Failed to pin: ") << coreCLRPath.c_str() << Logger::endl;
            return nullptr;
        }

        wstring coreCLRLoadedPath;
        ::GetModuleFileNameWrapper(result, coreCLRLoadedPath);

        *m_log << W("Loaded: ") << coreCLRLoadedPath.c_str() << Logger::endl;

        return result;
    }

public:
    // The path to the directory that CoreCLR is in
    wstring m_coreCLRDirectoryPath;

    HostEnvironment(Logger *logger) 
        : m_log(logger), m_CLRRuntimeHost(nullptr) {

            // Discover the path to this exe's module. All other files are expected to be in the same directory.
            ::GetModuleFileNameWrapper(::GetModuleHandleW(nullptr), m_hostPath);

            // Search for the last backslash in the host path.
            wstring::size_type lastBackslashIndex = m_hostPath.rfind(W('\\'));

            // Copy the directory path
            m_hostDirectoryPath.assign(m_hostPath, 0, lastBackslashIndex + 1);

            // Save the exe name
            m_hostExeName = m_hostPath.c_str() + lastBackslashIndex + 1;

            *m_log << W("Host directory: ")  << m_hostDirectoryPath.c_str() << Logger::endl;

            // Check for %CORE_ROOT% and try to load CoreCLR.dll from it if it is set
            wstring coreRoot;
            coreRoot.resize(MAX_LONGPATH);
            size_t outSize;
			m_coreCLRModule = NULL; // Initialize this here since we don't call TryLoadCoreCLR if CORE_ROOT is unset.
            errno_t err = _wgetenv_s(&outSize, &coreRoot.front(), coreRoot.size(), W("CORE_ROOT"));
            if (err == ERANGE)
            {
                coreRoot.resize(outSize);
                err = _wgetenv_s(&outSize, &coreRoot.front(), coreRoot.size(), W("CORE_ROOT"));
            }
            if (err == 0 && outSize > 0)
            {
                coreRoot.resize(outSize - 1);
                coreRoot.append(W("\\"));
                m_coreCLRModule = TryLoadCoreCLR(coreRoot.c_str());
            }
            else
            {
                *m_log << W("CORE_ROOT not set; skipping") << Logger::endl;
                *m_log << W("You can set the environment variable CORE_ROOT to point to the path") << Logger::endl;
                *m_log << W("where CoreCLR.dll lives to help CoreRun.exe find it.") << Logger::endl;
            }

            // Try to load CoreCLR from the directory that coreRun is in
            if (!m_coreCLRModule) {
                m_coreCLRModule = TryLoadCoreCLR(m_hostDirectoryPath.c_str());
            }

            if (!m_coreCLRModule) {

                // Failed to load. Try to load from the well-known location.
                wstring coreCLRInstallPath;
                coreCLRInstallPath.resize(MAX_LONGPATH);
                DWORD length = ::ExpandEnvironmentStringsW(coreCLRInstallDirectory, &coreCLRInstallPath.front(), (DWORD)coreCLRInstallPath.size());
                if (length >= coreCLRInstallPath.size())
                {
                    coreCLRInstallPath.resize(length);
                    length = ::ExpandEnvironmentStringsW(coreCLRInstallDirectory, &coreCLRInstallPath.front(), (DWORD)coreCLRInstallPath.size());
                }
                coreCLRInstallPath.resize(length - 1);
                m_coreCLRModule = TryLoadCoreCLR(coreCLRInstallPath.c_str());

            }

            if (m_coreCLRModule) {

                // Save the directory that CoreCLR was found in
                DWORD modulePathLength = ::GetModuleFileNameWrapper(m_coreCLRModule, m_coreCLRDirectoryPath);

                // Search for the last backslash and terminate it there to keep just the directory path with trailing slash
                m_coreCLRDirectoryPath.resize(m_coreCLRDirectoryPath.rfind(W('\\'), modulePathLength) + 1);

            } else {
                *m_log << W("Unable to load ") << coreCLRDll << Logger::endl;
            }
    }

    ~HostEnvironment() {
        if(m_coreCLRModule) {
            // Free the module. This is done for completeness, but in fact CoreCLR.dll 
            // was pinned earlier so this call won't actually free it. The pinning is
            // done because CoreCLR does not support unloading.
            ::FreeLibrary(m_coreCLRModule);
        }
    }

    bool TPAListContainsFile(_In_z_ wchar_t* fileNameWithoutExtension, _In_reads_(countExtensions) wchar_t** rgTPAExtensions, int countExtensions)
    {
        if (m_tpaList.length() == 0) return false;

        for (int iExtension = 0; iExtension < countExtensions; iExtension++)
        {
            wstring fileName;
            fileName.append(W("\\")); // So that we don't match other files that end with the current file name
            fileName.append(fileNameWithoutExtension);
            fileName.append(rgTPAExtensions[iExtension] + 1);
            fileName.append(W(";")); // So that we don't match other files that begin with the current file name

            if (m_tpaList.find(fileName) != wstring::npos)
            {
                return true;
            }
        }
        return false;
    }

    void RemoveExtensionAndNi(_In_z_ wchar_t* fileName)
    {
        // Remove extension, if it exists
        wchar_t* extension = wcsrchr(fileName, W('.')); 
        if (extension != NULL)
        {
            extension[0] = W('\0');

            // Check for .ni
            size_t len = wcslen(fileName);
            if (len > 3 &&
                fileName[len - 1] == W('i') &&
                fileName[len - 2] == W('n') &&
                fileName[len - 3] == W('.') )
            {
                fileName[len - 3] = W('\0');
            }
        }
    }

    void AddFilesFromDirectoryToTPAList(_In_z_ const wchar_t* targetPath, _In_reads_(countExtensions) wchar_t** rgTPAExtensions, int countExtensions)
    {
        *m_log << W("Adding assemblies from ") << targetPath << W(" to the TPA list") << Logger::endl;
        wstring assemblyPath;
        const size_t dirLength = wcslen(targetPath);

        for (int iExtension = 0; iExtension < countExtensions; iExtension++)
        {
            assemblyPath.assign(targetPath, dirLength);
            assemblyPath.append(rgTPAExtensions[iExtension]);
            WIN32_FIND_DATA data;
            HANDLE findHandle = FindFirstFile(assemblyPath.c_str(), &data);

            if (findHandle != INVALID_HANDLE_VALUE) {
                do {
                    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        // It seems that CoreCLR doesn't always use the first instance of an assembly on the TPA list (ni's may be preferred
                        // over il, even if they appear later). So, only include the first instance of a simple assembly name to allow
                        // users the opportunity to override Framework assemblies by placing dlls in %CORE_LIBRARIES%

                        // ToLower for case-insensitive comparisons
                        wchar_t* fileNameChar = data.cFileName;
                        while (*fileNameChar)
                        {
                            *fileNameChar = towlower(*fileNameChar);
                            fileNameChar++;
                        }

                        // Remove extension
                        wchar_t fileNameWithoutExtension[MAX_PATH_FNAME];
                        wcscpy_s(fileNameWithoutExtension, MAX_PATH_FNAME, data.cFileName);

                        RemoveExtensionAndNi(fileNameWithoutExtension);

                        // Add to the list if not already on it
                        if (!TPAListContainsFile(fileNameWithoutExtension, rgTPAExtensions, countExtensions))
                        {
                            assemblyPath.resize(dirLength);
                            assemblyPath.append(data.cFileName);
                            m_tpaList.append(assemblyPath);
                            m_tpaList.append(W(";"), 1);
                        }
                        else
                        {
                            *m_log << W("Not adding ") << targetPath << data.cFileName << W(" to the TPA list because another file with the same name is already present on the list") << Logger::endl;
                        }
                    }
                } while (0 != FindNextFile(findHandle, &data));

                FindClose(findHandle);
            }
        }
    }

    // Returns the semicolon-separated list of paths to runtime dlls that are considered trusted.
    // On first call, scans the coreclr directory for dlls and adds them all to the list.
    const wchar_t * GetTpaList() {
        if (m_tpaList.length() == 0) {
            wchar_t *rgTPAExtensions[] = {
                        W("*.ni.dll"),		// Probe for .ni.dll first so that it's preferred if ni and il coexist in the same dir
                        W("*.dll"),
                        W("*.ni.exe"),
                        W("*.exe"),
                        W("*.ni.winmd")
                        W("*.winmd")
                        };

            // Add files from %CORE_LIBRARIES% if specified
            wstring coreLibraries;
            coreLibraries.resize(MAX_LONGPATH);
            size_t outSize;
            errno_t err = _wgetenv_s(&outSize, &coreLibraries.front(), coreLibraries.size(), W("CORE_LIBRARIES"));
            if (err == ERANGE)
            {
                coreLibraries.resize(outSize);
                err = _wgetenv_s(&outSize, &coreLibraries.front(), coreLibraries.size(), W("CORE_LIBRARIES"));
            }
            if (err == 0 && outSize > 0)
            {
                coreLibraries.resize(outSize - 1);
                coreLibraries.append(W("\\"));
                AddFilesFromDirectoryToTPAList(coreLibraries.c_str(), rgTPAExtensions, _countof(rgTPAExtensions));
            }
            else
            {
                *m_log << W("CORE_LIBRARIES not set; skipping") << Logger::endl;
                *m_log << W("You can set the environment variable CORE_LIBRARIES to point to a") << Logger::endl;
                *m_log << W("path containing additional platform assemblies,") << Logger::endl;
            }

            AddFilesFromDirectoryToTPAList(m_coreCLRDirectoryPath.c_str(), rgTPAExtensions, _countof(rgTPAExtensions));
        }

        return m_tpaList.c_str();
    }

    // Returns the path to the host module
    const wchar_t * GetHostPath() {
        return m_hostPath.c_str();
    }

    // Returns the path to the host module
    const wchar_t * GetHostExeName() {
        return m_hostExeName;
    }

    // Returns the ICLRRuntimeHost2 instance, loading it from CoreCLR.dll if necessary, or nullptr on failure.
    ICLRRuntimeHost2* GetCLRRuntimeHost() {
        if (!m_CLRRuntimeHost) {

            if (!m_coreCLRModule) {
                *m_log << W("Unable to load ") << coreCLRDll << Logger::endl;
                return nullptr;
            }

            *m_log << W("Finding GetCLRRuntimeHost(...)") << Logger::endl;

            FnGetCLRRuntimeHost pfnGetCLRRuntimeHost = 
                (FnGetCLRRuntimeHost)::GetProcAddress(m_coreCLRModule, "GetCLRRuntimeHost");

            if (!pfnGetCLRRuntimeHost) {
                *m_log << W("Failed to find function GetCLRRuntimeHost in ") << coreCLRDll << Logger::endl;
                return nullptr;
            }

            *m_log << W("Calling GetCLRRuntimeHost(...)") << Logger::endl;

            HRESULT hr = pfnGetCLRRuntimeHost(IID_ICLRRuntimeHost2, (IUnknown**)&m_CLRRuntimeHost);
            if (FAILED(hr)) {
                *m_log << W("Failed to get ICLRRuntimeHost2 interface. ERRORCODE: ") << Logger::hresult << hr << Logger::endl;
                return nullptr;
            }
        }

        return m_CLRRuntimeHost;
    }


};

// Creates the startup flags for the runtime, starting with the default startup
// flags and adding or removing from them based on environment variables. Only
// two environment variables are respected right now: serverGcVar, controlling
// Server GC, and concurrentGcVar, controlling Concurrent GC.
STARTUP_FLAGS CreateStartupFlags() {
    auto initialFlags = 
        static_cast<STARTUP_FLAGS>(
            STARTUP_FLAGS::STARTUP_LOADER_OPTIMIZATION_SINGLE_DOMAIN | 
            STARTUP_FLAGS::STARTUP_SINGLE_APPDOMAIN |
            STARTUP_FLAGS::STARTUP_CONCURRENT_GC);
        
    // server GC is off by default, concurrent GC is on by default.
    auto checkVariable = [&](STARTUP_FLAGS flag, const wchar_t *var) {
        wchar_t result[25];
        size_t outsize;
        if (_wgetenv_s(&outsize, result, 25, var) == 0 && outsize > 0) {
            // set the flag if the var is present and set to 1,
            // clear the flag if the var isp resent and set to 0.
            // Otherwise, ignore it.
            if (_wcsicmp(result, W("1")) == 0) {
                initialFlags = static_cast<STARTUP_FLAGS>(initialFlags | flag);
            } else if (_wcsicmp(result, W("0")) == 0) {
                initialFlags = static_cast<STARTUP_FLAGS>(initialFlags & ~flag);
            }
        }
    };
    
    checkVariable(STARTUP_FLAGS::STARTUP_SERVER_GC, serverGcVar);
    checkVariable(STARTUP_FLAGS::STARTUP_CONCURRENT_GC, concurrentGcVar);
        
    return initialFlags;
}

bool TryRun(const int argc, const wchar_t* argv[], Logger &log, const bool verbose, const bool waitForDebugger, DWORD &exitCode)
{

    // Assume failure
    exitCode = -1;

    HostEnvironment hostEnvironment(&log);

    //-------------------------------------------------------------

    // Find the specified exe. This is done using LoadLibrary so that
    // the OS library search semantics are used to find it.

    const wchar_t* exeName = argc > 0 ? argv[0] : nullptr;
    if(exeName == nullptr)
    {
        log << W("No exename specified.") << Logger::endl;
        return false;
    }

    wstring appPath;
    wstring appNiPath;
    wstring managedAssemblyFullName;
    wstring appLocalWinmetadata;
    
    wchar_t* filePart = NULL;
    
    appPath.resize(MAX_LONGPATH);
    DWORD length = ::GetFullPathName(exeName, (DWORD)appPath.size(), &appPath.front(), &filePart);
    if (length >= appPath.size())
    {
        appPath.resize(length);
        length = ::GetFullPathName(exeName, (DWORD)appPath.size(), &appPath.front(), &filePart);
    }
    if (length == 0) {
        log << W("Failed to get full path: ") << exeName << Logger::endl;
        log << W("Error code: ") << GetLastError() << Logger::endl;
        return false;
    } 
    size_t pathLength = filePart - &appPath.front();
    appPath.resize(length);

    managedAssemblyFullName = appPath;

    appPath.resize(pathLength);

    log << W("Loading: ") << managedAssemblyFullName.c_str() << Logger::endl;
   
    appLocalWinmetadata = appPath;
    appLocalWinmetadata.append(W("\\WinMetadata"));
   
    DWORD dwAttrib = ::GetFileAttributes(appLocalWinmetadata.c_str());
    bool appLocalWinMDexists = dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY);

    if (!appLocalWinMDexists) {
        appLocalWinmetadata.clear();
    }
    appNiPath = appPath;
    appNiPath.append(W("NI"));
    appNiPath.append(W(";"));
    appNiPath.append(appPath);

    // Construct native search directory paths
    wstring nativeDllSearchDirs(appPath);
    wstring coreLibraries;
    coreLibraries.resize(MAX_LONGPATH);
    size_t outSize;
    errno_t err = _wgetenv_s(&outSize, &coreLibraries.front(), coreLibraries.size(), W("CORE_LIBRARIES"));
    if (err == ERANGE)
    {
        coreLibraries.resize(outSize);
        err = _wgetenv_s(&outSize, &coreLibraries.front(), coreLibraries.size(), W("CORE_LIBRARIES"));
    }
    if (err == 0 && outSize > 0)
    {
        coreLibraries.resize(outSize - 1);
        nativeDllSearchDirs.append(W(";"));
        nativeDllSearchDirs.append(coreLibraries);
    }
    nativeDllSearchDirs.append(W(";"));
    nativeDllSearchDirs.append(hostEnvironment.m_coreCLRDirectoryPath);

    // Start the CoreCLR

    ICLRRuntimeHost2 *host = hostEnvironment.GetCLRRuntimeHost();
    if (!host) {
        return false;
    }

    HRESULT hr;
    
    
    STARTUP_FLAGS flags = CreateStartupFlags();
    log << W("Setting ICLRRuntimeHost2 startup flags") << Logger::endl;
    log << W("Server GC enabled: ") << HAS_FLAG(flags, STARTUP_FLAGS::STARTUP_SERVER_GC) << Logger::endl;
    log << W("Concurrent GC enabled: ") << HAS_FLAG(flags, STARTUP_FLAGS::STARTUP_CONCURRENT_GC) << Logger::endl;

    // Default startup flags
    hr = host->SetStartupFlags(flags);
    if (FAILED(hr)) {
        log << W("Failed to set startup flags. ERRORCODE: ") << Logger::hresult << hr << Logger::endl;
        return false;
    }

    log << W("Starting ICLRRuntimeHost2") << Logger::endl;

    hr = host->Start();
    if (FAILED(hr)) {
        log << W("Failed to start CoreCLR. ERRORCODE: ") << Logger::hresult << hr << Logger:: endl;
        return false;
    }

    //-------------------------------------------------------------

    // Create an AppDomain

    // Allowed property names:
    // APPBASE
    // - The base path of the application from which the exe and other assemblies will be loaded
    //
    // TRUSTED_PLATFORM_ASSEMBLIES
    // - The list of complete paths to each of the fully trusted assemblies
    //
    // APP_PATHS
    // - The list of paths which will be probed by the assembly loader
    //
    // APP_NI_PATHS
    // - The list of additional paths that the assembly loader will probe for ngen images
    //
    // NATIVE_DLL_SEARCH_DIRECTORIES
    // - The list of paths that will be probed for native DLLs called by PInvoke
    //
    const wchar_t *property_keys[] = { 
        W("TRUSTED_PLATFORM_ASSEMBLIES"),
        W("APP_PATHS"),
        W("APP_NI_PATHS"),
        W("NATIVE_DLL_SEARCH_DIRECTORIES"),
        W("AppDomainCompatSwitch"),
        W("APP_LOCAL_WINMETADATA")
    };
    const wchar_t *property_values[] = { 
        // TRUSTED_PLATFORM_ASSEMBLIES
        hostEnvironment.GetTpaList(),
        // APP_PATHS
        appPath.c_str(),
        // APP_NI_PATHS
        appNiPath.c_str(),
        // NATIVE_DLL_SEARCH_DIRECTORIES
        nativeDllSearchDirs.c_str(),
        // AppDomainCompatSwitch
        W("UseLatestBehaviorWhenTFMNotSpecified"),
        // APP_LOCAL_WINMETADATA
        appLocalWinmetadata.c_str()
    };
     
    log << W("Creating an AppDomain") << Logger::endl;
    for (int idx = 0; idx < sizeof(property_keys) / sizeof(wchar_t*); idx++)
    {
        log << property_keys[idx] << W("=") << property_values[idx] << Logger::endl;
    }

    DWORD domainId;

    hr = host->CreateAppDomainWithManager(
        hostEnvironment.GetHostExeName(),   // The friendly name of the AppDomain
        // Flags:
        // APPDOMAIN_ENABLE_PLATFORM_SPECIFIC_APPS
        // - By default CoreCLR only allows platform neutral assembly to be run. To allow
        //   assemblies marked as platform specific, include this flag
        //
        // APPDOMAIN_ENABLE_PINVOKE_AND_CLASSIC_COMINTEROP
        // - Allows sandboxed applications to make P/Invoke calls and use COM interop
        //
        // APPDOMAIN_SECURITY_SANDBOXED
        // - Enables sandboxing. If not set, the app is considered full trust
        //
        // APPDOMAIN_IGNORE_UNHANDLED_EXCEPTION
        // - Prevents the application from being torn down if a managed exception is unhandled
        //
        APPDOMAIN_ENABLE_PLATFORM_SPECIFIC_APPS |
        APPDOMAIN_ENABLE_PINVOKE_AND_CLASSIC_COMINTEROP |
        APPDOMAIN_DISABLE_TRANSPARENCY_ENFORCEMENT,
        NULL,                // Name of the assembly that contains the AppDomainManager implementation
        NULL,                    // The AppDomainManager implementation type name
        sizeof(property_keys)/sizeof(wchar_t*),  // The number of properties
        property_keys,
        property_values,
        &domainId);

    if (FAILED(hr)) {
        log << W("Failed call to CreateAppDomainWithManager. ERRORCODE: ") << Logger::hresult << hr << Logger::endl;
        return false;
    }

    if(waitForDebugger)
    {
        if(!IsDebuggerPresent())
        {
            log << W("Waiting for the debugger to attach. Press any key to continue ...") << Logger::endl;
            getchar();
            if (IsDebuggerPresent())
            {
                log << "Debugger is attached." << Logger::endl;
            }
            else
            {
                log << "Debugger failed to attach." << Logger::endl;
            }
        }
    }

    hr = host->ExecuteAssembly(domainId, managedAssemblyFullName.c_str(), argc-1, (argc-1)?&(argv[1]):NULL, &exitCode);
    if (FAILED(hr)) {
        log << W("Failed call to ExecuteAssembly. ERRORCODE: ") << Logger::hresult << hr << Logger::endl;
        return false;
    }

    log << W("App exit value = ") << exitCode << Logger::endl;


    //-------------------------------------------------------------

    // Unload the AppDomain

    log << W("Unloading the AppDomain") << Logger::endl;

    hr = host->UnloadAppDomain(
        domainId, 
        true);                          // Wait until done

    if (FAILED(hr)) {
        log << W("Failed to unload the AppDomain. ERRORCODE: ") << Logger::hresult << hr << Logger::endl;
        return false;
    }


    //-------------------------------------------------------------

    // Stop the host

    log << W("Stopping the host") << Logger::endl;

    hr = host->Stop();

    if (FAILED(hr)) {
        log << W("Failed to stop the host. ERRORCODE: ") << Logger::hresult << hr << Logger::endl;
        return false;
    }

    //-------------------------------------------------------------

    // Release the reference to the host

    log << W("Releasing ICLRRuntimeHost2") << Logger::endl;

    host->Release();

    return true;

}

void showHelp() {
    ::wprintf(
        W("Runs executables on CoreCLR\r\n")
        W("\r\n")
        W("USAGE: coreRun [/d] [/v] Managed.exe\r\n")
        W("\r\n")
        W("  where Managed.exe is a managed executable built for CoreCLR\r\n")
        W("        /v causes verbose output to be written to the console\r\n")
        W("        /d causes coreRun to wait for a debugger to attach before\r\n")
        W("         launching Managed.exe\r\n")
        W("\r\n")
        W("  CoreCLR is searched for in %%core_root%%, then in the directory\r\n")
        W("  that coreRun.exe is in, then finally in %s.\r\n"),
        coreCLRInstallDirectory
        );
}

int __cdecl wmain(const int argc, const wchar_t* argv[])
{
    // Parse the options from the command line

    bool verbose = false;
    bool waitForDebugger = false;
    bool helpRequested = false;
    int newArgc = argc - 1;
    const wchar_t **newArgv = argv + 1;

    auto stringsEqual = [](const wchar_t * const a, const wchar_t * const b) -> bool {
        return ::_wcsicmp(a, b) == 0;
    };

    auto tryParseOption = [&](const wchar_t* arg) -> bool {
        if ( stringsEqual(arg, W("/v")) || stringsEqual(arg, W("-v")) ) {
                verbose = true;
                return true;
        } else if ( stringsEqual(arg, W("/d")) || stringsEqual(arg, W("-d")) ) {
                waitForDebugger = true;
                return true;
        } else if ( stringsEqual(arg, W("/?")) || stringsEqual(arg, W("-?")) ) {
            helpRequested = true;
            return true;
        } else {
            return false;
        }
    };

    while (newArgc > 0 && tryParseOption(newArgv[0])) {
        newArgc--;
        newArgv++;
    }

    if (argc < 2 || helpRequested || newArgc==0) {
        showHelp();
        return -1;
    } else {
        Logger log;
        if (verbose) {
            log.Enable();
        } else {
            log.Disable();
        }

        DWORD exitCode;
        auto success = TryRun(newArgc, newArgv, log, verbose, waitForDebugger, exitCode);

        log << W("Execution ") << (success ? W("succeeded") : W("failed")) << Logger::endl;

        return exitCode;
    }
}
