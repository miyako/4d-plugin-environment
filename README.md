# 4d-plugin-environment
Get, set, expand (windows only) environment variables.

#### Set

* Using [``_wputenv_s``](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/putenv-s-wputenv-s?view=vs-2019), not [``SetEnvironmentVariable``](https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setenvironmentvariable) on Windows.
