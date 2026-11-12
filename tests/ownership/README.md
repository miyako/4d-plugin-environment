# Ownership regressions

A synthetic 4D engine (`engine_harness.cpp`) replaces the plugin-API callback (`gCall4D`). It links the real plugin sources against the bundled, unmodified `4D Plugin API/4DPluginAPI.c`:

- `4DPlugin-Environment.cpp`
- `4D Plugin API/Classes/C_TEXT.cpp`
- `support/4DPlugin-JSON.cpp`, which is **dormant/unbuilt**: no Xcode or Visual Studio project compiles it, but it ships as a template.

The engine tracks every engine-owned string, picture, object/collection and handle, and the harness runs under ASan/UBSan.

```sh
tests/ownership/run.sh . fixed        # every test must pass
D=$(mktemp -d); git archive 8fb5d1d | tar -x -C "$D"
tests/ownership/run.sh "$D" baseline  # named defect_*/guard_* tests must fail, control_* must pass
```

Contracts enforced:

- `PA_CreateVariable(eVK_Unistring/eVK_Picture)` allocates an empty value, and a following shallow `PA_SetStringVariable`/`PA_SetPictureVariable` leaks it. Create the variable with `eVK_Undefined` and set it once.
- `PA_GetObjectProperty` returns an owned value: a deep copy, or a retained object/collection reference. Read `PA_GetLastError()` first. On `eER_NoErr`, call `PA_ClearVariable` on it whatever its kind. A collection that `ob_get_c` returns is borrowed from the parent object.
- `PA_SetObjectProperty` deep-copies or retains, so the caller still clears its own variable.

The commands (`Get environment variable`, `PUT ENVIRONMENT VARIABLE`, `Expand environment string`, `REGISTER ENVIRONMENT WINDOW`) and init/deinit run as controls through the real `PluginMain`.

Limits:

- macOS only. The Windows-only code (registry refresh, window subclassing, `getMDI`/`PA_GetApplicationFullPath`, `isSDI`) is not exercised.
- Engine semantics are modelled, not taken from a real 4D engine.
