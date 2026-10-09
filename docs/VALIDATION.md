# v0.1.1 validation

Date: 2026-10-09. Native unsigned Windows x64 build.

- CMake 3.31.6 / LLVM-MinGW Clang 21.1.8 Release build and all three CTest suites passed after the reliability changes.
- Regression tests cover transient observations, consecutive evidence, manual baseline/topology changes, deferred target availability, bounded mode settling, monitor-first recovery and rejection of empty recovery profiles.
- Native isolated offline-TV fixture: automation starts with a verified monitor even when the TV connector and endpoint are unavailable. Opening settings keeps automation enabled.
- Native hotkey-conflict fixture: deliberately occupying Ctrl+Alt+F12 leaves F10/F11 registered and automation active. Refusing TV selection while it is unavailable preserves automation.
- Native startup fixture: Task Scheduler launches the real portable EXE, including the offline-TV case. A deliberately locked configuration file makes startup-on fail and rolls registration back rather than leaking an enabled task.
- Actual installed EXE: all configured shortcuts register, Task Scheduler launch probe succeeds, hidden per-user logon task is enabled and a scheduler invocation is delivered successfully. Actual reboot/logon is not yet tested.
- Actual RTX 3070 display profiles validate: Redmi 3840x2160/160 Hz, Samsung QE65QN85BAUXCE 3840x2160/120 Hz. A TV-only native trial applies and verifies, then its independent 30-second guardian restores the prior desktop containing the monitor. Automatic powered-on-TV selection applies and verifies.
- A fresh independent source review identified command/pause/queue/guardian races; the reviewed corrections preserve safety pauses and invalidate pre-manual observations.

Actual sleep/resume, reboot/logon, hard driver hangs, cable removal and game/HDR/VRR behavior remain physical-testing limitations. Automated tests and Windows profile verification do not prove that a physical screen shows a picture. New setups require human picture confirmation.

The v0.1.0 startup failure was traced to the installation path being inaccessible outside the packaged host; blaming Task Scheduler itself was incorrect. v0.1.1 uses a real portable installation and proves launch through the scheduler before enabling startup.

Two missed network replies deliberately mean off; this is inferred power state, not HDMI power telemetry. Incomplete responses and transient server errors instead keep polling without choosing a screen. A permanently hung driver cannot be repaired by this user-mode utility; the independent guard waits for the display lock rather than silently abandoning recovery.

Old binaries, prototype and private configuration remain local backups, excluded from source/releases.
