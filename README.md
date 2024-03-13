Get, set, expand (windows only) environment variables.

#### Windows

* Using [``_wputenv_s``](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/putenv-s-wputenv-s?view=vs-2019), not [``SetEnvironmentVariable``](https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setenvironmentvariable) to write. 

* Using [``_wgetenv``](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/getenv-wgetenv?view=vs-2019), not [``GetEnvironmentVariable``](https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getenvironmentvariable) to read.

* Catch ``WM_SETTINGCHANGE`` window message and read values from the registry.

  * ``HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment``
  * ``HKCU\Environment``
  * ``HKCU\Volatile Environment``
  * ``HKCU\Volatile Environment\{number}``
  
[miyako.github.io](https://miyako.github.io/2020/06/26/4d-environment.html)
