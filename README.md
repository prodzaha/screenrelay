# ScreenRelay

**Your desktop, on the screen you're using.**

A small Windows app that switches between your desk monitor and a Samsung TV. Turn the TV on: only the TV is active. Turn it off: only the monitor is active. Your apps stay open and your saved refresh rates are restored.

[Русская инструкция](README.ru.md) · [Architecture and adaptation](docs/ARCHITECTURE.md) · [Rules for coding agents](AGENTS.md)

## The short version

- Native Windows x64 executable. No PowerShell, .NET installation, tray icon or permanent window.
- Local Samsung API only. No cloud, account, TV remote pairing, telemetry or automatic updates.
- One request every **500 ms**, one in flight at a time, **200 ms total response deadline**.
- Two verified `on` responses select the TV. Explicit `off` or two consecutive missed responses select the monitor.
- A wrong TV identity pauses automation. Incomplete replies, unknown power states and HTTP 408/429/5xx keep polling without switching. Verified `standby` is treated as off.
- Windows validates the exact saved modes before switching; a separate recovery process protects each change.

## Set up in a minute

1. Download the Windows x64 ZIP from [Releases](https://github.com/prodzaha/screenrelay/releases). Unpack it into a permanent folder and run `ScreenRelay.exe --settings`.
2. Turn on your Samsung TV. Enter its local IPv4 address and click **Test TV**. Reserve this address in your router's DHCP settings if possible.
3. In Windows, use **Win+P → Extend**. Set each screen's preferred resolution and refresh rate.
4. Select the desk monitor and TV display. Click **Capture current Extended profiles**, then test and confirm each single-display picture. Unconfirmed trials return to the previous desktop after 30 seconds.
5. Choose startup and hotkeys, then **Save and start automation**.

The address must be a private LAN IPv4 address. A compatible Samsung local API must answer on port 8001. Not every Samsung model supports this API or reports power the same way.

## Daily use

| Shortcut | Action |
|---|---|
| Ctrl+Alt+F10 | Both screens |
| Ctrl+Alt+F11 | Desk monitor only |
| Ctrl+Alt+F9 | TV only, after confirming the TV is on |

A manual selection keeps polling enabled and holds the chosen display profile until the next confirmed TV power-state change. If the initial network state is unknown, the first stable state establishes a baseline without undoing the manual choice. Run `ScreenRelay.exe --auto` to cancel this hold and follow the current TV state immediately. Opening settings keeps polling active. Timed picture trials temporarily block automation and other switching commands. Hotkeys can be changed in settings; existing registrations are not stolen. A blocked shortcut leaves the other keys and automation enabled. Windows reserves F12 for debugging, so choose another function key if registration fails on your PC.

Settings are intentionally small: TV address, the two displays, three captured profiles, startup and function keys. Select Both with the configured shortcut before changing resolutions/frequencies in Windows and recapturing profiles. Arbitrary macros, an advanced rule editor and more than two configured displays are **not implemented**.

## What “off” means

Some Samsung TVs remain connected over HDMI while powered off. ScreenRelay reads their network API instead. If the TV stops answering twice, it treats that as off. A Wi-Fi outage can therefore switch back to the monitor. Internet availability does not matter.

Response and switching time are separate. Expect roughly **1–2 seconds** once the TV API is reachable; a TV's network wake-up and the graphics driver can add time. ScreenRelay does not promise uninterrupted exclusive-fullscreen games or preserve an app's internal display choice. Borderless windowed mode often works best.

## Safety and local files

- Exact modes are validated and checked after application. Silent refresh-rate substitution is not allowed.
- GPU IDs are rebound from the live Windows topology after startup and sleep. Missing devices await a topology or power-state change; ambiguous identity pauses switching.
- A separate temporary process restores a safe profile after 10 seconds without acknowledgement. The verified monitor takes priority over a TV-only previous topology. Windows has up to 1500 ms to settle before verification fails.
- If a profile cannot be applied or verified, automation pauses and records the reason. Open settings to inspect it.
- Drivers, EDID overrides, TDR, HDR/VRR and router settings are not modified.

Local settings, status and bounded logs live under `%LOCALAPPDATA%\ScreenRelay`. With `portable.flag` beside the EXE, files live in its `data/` subfolder instead. The release ZIP includes this flag: keep it in a permanent writable folder. Private data must never be published. `--diagnose` writes a report without addresses, UUIDs or GPU paths. Startup uses a hidden per-user Task Scheduler logon task without elevation. Enabling it requires a successful launch probe through Task Scheduler. Disable it in settings or with `--startup-off`.

## Commands

`--settings`, `--auto`, `--monitor`, `--tv`, `--both`, `--pause`, `--exit`, `--status`, `--diagnose`, `--startup-on`, `--startup-off`.

`--status` updates the local `status.json`. `--diagnose` validates profiles without changing displays. Advanced migration and timed trial commands are documented in [architecture notes](docs/ARCHITECTURE.md).

## Build and test

Windows, CMake 3.20+, and MSVC with a Windows SDK:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

LLVM-MinGW is also supported. JSON for Modern C++ 3.12.0 is vendored with its MIT license; no dependency downloads are needed during a normal build. GitHub Actions runs the build and tests.

## Compatibility and resources

The original workflow was physically verified on Windows 11, RTX 3070, Redmi G27U at 3840×2160/160 Hz and Samsung QE65QN85BAUXCE at 3840×2160/120 Hz. Native release validation results and untested scenarios are recorded in [VALIDATION.md](docs/VALIDATION.md).

The design target is below 20 MiB working-set memory and below 0.1% average total CPU while idle. These are measurement targets, not guarantees. Polling pauses naturally while the PC sleeps; no continuous screenshot capture or GPU workload is used.

## Remove

Run `ScreenRelay.exe --startup-off`, then `--both`, then `--exit`. Delete the application folder and, optionally, `%LOCALAPPDATA%\ScreenRelay`. Display drivers and Windows' saved display database remain untouched.

## Later, if useful

Possible future work: more displays, custom profiles/rules, macros and additional TV adapters. These are ideas, not current features. See [AGENTS.md](AGENTS.md) before extending behavior.

MIT licensed. Windows and Samsung are trademarks of their respective owners; this project is independent.

A hung graphics driver cannot be repaired by this utility. Recovery keeps waiting for the display lock; Windows driver calls themselves are not cancellable.
