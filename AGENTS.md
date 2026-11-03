# VelaRide development constraints

The user's required order is recorded in `docs/BASELINE_RESTORE_20261026.md`:
restore and validate the existing watch first, integrate microphone support
second, then add the new Bluetooth/mobile-app integration.

Before extending an accepted version, preserve its firmware, configuration,
source snapshot/patches and hashes. Keep feature edits scoped. Shared startup,
touch, display, wake, pinmux, I2C, DMA and power changes require a concrete
reason and targeted regression checks. Keep unrelated edits when syncing trees.

Do not substitute degraded operation for an already accepted feature. A lit
screen does not establish working touch/gestures/wake; a build or protocol ACK
does not establish the corresponding physical behavior. Record exactly what
was tested, fix regressions before advancing stages, and keep audio quiet
unless a bounded listening test is requested or part of the agreed validation.

These rules add no permission ceremony. Continue authorized diagnostics,
builds, repairs and restorations. Ask only for missing information or physical
actions/observations unavailable through tools.

If this repository has `.codegraph/`, use CodeGraph before ordinary code search.
Otherwise skip it; do not create an index without the user's request.
