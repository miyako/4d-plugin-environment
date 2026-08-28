# 4d-plugin-environment

This plugin exposes the process environment block to 4D code.

**Windows:**
- Reading uses `_wdupenv_s` / `_wgetenv` (CRT block), not `GetEnvironmentVariableW`. This means changes are visible to CRT and to child processes launched from 4D.
- Writing uses `_wputenv_s`, not `SetEnvironmentVariableW`.
- The plugin listens for `WM_SETTINGCHANGE` with `lParam="Environment"`. When Windows broadcasts that the user edited environment variables in Control Panel, it re-reads:
  - `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment`
  - `HKCU\Environment`
  - `HKCU\Volatile Environment`
  - `HKCU\Volatile Environment\{session id}` (subkeys)
- In MDI mode, the plugin subclasses the MDI main window at `kInitPlugin` automatically.
- In SDI mode, MDI window does not exist, so you must register your own window with `REGISTER ENVIRONMENT WINDOW`.

**macOS / Linux:**
- Reading uses `getenv`, writing uses `setenv`. These are process-local and not persisted.
- `Expand environment string` and `REGISTER ENVIRONMENT WINDOW` are Windows-only and return empty / do nothing on macOS.

All four commands are declared `threadSafe:true` in manifest.json.

---

## Installation

1. Put `Environment.bundle` (mac) or `Environment.dll` (win) in `Plugins/` folder.
2. Restart 4D.
3. Commands appear in theme `environment`.

No entitlements needed.

---

## Commands

### 1. Expand environment string

**Syntax:** `expanded := Expand environment string (source : Text) : Text`

**Description:** Windows only. Expands `%VAR%` references inside a string using `ExpandEnvironmentStringsW`.

The size is limited to 32K per Windows API.

- `%SystemRoot%` → `C:\Windows`
- `%userprofile%\%appdata%` → `C:\Users\you\AppData\Roaming`
- If variable does not exist, it stays unexpanded.

On macOS, returns empty string.

**Parameters:**
- `source` (IN) - Text containing `%NAME%` placeholders
- Returns - Expanded text

**Example:**
```4d
$path:=Expand environment string("%SystemRoot%\\System32")
  // $path = "C:\\Windows\\System32"

$user:=Expand environment string("%userprofile%")
$desktop:=Expand environment string("%userprofile%\\Desktop")
```

---

### 2. Get environment variable

**Syntax:** `value := Get environment variable (name : Text) : Text`

**Description:** Returns the current value of an environment variable from the current 4D process.

- Windows: uses `_wdupenv_s`, thread-safe with internal mutex after fix. Case-insensitive on Windows (`PATH` == `Path` == `path`), but use canonical uppercase.
- macOS: uses `getenv`, now protected by mutex after fix. Case-sensitive.

Returns empty string if variable does not exist.

Does **not** read directly from registry. It reads from the process block. Use `REGISTER ENVIRONMENT WINDOW` to keep that block in sync after system changes.

**Parameters:**
- `name` (IN) - Variable name, e.g. `"PATH"`, `"TEMP"`, `"BAZEL_VC"`
- Returns - Variable value

**Example:**
```4d
$path:=Get environment variable("PATH")
$temp:=Get environment variable("TEMP")
If ($temp="")
  $temp:=Get environment variable("TMP")
End if

$bazel:=Get environment variable("BAZEL_VC")
```

---

### 3. PUT ENVIRONMENT VARIABLE

**Syntax:** `PUT ENVIRONMENT VARIABLE (name : Text; value : Text)`

**Description:** Sets or creates an environment variable in the current 4D process.

- Windows: `_wputenv_s(name, value)`. Visible to current process and child processes spawned after the call. If `value` is empty string, variable is deleted from block.
- macOS: `setenv(name, value, 1)`. Same semantics.

**Does NOT persist** to registry / system. If you want system-wide persistence, you must write to registry yourself or use external tool. The plugin's job is the opposite: when system registry *does* change, it pulls those changes into the process (via `WM_SETTINGCHANGE`).

Name must not contain `=` and must not be empty. After safe-fix, invalid names are ignored instead of crashing CRT.

**Parameters:**
- `name` (IN) - Variable name
- `value` (IN) - New value. Empty to delete.

**Example:**
```4d
// Set for child processes
PUT ENVIRONMENT VARIABLE("MY_APP_DATA"; $myFolder)

// Create temporary test var
PUT ENVIRONMENT VARIABLE("TEST"; Generate UUID)
$test:=Get environment variable("TEST")

// Delete
PUT ENVIRONMENT VARIABLE("TEST"; "")

// Example: prepend to PATH for current session
$currentPath:=Get environment variable("PATH")
PUT ENVIRONMENT VARIABLE("PATH"; $myToolsFolder+";"+$currentPath)
```

**Tip - launching external process with custom env:**
```4d
PUT ENVIRONMENT VARIABLE("MY_TOOL_HOME"; $toolPath)
LAUNCH EXTERNAL PROCESS($cmd; $in; $out; $err)  // child inherits env
```

---

### 4. REGISTER ENVIRONMENT WINDOW

**Syntax:** `REGISTER ENVIRONMENT WINDOW (windowRef : Longint)`

**Platform:** Windows SDI only. No-op on macOS.

**Description:** Registers a 4D form window to receive `WM_SETTINGCHANGE` and refresh the CRT environment block from registry.

Why needed?
- In MDI mode (4D pre-v18 default), plugin subclasses the main MDI window automatically on startup. No action needed.
- In SDI mode (default since v18), there is no MDI window. `isSDI()` returns true (checks `GET WINDOW RECT` returns 0,0,0,0). Plugin does **not** subclass anything at startup. You must call this for each SDI window that should stay in sync.

What it does internally after fix:
- Restores previous SDI window proc (if any) safely under lock
- Subclasses `windowRef` with `customWndProc_sdi`
- On `WM_SETTINGCHANGE` where `lParam="Environment"` -> `refresh_environ()` re-reads the three registry locations
- On `WM_CLOSE` and `WM_NCDESTROY` -> automatically unsubclasses

Call once per window after `Open form window`.

**Parameters:**
- `windowRef` (IN) - Longint window reference returned by `Open form window` or `Open window`

**Example - TEST_SDI.4dm:**
```4d
//%attributes = {}
$w:=Open form window("TEST"; Movable form dialog box)
REGISTER ENVIRONMENT WINDOW($w)
DIALOG("TEST")
// On dialog close, plugin auto-unsubclasses via WM_CLOSE
```

**Full SDI usage pattern:**
```4d
$win:=Open form window("MyForm"; Movable form dialog box)
REGISTER ENVIRONMENT WINDOW($win)

$msg:="Current PATH: "+Get environment variable("PATH")
OBJECT SET TITLE(*; "info@"; $msg)

DIALOG("MyForm")

// After user changes env in System Properties -> Control Panel,
// without closing 4D, the next Get environment variable will return new value
// because WM_SETTINGCHANGE was caught.
```

If you open multiple SDI windows, only last registered window is active (plugin keeps single `gSDI`). Re-registering automatically unhooks previous. If you need all windows to stay synced, register each time you open one, or register a hidden utility window that stays open for app lifetime.

---

## Complete Example

```4d
// Expand
$system:=Expand environment string("%SystemRoot%")
ALERT($system)

// Get
$path:=Get environment variable("PATH")
$tmp:=Get environment variable("TEMP")

// Put
PUT ENVIRONMENT VARIABLE("FOO"; "BAR")
ALERT(Get environment variable("FOO"))  // BAR
PUT ENVIRONMENT VARIABLE("FOO"; "")     // delete

// SDI sync
If (Is Windows)  // your own method
  $w:=Open form window("Main"; Movable form dialog box)
  REGISTER ENVIRONMENT WINDOW($w)
  DIALOG("Main")
End if
```

---

## Behavior Matrix

| Command | Windows MDI | Windows SDI | macOS |
|---|---|---|---|
| Expand environment string | Expands %VAR% | Expands %VAR% | Returns "" |
| Get environment variable | CRT block, synced via WM_SETTINGCHANGE | CRT block, synced only if window registered | getenv |
| PUT ENVIRONMENT VARIABLE | Process only, not persistent | Process only | Process only |
| REGISTER ENVIRONMENT WINDOW | Auto at startup, manual optional | Required for sync | No-op |

---

## Notes & Best Practices

1. **Not persistent:** `PUT ENVIRONMENT VARIABLE` does not write to registry. Restarting 4D loses changes. For persistence, write to `HKCU\Environment` yourself via registry plugin, then broadcast `WM_SETTINGCHANGE`.

2. **Child processes:** Environment is inherited at spawn time. Set variables *before* `LAUNCH EXTERNAL PROCESS`.

3. **Case sensitivity:** On Windows env names are case-insensitive, on macOS case-sensitive. Normalize to upper case.

4. **PATH manipulation:** Always read current, modify, then write back. Remember 4D's own PATH is needed for plugins.

5. **Thread safety after fix:** All commands now use `gMutexEnvironment` on both platforms. Still, avoid heavy concurrent `PUT`/`GET` loops.

6. **32K limit:** `ExpandEnvironmentStringsW` and registry values are limited to 32767 chars. Larger values are truncated.

7. **Security:** Do not pass user-supplied names with `=` or control chars. After fix they are rejected, but validate anyway.

---

## What was fixed in this safe-fix (2026-08-28)

- `getMDI()`: `wcscpy` unbounded copy → `wcsncpy_s` with `PA_GetApplicationFullPath().fLength` check, `FindWindowExW`
- `WM_SETTINGCHANGE`: NULL `lParam` check (`wcscmp` instead of `std::wstring(nullptr)`)
- Registry buffers: `vector<unsigned char>` → `vector<wchar_t>`, correct bytes-to-chars conversion, trim at null, skip names with `=`
- `isSDI()`: Added `PA_ClearVariable` for 5 args
- `ExpandEnvironmentString`: empty check, null check, correct required-size handling
- `Get env var`: `len-1` / `wcslen` fix, mutex on macOS, null check
- `Put env var`: reject `=` in name, mutex on macOS, null checks
- `REGISTER ENVIRONMENT WINDOW`: whole operation under `gMutexSDI`, safe `OnExit_sdi_locked`, `SetWindowLongPtrW`/`CallWindowProcW`, handle `WM_NCDESTROY`