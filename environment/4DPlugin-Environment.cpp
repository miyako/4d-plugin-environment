/* --------------------------------------------------------------------------------
 #
 #  4DPlugin-Environment.cpp
 #	source generated by 4D Plugin Wizard
 #	Project : Environment
 #	author : miyako
 #	2020/06/25
 #  
 # --------------------------------------------------------------------------------*/

#include "4DPlugin-Environment.h"

#pragma mark -

static std::mutex gMutexEnvironment;

static bool wasMDI;

#if VERSIONWIN
static WNDPROC originalWndProc = NULL;
#endif

static bool isSDI() {
    
#if VERSIONWIN
    
    PA_Variable args[5];

    args[0] = PA_CreateVariable(eVK_Longint);
    args[1] = PA_CreateVariable(eVK_Longint);
    args[2] = PA_CreateVariable(eVK_Longint);
    args[3] = PA_CreateVariable(eVK_Longint);
    args[4] = PA_CreateVariable(eVK_Longint);
    
    PA_SetLongintVariable(&args[4], (PA_long32)-1);

    PA_ExecuteCommandByID(443 /*GET WINDOW RECT*/, args, 5);

    return (
       (PA_GetLongintVariable(args[0]) == 0)
    && (PA_GetLongintVariable(args[1]) == 0)
    && (PA_GetLongintVariable(args[2]) == 0)
    && (PA_GetLongintVariable(args[3]) == 0));
    
    /* the variable belongs to 4D, no need to PA_ClearVariable (I think) */

#endif
    
    return false;
}

#if VERSIONWIN

static HWND getMDI() {
    
    wasMDI = true;

    PA_ulong32 version = PA_Get4DVersion();
    
    if (version >= 16)
        return (HWND)PA_GetMainWindowHWND();

    // Altura MAc2Win does not allow multiple instances of the same app
    // we can assume that the window class is the folder name of the application
    
    HWND mdi = NULL;
    wchar_t path[_MAX_PATH] = { 0 };
    wchar_t * applicationPath = wcscpy(path, (const wchar_t *)PA_GetApplicationFullPath().fString);
    
    //remove file name (4D.exe)
    PathRemoveFileSpec(path);
    //check instance as well, to be sure
    HINSTANCE h = (HINSTANCE)PA_Get4DHInstance();
    
    do {
        mdi = FindWindowEx(NULL, mdi, (LPCTSTR)path, NULL);
        if (mdi)
        {
            if (h == (HINSTANCE)GetWindowLongPtr(mdi, GWLP_HINSTANCE))
            {
                break;
            }
        }
    } while (mdi);
    
   return mdi;
}
#endif

#if VERSIONWIN

#include <tchar.h>

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_LENGTH 32767

static void get_env_from_reg(HKEY hKey) {

	DWORD Ret;

	TCHAR    achKey[MAX_KEY_LENGTH];   // buffer for subkey name
	DWORD    cbName;                   // size of name string 
	TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name 
	DWORD    cchClassName = MAX_PATH;  // size of class string 
	DWORD    cSubKeys = 0;               // number of subkeys 
	DWORD    cbMaxSubKey;              // longest subkey size 
	DWORD    cchMaxClass;              // longest class string 
	DWORD    cValues;              // number of values for key 
	DWORD    cchMaxValue;          // longest value name 
	DWORD    cbMaxValueData;       // longest value data 
	DWORD    cbSecurityDescriptor; // size of security descriptor 
	FILETIME ftLastWriteTime;      // last write time 

	Ret = RegQueryInfoKey(
		hKey,                    // key handle 
		achClass,                // buffer for class name 
		&cchClassName,           // size of class string 
		NULL,                    // reserved 
		&cSubKeys,               // number of subkeys 
		&cbMaxSubKey,            // longest subkey size 
		&cchMaxClass,            // longest class string 
		&cValues,                // number of values for this key 
		&cchMaxValue,            // longest value name 
		&cbMaxValueData,         // longest value data 
		&cbSecurityDescriptor,   // security descriptor 
		&ftLastWriteTime);       // last write time 

	DWORD	type;
	DWORD	_name_len;
	DWORD	value_len;

	std::vector<unsigned char>_name_buf(MAX_VALUE_LENGTH);
	std::vector<unsigned char>value_buf(MAX_VALUE_LENGTH);

	if (cValues) {
		for (DWORD i = 0; i<cValues; i++) {
			_name_len = MAX_VALUE_LENGTH;
			value_len = MAX_VALUE_LENGTH;
			Ret = RegEnumValue(hKey, i,
				(LPWSTR)&_name_buf[0],
				&_name_len,
				0,
				&type,
				(BYTE *)&value_buf[0],
				&value_len);
			if (Ret == ERROR_SUCCESS) {
				if ((type == REG_SZ) || (type == REG_EXPAND_SZ)) {
					std::wstring value = std::wstring((const wchar_t *)&value_buf[0], value_len);
					std::wstring _name = std::wstring((const wchar_t *)&_name_buf[0], _name_len);

					std::lock_guard<std::mutex> guard(gMutexEnvironment);

					_wputenv_s(_name.c_str(), value.c_str());
				}
			}
		}
	}
}

static void refresh_environ() {

	HKEY hKeyParent = HKEY_LOCAL_MACHINE;
	PWCHAR valueName = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
	DWORD Ret;
	HKEY hKey;

	Ret = RegOpenKeyEx(
		hKeyParent,
		valueName,
		0,
		KEY_READ | KEY_QUERY_VALUE | KEY_WOW64_64KEY,
		&hKey
	);

	if (Ret == ERROR_SUCCESS) {
		get_env_from_reg(hKey);
		RegCloseKey(hKey);
	}

	hKeyParent = HKEY_CURRENT_USER;
	valueName = L"Environment";

	Ret = RegOpenKeyEx(
		hKeyParent,
		valueName,
		0,
		KEY_READ | KEY_QUERY_VALUE | KEY_WOW64_64KEY,
		&hKey
	);

	if (Ret == ERROR_SUCCESS) {
		get_env_from_reg(hKey);
		RegCloseKey(hKey);
	}

}

LRESULT CALLBACK customWndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {

    switch (msg) {
        case WM_SETTINGCHANGE:
            if((std::wstring((const wchar_t *)lp)) == L"Environment") {
				refresh_environ();
            }
            break;
        default:
            break;
    }
    
    return CallWindowProc(originalWndProc, wnd, msg, wp, lp);
}
#endif

void OnStartup() {
    
#if VERSIONWIN
    HWND mdi = getMDI();
    if (mdi) {
        originalWndProc = (WNDPROC)GetWindowLongPtr(mdi, GWLP_WNDPROC);
        SetWindowLongPtr(mdi, GWLP_WNDPROC, (LONG_PTR)customWndProc);
    }
#endif
}

void OnExit() {

#if VERSIONWIN
    HWND mdi = getMDI();
    if (mdi) {
        SetWindowLongPtr(mdi, GWLP_WNDPROC, (LONG_PTR)originalWndProc);
    }
#endif
}

void PluginMain(PA_long32 selector, PA_PluginParameters params) {
    
	try
	{
        switch(selector)
        {
                
            case kInitPlugin :
            case kServerInitPlugin :
                if(!isSDI()) {
                    PA_RunInMainProcess((PA_RunInMainProcessProcPtr)OnStartup, NULL);
                }
                break;
                
            case kDeinitPlugin :
#if VERSIONWIN
                //too late to call isSDI() which depends on PA_ExecuteCommandByID
                if (wasMDI) {
                    PA_RunInMainProcess((PA_RunInMainProcessProcPtr)OnExit, NULL);
                }
#endif
                break;
                
			// --- Environment
            
			case 1 :
				Expand_environment_string(params);
				break;
			case 2 :
				Get_environment_variable(params);
				break;
			case 3 :
				PUT_ENVIRONMENT_VARIABLE(params);
				break;

        }

	}
	catch(...)
	{

	}
}

#pragma mark -

void Expand_environment_string(PA_PluginParameters params) {

    sLONG_PTR *pResult = (sLONG_PTR *)params->fResult;
    
    C_TEXT Param1;
    C_TEXT returnValue;

#if VERSIONWIN

    /*
     https://docs.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-expandenvironmentstringsw
     */
    
    PackagePtr pParams = (PackagePtr)params->fParameters;
    Param1.fromParamAtIndex(pParams, 1);
    
    DWORD len = ExpandEnvironmentStringsW((LPCWSTR)Param1.getUTF16StringPtr(), NULL, 0);
    std::vector<wchar_t>buf(++len);//The size of the lpSrc and lpDst buffers is limited to 32K
    len = ExpandEnvironmentStringsW((LPCWSTR)Param1.getUTF16StringPtr(), &buf[0], buf.size());
    if(len) {
        returnValue.setUTF16String((const PA_Unichar *)&buf[0], len);
    }
    
#endif
    
    returnValue.setReturn(pResult);
}

void Get_environment_variable(PA_PluginParameters params) {

    sLONG_PTR *pResult = (sLONG_PTR *)params->fResult;
    PackagePtr pParams = (PackagePtr)params->fParameters;
 
    C_TEXT Param1;
    C_TEXT returnValue;
 
    Param1.fromParamAtIndex(pParams, 1);
    
    if(Param1.getUTF16Length()) {
    
#if VERSIONWIN
    
    std::lock_guard<std::mutex> guard(gMutexEnvironment);
    
    size_t len = 0;
    wchar_t* buf = 0;

    if(_wdupenv_s(&buf, &len, (const wchar_t *)Param1.getUTF16StringPtr()) == 0) {
        if(buf) {
            returnValue.setUTF16String((const PA_Unichar *)buf, (uint32_t)len);
            free(buf);
        }
    }
    
#else
    
    CUTF8String name, value;
    Param1.copyUTF8String(&name);
    
    value = CUTF8String((const uint8_t *)getenv((const char *)name.c_str()));
    returnValue.setUTF8String(&value);
    
#endif
        
    }
    
    returnValue.setReturn(pResult);
}

void PUT_ENVIRONMENT_VARIABLE(PA_PluginParameters params) {

    PackagePtr pParams = (PackagePtr)params->fParameters;
    
    C_TEXT Param1;
    C_TEXT Param2;

    Param1.fromParamAtIndex(pParams, 1);
    Param2.fromParamAtIndex(pParams, 2);
    
    if(Param1.getUTF16Length()) {
        
#if VERSIONWIN
    
    std::lock_guard<std::mutex> guard(gMutexEnvironment);

    _wputenv_s((const wchar_t *)Param1.getUTF16StringPtr(), (const wchar_t *)Param2.getUTF16StringPtr());
    
#else
    
    CUTF8String name, value;
    Param1.copyUTF8String(&name);
    Param2.copyUTF8String(&value);
    
    setenv((const char *)name.c_str(), (const char *)value.c_str(), 1);
    
#endif
        
    }
}

