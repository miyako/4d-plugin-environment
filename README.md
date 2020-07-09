# 4d-plugin-environment
Get, set, expand (windows only) environment variables.

### Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
||<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|

### Version

<img width="32" height="32" src="https://user-images.githubusercontent.com/1725068/73986501-15964580-4981-11ea-9ac1-73c5cee50aae.png"> <img src="https://user-images.githubusercontent.com/1725068/73987971-db2ea780-4984-11ea-8ada-e25fb9c3cf4e.png" width="32" height="32" />

#### Windows

* Using [``_wputenv_s``](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/putenv-s-wputenv-s?view=vs-2019), not [``SetEnvironmentVariable``](https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setenvironmentvariable) to write. 

* Using [``_wgetenv``](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/getenv-wgetenv?view=vs-2019), not [``GetEnvironmentVariable``](https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getenvironmentvariable) to read.

* Catch ``WM_SETTINGCHANGE`` window message and read values from the registry.

  * ``HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment``
  * ``HKCU\Environment``

[miyako.github.io](https://miyako.github.io/2020/06/26/4d-environment.html)
