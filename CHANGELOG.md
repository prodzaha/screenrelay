# Changelog

## 0.1.1

- Manual display selections keep polling enabled and last until the next confirmed TV power-state change. Unknown initial state establishes a baseline first; topology updates preserve the override.
- Optional `portable.flag` stores private settings in `data/` beside the EXE, avoiding packaged-host AppData redirection during local installation.

- Launch-verified per-user Task Scheduler startup, with rollback on failed settings writes.
- Offline-TV startup, transient replies and opening settings keep automation active.
- Working shortcuts survive conflicts; rejecting an unavailable TV keeps polling active.
- Bounded settling checks, verified monitor recovery and empty-topology rejection.
- Observation epochs isolate manual/setup changes; setup confirmations block nested switches.

## 0.1.0

- Native hidden Windows x64 controller with a small settings window.
- Local Samsung power-state polling with two-sample switching logic.
- Exact monitor/TV/Extended profiles, live GPU rebinding and independent recovery.
- Configurable Ctrl+Alt function keys and current-user autostart.
- Bounded local logs, private configuration and sanitized diagnostic output.
- English/Russian instructions and coding-agent rules.

First release: support is limited to one desk monitor and one compatible Samsung TV. No generic rules, macros, updater or telemetry.
