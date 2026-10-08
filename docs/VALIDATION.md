# v0.1.0 validation

Validation date: 2026-10-09. This is an unsigned, native Windows x64 build.

- CMake 3.31.6 / LLVM-MinGW Clang 21.1.8: clean Release build passed.
- CTest: all three suites passed (controller/ordered queue, strict network parser, connector identity and exact profile consistency).
- Controlled local HTTP fixture: five deliberately delayed responses cancelled in 203–219 ms including call overhead, then 100 valid responses at 500 ms cadence passed. This checks actual callback cancellation, not only JSON parsing.
- Physical Windows 11 / RTX 3070 setup: Redmi 3840×2160 at 160 Hz and Samsung QE65QN85BAUXCE 3840×2160 at 120 Hz. All three exact profiles validated without changing modes.
- Native TV-only trial was visually confirmed; independent 30-second recovery restored both screens.
- Native monitor-only trial applied and verified. Automatic selection of the powered-on TV applied and verified.
- Per-user Windows Run entry replaced a Task Scheduler setup that could not launch on the test machine. Direct startup command verified; actual reboot/logon is untested.
- 30-second idle sample with 500 ms polling: 18.86 MiB working set, 18.89 MiB peak, 0.094 CPU seconds, 0.026% average total CPU on the tested PC. Short measurements do not guarantee long-term behavior or other PCs' resource use.

Sleep/resume, reboot/logon, disconnected cables, game/HDR/VRR behavior, long-duration polling and driver-reset recovery require further physical testing. Automated tests do not replace these checks. A missing network response is deliberately treated as an off TV after two misses; this is inferred power state, not HDMI power telemetry.

The old controller and user settings are retained locally as a fallback, excluded from the public repository. The app and its dependency licenses are included in release packages.
