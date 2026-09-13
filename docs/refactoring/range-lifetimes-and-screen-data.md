# Range selection and screen-data boundaries

## Changes

`e6d244a` fixes `ui::select` when a forward range generates its elements by value. The next element is bound before its key projection runs, so a projected reference or view remains valid through comparison and assignment. Existing records remain borrowed; no option wrapper, retained callback, widget object hierarchy or per-frame owning copy was added. A selected key must still own its data or refer to storage that outlives the selection; assigning a view into a generated temporary is not an ownership transfer.

Screen and action identifiers now live in `ui/screens/Screen.h`, with their original underlying types, names and values. `Screens.h` includes that definition rather than owning it alongside network, storage, font and screen-class declarations. The settings-page entry table includes only its geometry, caption and identifier dependencies. Its standalone compile test intentionally has no platform-stub include directory. The native UI input fixture also uses the production identifiers instead of a copied enum with different action values.

Regular and watch presentation sources, page geometry, existing numeric/selection semantics, source-selected IMU composition, reader algorithms and graphics backend were preserved. This is dependency and lifetime work, not a new UI framework or a completed renderer migration.

## Verification

Before the lifetime fix, the generated-scalar regression failed GCC's dangling-reference diagnostic. Allowing that diagnostic for the baseline run reproduced an AddressSanitizer stack-use-after-scope in `Inputs.h`. The corrected regression covers generated scalars with a non-common sentinel, generated owning records, reference and string-view projections into those records, noncopyable borrowed records, wraparound, unknown values and hidden/idle generation work.

Before the header extraction, compiling the settings-page data without platform stubs failed because it pulled in `Arduino.h` through `Screens.h`. The new header test compiles without those stubs and checks the existing ordinal values and underlying types of every screen/action identifier.

The complete `test/native_ui_inputs/run.py` was compiled and executed with GCC 14.2, Clang 17 at `-O2`, and GCC AddressSanitizer/UndefinedBehaviorSanitizer. All seven executables passed: screen-data headers, geometry, inputs, input edges, input lifetimes, regular screens and watch screens. Tests retain warnings-as-errors, C++23, no exceptions, no RTTI and enabled assertions. Input edges include the existing 3,190 positive, disjoint rotary layouts. The unmodified recording and existing test sources were checked against the branch's Git blob hashes before the final runs.

Run from the repository root:

```sh
python test/native_ui_inputs/run.py
CXX=clang++ CXXFLAGS='-O2' python test/native_ui_inputs/run.py
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 \
  CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  python test/native_ui_inputs/run.py
```

These tests compile production UI template APIs and both actual presentation sources, with recording primitives and controlled paging. They do not exercise real widget painting, font decoding, capture, display transfers, or hardware. The local workspace is a verified source subset, not a full firmware checkout. Other native suites, PlatformIO firmware builds, CI runs, device tests, RAM/flash comparisons and the 1,000-WPM hardware benchmarks were not run in this checkpoint.

Stable widget identity, capture arbitration, structural damage restoration, text-cache ownership, the remaining screen/domain migrations and the wider master-plan work remain open. Passing this focused runner is not a substitute for that work or for pixel-equivalence and target-performance acceptance.
