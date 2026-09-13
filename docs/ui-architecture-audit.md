# General UI architecture audit

Reviewed against `f51b964` on `refactor/cpp-ui-architecture`, 13 September 2026.
This supplements `refactor-status.md` and `src/ui/README.md`. It applies to the
shared UI engine and authoring API, not only the current settings screens.

## Ownership and authoring direction

Generic building blocks are useful when they centralize a real UI operation.
`Inputs.h` now selects directly from a caller's range, with label/key projections
when needed. Keep that API; do not layer a second `Choice` model or a settings
form object over it. Application-specific labels, values and destinations remain
screen data. A typed value/label record can still be appropriate for a particular
domain; the generic control should not require every caller to manufacture one.

Use stateful objects for resources and invariants: frame/rendering state, input
capture, font assets and interaction state that persists between frames. Prefer
plain data for menu entries and presentation, and functions for stateless layout
and control composition. Neither inheritance nor data-driven authoring is an
end in itself. Splitting `Context` into forwarding managers without changing
ownership would leave the current coupling intact.

The selector and value action have intentionally different contracts: `select`
reports an actual value change; `valueButton` reports activation. Keep effects
such as applying brightness or switching a theme explicit in the workflow.
Moving those effects to the eventual application/domain owner remains separate
from generic input composition. Do not short-circuit subsequent control emission
when accumulating changes in an immediate-mode screen.

## Production-engine findings

These are source-level findings, not claims of completed renderer regression
tests. The inspected mechanisms remain in `Ui.cpp` and `Ui.h`; the range-driven
input refactor did not replace them.

### Input capture is validated after some controls activate

`Context::button` and `Context::iconButton` call `tapped` before `claim`.
`claim` detects a changed rectangle/kind and clears `capturedSlot_` afterwards.
Consequently, that structural validation is too late to guard the activation
that just ran. Sequential slot position still supplies identity; changing to
an explicit identity must also account for list reordering and replacement,
not merely preserve an array index under a new name.

Acceptance: press a control, then move, replace, reorder, remove or disable it
before release. No replacement receives the old gesture. Test screen transitions
and slot-capacity exhaustion as well. Exercise production capture, not a fixture
that simply returns a requested activation.

### Damage clearing and painting are interleaved

`Context::claim` can clear a previous rectangle while current controls are being
painted. `endFrame` clears retired slots after surviving controls have painted.
An overlapping survivor can therefore be erased after its own draw. Invalidating
only the removed widget does not restore the content underneath it.

Acceptance: compare incremental pixels with a clean repaint after removals,
moves, resizes and overlapping updates, including background restoration. All
surviving intersecting content must be restored in drawing order. Correct the
frame/damage contract rather than adding screen-specific forced-redraw calls.
Do not claim an extra full repaint is performance-neutral without measurement.

### Visual-state signatures lose field boundaries

`setting` computes `signature(value, signature(label))`. The signature helper
appends text to the same FNV state without encoding the field boundary. Thus
label/value pairs `("ab", "c")` and `("a", "bc")` contribute the same bytes even
though their layout differs. `button` likewise appends its left and right detail
strings consecutively. This is deterministic boundary ambiguity, not merely the
possibility of a random hash collision.

`dockItem` in `Controls.cpp` also draws an icon that is absent from its signature.
`card` and keyboard input concatenate adjacent text fields in the same way.
These controls need the same dependency rules as ordinary buttons; fixing one
settings call site would leave the mechanism broken elsewhere.

Acceptance: vary each visible input independently, and change text-field
boundaries while keeping the concatenated bytes unchanged. Include icon-only
changes and transitions between control kinds. Keep interaction identity separate
from visual-state comparison. A field-aware signature can address this specific
ambiguity, but does not by itself repair slot identity or overlapping damage.

### Persistent component state is bundled into Context

`Context` owns one `gridPage_`, capture and rotary state alongside painting and
font caches. That is not a general contract for independently paged or draggable
components. Moving these members to a generic service would not give each
component its own lifetime.

Acceptance: two independently paged components do not change each other's page;
screen changes and removal cancel the appropriate interaction. Assign persistent
state to the component/screen that owns it, while retaining an explicit frame
owner for input arbitration and rendering resources.

### Rendering and lifetime boundaries must remain explicit

Custom drawing exposes `redraw`, `signature` and `markDrawn`. `TextLayout` owns
its text strings but borrows font storage. The current paint callback runs
synchronously and can execute once per transfer strip. Neither that callback nor
its captured references automatically becomes safe to retain in a deferred frame.

Acceptance: temporary text, catalog/font replacement, locale changes, orientation,
clipping and aligned transfers remain correct. A deferred payload must own or
explicitly pin what it retains; rendering callbacks must not repeat application
side effects when replayed across strips. Preserve the specialized reader path
unless a measured, tested change is required.

### Text layout advertises more lines than it constructs

`appendText` in `Ui.cpp` constructs at most two lines despite accepting `maxLines`.
`prepareFixedText` in `Controls.cpp` has a separate eight-entry wrapping array and
consumed-byte accounting. These are general text-layout contracts, not settings
screen concerns.

Acceptance: define supported limits and test wrapping, measurement, truncation,
consumed bytes and complete pagination. Include constrained bounds, UTF-8,
mixed RTL/LTR text and fallback fonts. Keep the specialized reader path where it
fits; a generic label is not a replacement for prepared reading content.

### Slot capacity needs explicit overflow behavior

`Ui.h` fixes the slot budget at 64. When `claim` exceeds that capacity, it returns
a draw-needed result without retaining the overflow slot. Raising the constant
alone would not define capture, cleanup or diagnostics for that state.

Acceptance: test at and beyond capacity, including disappearance of overflow
controls. Define interaction and repaint fallback behavior, expose diagnostics,
and measure actual high-water usage before choosing a larger memory budget.

## Required completion evidence

The next engine increment needs failing production input/pixel regressions for
the mechanisms above, followed by fixes that make those tests pass. Recording
screen tests remain useful for composition, hidden work and edit semantics; they
are not evidence that capture or framebuffer damage is correct. All listed engine
areas remain open, even where the current application has only one instance.

After engine changes, run the supported firmware build matrix and compare
RAM/flash and the existing hardware timing benchmarks, including 1,000 WPM.
No such firmware, pixel or hardware validation was performed in this audit pass.

## Reconciliation and validation for this follow-up

An overlapping UI commit (`f51b964`) reached the branch while the earlier control
cleanup was being prepared. This follow-up preserves its range-driven inputs,
removed wrappers, screen migrations, geometry API and tests. It does not restore
the superseded control facade or introduce a competing value-control API.

`test/native_ui_inputs/test_geometry.cpp` now also checks consecutive full rows,
partial-row placement, zero-span/zero-column normalization, rectangle boundaries,
intersection/rotation, row/column cursors and index exhaustion. Failed spanning
requests must not consume the remaining cells. The test includes the production
`Geometry.h`; it does not copy the geometry implementation or use a graphics stub.

The incoming geometry baseline passed with GCC. The expanded geometry executable
passed with GCC, Clang, and Clang AddressSanitizer/UndefinedBehaviorSanitizer, using
C++23, warnings-as-errors, no exceptions and no RTTI. The full incoming input and
interface suites were not rerun during this reconciliation; their earlier results
remain separately recorded in `refactor-status.md`. Results from the superseded
local control implementation are not evidence for the current branch.

## Explicit numeric inputs and validation (`0be2e87` continuation)

Preserved the newer selector normalization fix, tiny-rotary bounds checks and
edge tests. Extended the existing `ui::number` helper with a caller-owned `int`
and explicit minimum, maximum and step; no settings wrapper or new form API is
required. The bounded-value overload supplies its limits to that same path and
reports the value actually stored after assignment. Hidden rectangles, reversed
bounds and nonpositive steps stop before translation or primitive work.

The [UI README](../src/ui/README.md#numeric-input) includes a non-settings duration
example. The interface recording fixture now accepts the explicit primitive
overloads; its existing behavior tests are retained. No Context fields, retained
callbacks, persistence rules or rendering implementations were added.

After reconciling with `0be2e87`, the existing input and input-edge executables
(`test_inputs.cpp` and `test_input_edges.cpp`, each linked with `Recording.cpp`)
passed with GCC, Clang and GCC AddressSanitizer/UndefinedBehaviorSanitizer. These
use the production Context header/templates with recording rendering primitives.
The new numeric cases cover plain/signed integers, both presentations, localized
and literal captions, hidden work, invalid constraints, equal bounds, no-op edits,
clamping and assignment that normalizes the primitive's proposed value.

`test/native_interface/run.py` also passed in all three configurations, compiling
both actual regular/watch interface layouts and shared behavior against its
separate recording Context/catalog fixture. Existing live brightness, no-op
edits, catalog cycling, Back, hidden work and layout-bound checks passed.

The builds use C++23, warnings-as-errors, no exceptions and no RTTI. The sanitizer
flags are `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie`.
The complete `native_ui_inputs` driver, other native/pixel suites, the PlatformIO
firmware matrix and device benchmarks were not rerun for this numeric increment.
These results do not close the production-engine findings above or establish
pixel/capture correctness, target memory usage or 1,000-WPM device timing.
