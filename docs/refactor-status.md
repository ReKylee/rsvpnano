# C++ and UI refactor status

Branch: `refactor/cpp-ui-architecture`. Updated 13 September 2026.

This is an incremental implementation record, not a declaration that the master refactor is complete. The continuation started at `808e3c3`; existing work was preserved and new commits were fast-forwarded without rewriting history.

## Completed in this continuation

### Source-selected IMU adapter (`806686d`)

The QMI8658-specific `Board::Imu` implementation now lives in `src/drivers/imu/qmi8658/BoardImu.cpp`, selected by the existing driver source filters. The unconditional `src/board/BoardImu.cpp`, `ImuBinding.h`, and six per-board factory definitions were deleted.

Each applicable board exposes its existing IMU wiring through `Board::Config` and supplies its I2C controller number. The private device is constant-initialized without peripheral I/O or an externally exposed reference. LCD 3.49 and AMOLED 2.41 retain `Wire1`; AMOLED 1.8/2.06/2.16 and C6 1.47 retain `Wire`. The driver register sequence, scaling, fallback addresses and read policy are unchanged.

The public board header does not instantiate or require a QMI8658 object. The no-IMU host fixture only establishes that this header can link without a provider; it is not evidence that a complete no-IMU firmware target exists. A future alternative device must supply only its selected implementation, not a dummy QMI8658 binding.

### Shared interface/display settings (`88af8e7`)

`InterfaceSettingsScreen.cpp` now owns the shared editing behavior. The regular and watch source files provide field geometry and presentation through `InterfaceSettingsLayout.h`. Regular inline settings/sliders and watch cards/steppers remain separate, with their original dimensions and navigation presentation.

`SettingsControls` accepts a synchronous lazy value supplier. Invisible fields do not evaluate that supplier, so watch pages no longer resolve off-page theme/locale names or format standby durations. The callback is consumed immediately and is not retained. Numeric presentation is selected by the layout rather than by duplicating the editing workflow.

Screensaver values and labels share one typed table in the existing order: Life, Maze, Voronoi, Screen Off, Reaction. Live brightness application remains in place. Choosing the sole current theme or the already selected fallback locale no longer reports a mutation or reapplies the same value. These no-op corrections have dedicated tests.

## Validation actually performed

The three suites below were compiled and executed locally with GCC 14.2, Clang 17, and GCC AddressSanitizer/UndefinedBehaviorSanitizer. Builds use C++23, warnings-as-errors, no exceptions and no RTTI.

| Suite | Verified scope |
|---|---|
| `test/native_imu/run.py` | Actual driver register traces, decoding, fallback, failure and timeout behavior; actual board adapter on single/dual I2C controller fixtures; unavailable-controller compile rejection; no-IMU header/link fixture |
| `test/native_choices/run.py` | Actual typed form/choice helpers with a recording widget boundary; cycling, bounded edits, menu geometry, lazy temporary-string lifetime, hidden work and numeric presentation |
| `test/native_interface/run.py` | Actual shared interface screen and each actual presentation source, with recording UI/catalog boundaries; live brightness, no-op edits, locale/screensaver/standby cycling, Back, hidden lookup counts and field bounds |

Interface geometry cases cover regular 640x172 and watch 450x600, 480x480, 410x502, 368x448, 172x320, 600x450, 502x410, 448x368 and 320x172. Paging state is supplied by the fixture. These are behavior/field-geometry checks, not end-to-end touch, font, clipping, pixel-equivalence or display-transfer tests.

Run from the repository root:

```sh
python test/native_imu/run.py
python test/native_choices/run.py
python test/native_interface/run.py
```

Each runner accepts `CXX` and `CXXFLAGS`, for example `CXX=clang++` or `CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'` on a supported host. The suites are added to the existing Tests workflow; its main-push/PR triggers are unchanged. CI wiring is not a claim that a GitHub Actions run completed.

## Performance and hardware boundary

No RSVP/page-reader, shaping, prefetch, display-driver, touch-acquisition, ISR, sleep or PMU implementation was changed in this continuation. In particular, the IMU acquisition algorithm was retained; only the selected adapter and its construction boundary changed.

No full PlatformIO suite, supported-target firmware build matrix, device test, RAM/flash comparison or 1,000-WPM hardware re-benchmark was run here. The local workspace contained the exact sources needed by the host suites rather than a complete firmware checkout/toolchain. Unchanged hot-path source is not proof of unchanged firmware timing. Compare the same existing hardware benchmarks before accepting a release.

## Remaining master-plan work

| Workstream | Status |
|---|---|
| A: complete inventory, baseline, resource budgets | Pending beyond the focused sources and host baselines inspected here |
| B/H1: domain ownership, accepted settings, transport-independent operations | Pending; UI callbacks and App settings application are not yet centralized |
| C: board/device composition | QMI8658 ownership and source selection implemented with host coverage; PMU, touch, SDMMC, audio and broader lifecycle extraction pending |
| D: concurrency/resource lifetimes | Pending broader access map, handover and failure validation |
| E: UI identity, capture, damage and retained-payload contract | Pending; the current slot/hash engine has NOT been replaced or declared correct |
| F: UI authoring and all screens | Typed reading/settings helpers from earlier commits retained; interface behavior now shared across both layouts; other screens and complete components pending |
| G: reader preparation and asset lifetimes | Pending; preserve the specialized renderer and benchmark acceptance boundary |
| H2/H3/I: remaining workflows and first-party cleanup | Pending |
| J: full correctness/performance acceptance | Focused host suites added; full native/pixel, firmware and hardware matrices pending |

The next renderer work must first reproduce the audit's icon/signature, capture and structural-clearing failures in the real UI fixtures. A helper around the existing controls is not a substitute for correcting that contract. No screen, transport or device-family migration is complete until its old implementation is removed and its own acceptance tests pass.
