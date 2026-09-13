# Rotary input feedback checkpoint

Base: `ebfca498d8f788fdabc61c41a0fbba8ab47298b9` on `refactor/cpp-ui-architecture`.

## Change

`ui::rotaryStepper` applies its minus/plus button edits before invoking the dial. Previously the model changed after the dial had already been submitted, so the updated number required another frame. The composite still submits each of its three controls once; it adds no state, allocation, deferred callback or extra paint pass.

The existing range/projection selectors, ordinary-integer numeric inputs, normalization checks, minimum-width guard, numeric clamping and geometry are preserved. The source-selected IMU adapter, regular/watch screen implementations and specialized reader are unchanged. No alternative settings wrapper or option-model requirement is introduced.

The recording primitive now records the accepted numeric value that would be painted. The new regression checks same-invocation feedback for both side buttons, no-op edits at the limits and dial-originated edits. Existing geometry tests locate controls by kind/label rather than depending on the former submission order. The test-only recorded value does not change the production Context.

## Validation performed

The pre-existing input tests passed on the earlier source baseline; later numeric-input additions were preserved and included in the final test runs. The new same-frame-feedback regression then failed against the unmodified composite, including after reconciliation with the newer branch head. It passed after moving side-button processing before the dial.

Both `test_inputs.cpp` and `test_input_edges.cpp`, linked against the existing `Recording.cpp` and real production UI headers, compiled and passed with GCC 14.2, Clang 17, GCC ASan/UBSan and GCC `-O2`. Flags include C++23, warnings-as-errors, no exceptions, no RTTI and assertions enabled. The edge executable retains its normalized-selection, zero-idle-snapshot and 3,190 positive/disjoint geometry checks.

The local workspace was a verified source subset, not a complete firmware checkout. To reproduce the two executed test binaries from a checkout:

```sh
for source in test_inputs test_input_edges; do
    g++ -std=c++23 -Wall -Wextra -Werror -pedantic \
        -fno-exceptions -fno-rtti -UNDEBUG \
        -Itest/native_ui_inputs/support -Isrc -Itest/native_ui_inputs \
        test/native_ui_inputs/$source.cpp test/native_ui_inputs/Recording.cpp \
        -o /tmp/rsvpnano-$source
    /tmp/rsvpnano-$source
done
```

The existing `test/native_ui_inputs/run.py` already runs both executables; that full runner, its screen/geometry executables, other native suites, firmware builds, CI and hardware benchmarks were not rerun for this checkpoint. These are API/composition checks, not real touch-capture, font, pixel-equivalence or display-transfer measurements. No 1,000-WPM performance claim follows from unchanged reader source.

The master plan's widget-identity, capture, damage-restoration and complete-screen migration work remains outstanding. This fixes one responsiveness defect in a reusable control without pretending to complete the rendering-engine redesign.
