# C++ and UI refactor status

Branch: `refactor/cpp-ui-architecture`. Updated 13 September 2026.

This is an incremental implementation record, not a declaration that the master
refactor is complete. The general-UI continuation started at `806686d`, then
incorporated the newer interface and test-workflow changes through `0b71a9f`.
Existing commits are preserved; no shared history is rewritten.

## Completed and retained work

### Source-selected IMU adapter (`806686d`)

The QMI8658-specific `Board::Imu` implementation lives in
`src/drivers/imu/qmi8658/BoardImu.cpp`, selected by the existing driver source
filters. The unconditional board implementation, `ImuBinding.h` and six per-board
factory definitions were deleted. Device state is private and constant-initialized
without peripheral I/O. LCD 3.49 and AMOLED 2.41 retain Wire1; the other supported
targets retain Wire. The driver sequence, scaling, address fallback and read policy
are unchanged by this UI continuation.

The no-IMU host fixture establishes that the public header links without a
provider; it does not establish a complete no-IMU firmware target. A future device
must supply only its selected implementation, not a dummy QMI8658 binding.

### Shared interface/display settings (`88af8e7`)

The shared InterfaceSettingsScreen owns editing behavior; regular/watch sources
provide geometry through InterfaceSettingsLayout. Regular inline values/sliders
and watch cards/steppers remain different presentations. Theme/locale no-op checks,
live brightness, screensaver order, Back and hidden-value work are retained.

### General UI inputs and geometry (current continuation)

Removed `SettingsControls`, `Choices`, `SettingsChoices` and `SettingsMenu` rather
than retaining a parallel settings framework. `ui::select` accepts ordinary
forward ranges and label/value projections; callers do not manufacture option
objects. `ui::valueButton` handles labelled values and lazy synchronous suppliers;
`ui::number` selects a slider or stepper. Rotary composition reports actual changes
and keeps side buttons beside the dial on narrow widths. No form object, registry,
heap widget tree or new Context member state is introduced.

Geometry is independent of graphics/application headers. Grid spans and row
measurement use the same packing rule. Application menu entries and value labels
remain ordinary data in the corresponding screen code. Both reading/settings
presentations and the newer shared interface implementation use these primitives.
The separate regular/watch layouts are not merged into a conditional renderer.

See `src/ui/README.md` for API examples unrelated to RSVP settings and the remaining
renderer boundaries. Selection still uses the existing immediate control's
paint-before-edit behavior; this is not a completed reconciliation/damage engine.

## Validation actually performed

This UI continuation compiled and executed `test/native_ui_inputs/run.py` and
`test/native_interface/run.py` using GCC 14.2, Clang 17 and GCC ASan/UBSan, with
C++23, warnings-as-errors, no exceptions and no RTTI. Both suites passed.

| Suite | Verified scope |
|---|---|
| `test/native_ui_inputs/run.py` | Real geometry, input/Context headers, settings model and bounded types; range projections, sparse/missing/empty values, lazy temporary-string lifetime, hidden work, numeric limits, spans and actual regular/watch settings screen sources with recording rendering primitives |
| `test/native_interface/run.py` | Existing shared interface behavior and both real layouts with a recording Context/catalog fixture; live brightness, no-op edits, locale/screensaver/standby cycling, Back, hidden lookups and field bounds |
| `test/native_imu/run.py` | Existing driver/adapter tests retained in CI; not rerun in this UI continuation. Earlier validation recorded GCC/Clang/sanitizer runs for register traces, decoding, fallback, failure, timeout, source selection and no-IMU link fixtures |

A control-record comparison against `806686d` covered ten sizes, two screens and
controlled page windows: 300 cases per presentation. Regular records matched;
watch differences were limited to side-button bounds for the narrow portrait
WPM dial. Context/Grid sizes matched in that host fixture. This is not pixel
comparison or a target memory/latency benchmark. Interface tests additionally
retain the nine watch sizes and regular 640x172 coverage from the preceding work.

Run from the repository root:

```sh
python test/native_imu/run.py
python test/native_ui_inputs/run.py
python test/native_interface/run.py
```

Runners accept `CXX` and `CXXFLAGS`. The Tests workflow now invokes the generic UI
suite in place of the removed choices suite and retains IMU/interface tests and
its existing triggers. CI wiring is not evidence of a completed Actions run.

## Performance and hardware boundary

No RSVP/page-reader, shaping, prefetch, paint backend, display driver, touch
acquisition, ISR, sleep or PMU implementation is changed by this UI continuation.
The existing source-selected IMU fix is preserved. General tests link recording
primitives, not a physical panel; interface tests use a separate recording Context.

No full PlatformIO suite, firmware target matrix, device test, RAM/flash comparison
or 1,000-WPM hardware re-benchmark was run here. The local workspace contains the
exact sources used by these host tests, not a complete firmware toolchain.
Unchanged hot-path source is not proof of unchanged device timing.

## Remaining master-plan work

| Workstream | Status |
|---|---|
| A: complete inventory, baseline and resource budgets | Pending beyond the focused sources and host comparisons |
| B/H1: domain ownership, accepted settings and transport-independent operations | Pending; App/runtime effects are not yet centralized |
| C: board/device composition | QMI8658 ownership/source selection has host coverage; PMU, touch, SDMMC, audio and broader lifecycle extraction remain |
| D: concurrency/resource lifetimes | Broader access map, handover and failure validation remain |
| E: UI identity, capture, damage and payload ownership | Pending; slot/hash engine has not been replaced or declared correct |
| F: UI authoring/all screens | General inputs/geometry adopted by settings, reading and interface screens; remaining screens/components still require migration |
| G: reader preparation and asset lifetimes | Pending; preserve specialized rendering and benchmark acceptance |
| H2/H3/I: workflows and first-party cleanup | Pending |
| J: full correctness/performance acceptance | Focused host suites pass; full native/pixel, firmware and hardware matrices remain |

The renderer work still needs failing real-UI regressions for icon/signature,
capture and structural-clearing faults. General input helpers do not substitute
for that repair. A complete refactor requires all master-plan acceptance criteria,
not only successful compilation of new abstractions.
