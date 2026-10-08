# ScreenRelay: instructions for coding agents

## Intent

Keep one desktop on the screen the person is using: a desk monitor or a Samsung TV. This is a tiny hidden Windows utility, not a display-management suite. Optimize for predictable behavior, small resource use and recoverable changes.

Do not add a tray icon, cloud service, screenshot capture, telemetry, updater, macros, a generic rules engine or multi-display editor unless the user explicitly asks. Settings should remain a short setup screen.

## Current contract

- Windows 11 x64, native C++20/Win32/WinHTTP. No PowerShell or .NET process in normal operation.
- Two configured displays; three profiles: monitor, TV, both.
- Poll the verified Samsung endpoint every 500 ms; do not overlap requests. Whole-response deadline: 200 ms. Disable redirects and proxies. Limit response bodies to 64 KiB.
- Two consecutive verified `on` replies select TV. Explicit `off` or two consecutive network failures select monitor. A successful response clears failures. Wrong identity, malformed JSON, unexpected power state or HTTP error pauses automation.
- There is no added switching delay. Do not repeat `SetDisplayConfig` on every poll.
- Manual selection keeps polling active and holds the selected profile until the next confirmed TV power change. Unknown initial state must first establish a baseline without switching. Topology changes must not clear this hold. `--auto` explicitly cancels it. Explicit pause, settings and safety errors still suspend automation.

## Display invariants

1. Resolve current GPU LUID, target and source IDs from Windows before each application. Never persist DISPLAY1/DISPLAY2 or assume they are stable across boots.
2. Bind selected hardware to GPU device path, output technology, physical connector and manufacturer. Product codes can change during a valid Samsung transition (observed SAM7202/SAM7203). Do not silently accept ambiguous targets.
3. Preserve exact captured modes, including rational refresh rates: 120 is not automatically interchangeable with 120000/1001.
4. Validate before applying; omit SDC_ALLOW_CHANGES and do not write temporary topology to the Windows display database.
5. Verify identity, active count, resolution, refresh, rotation and desktop positions after applying. Single-display origin is 0,0.
6. Keep independent recovery armed until verification succeeds. Never remove it to hide a failing test.
7. Missing hardware, uncertain identity, invalid settings or failed verification must not trigger a retry loop that keeps flashing screens.
8. First-time profile setup requires real picture confirmation for both single-display modes; a successful API call is not physical confirmation.

Do not change drivers, TDR registry values, EDID overrides, HDR/VRR or router settings as part of an ordinary fix. Explain the specific evidence before proposing those changes.

## Code map

- `src/controller.hpp`: pure decision logic; no network, UI or display writes.
- `src/network.hpp`: bounded LAN HTTP adapter and strict identity parsing.
- `src/display.hpp`: capture, serialization, live rebinding, validation and application.
- `src/app.cpp`: small settings UI, hidden host, hotkeys, startup and recovery process.

The JSON dependency is pinned in `vendor/`; keep its license. Avoid extra runtimes and frameworks for the minimal settings screen.

## Changing behavior safely

Read both READMEs and architecture notes first. Reproduce the relevant failure and add a behavior test before changing logic. Keep changes limited to the requested scenario. Existing user settings belong to LocalAppData, not the repository.

Run the complete CMake/CTest suite, then `--diagnose` for non-mutating Windows validation. Exercise actual switching only with a confirmed visible target and recovery active. Obtain human confirmation for a new display setup.

Test power on/off, one lost reply, two failures, identity mismatch, corrupt profiles, unplugged hardware, manual override, repeated commands, sleep/resume and restarted GPU IDs when relevant. Do not claim physical sleep/reboot or game tests happened unless they actually did.

Measure working-set/private memory, CPU and actual timing. Do not replace measurements with guesses or make an untested compatibility claim. Distinguish network detection time from Windows switching time.

## Privacy and release

Never commit user IP/MAC addresses, model UUIDs, serial numbers, profiles, logs, credentials, screenshots or home-directory paths. Samples use fictitious private addresses and identities. Scan tracked files before publication. Credentials must not be written into tools, command arguments or documentation.

Release only after a fresh independent review and passing tests. Package the EXE, instructions and licenses; publish checksums. Explain unsupported cases and unsigned binaries honestly. Keep the old controller as a local fallback during migration, but never run both controllers together.

## Future ideas

Additional TV adapters, more displays, user-defined profiles and macros are possible extensions. They are not part of v0.1.0. Add one narrowly specified capability at a time; keep the existing small setup and recovery path usable.
