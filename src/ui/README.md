# UI building blocks

`Context` owns input and rendering state. Screen data owns values and destinations;
layout code chooses rectangles. Controls do not own settings, persistence or boards.
The regular and watch screens deliberately retain separate layouts.

## Select from existing data

`Inputs.h` provides a tap-to-cycle selector. Pass an existing forward range and,
when necessary, projections for its display label and stored value. It does not
require a `Choice` object, form instance, registry, or separately allocated list.
Plain strings work without projections:

```cpp
constexpr std::array<std::string_view, 3> tools{"Move", "Rotate", "Scale"};
std::string selected = "Move"; // Owned by the screen/model, outside the frame loop.

// In the screen's draw function:
const bool changed = ui::select(context, toolRect, "Tool", selected, tools);
```

Existing records can be used directly:

```cpp
struct DeviceInfo {
    int id;
    std::string name;
};

// devices and selectedDeviceId belong to the caller.
const bool changed = ui::select(context, deviceRect, "Device", selectedDeviceId,
                               devices, &DeviceInfo::name,
                               {.layout = ui::ValueLayout::Card, .textSize = 2},
                               &DeviceInfo::id);
```

Captions/projections can return text or the existing `UiText` keys. A projection
may return a temporary formatted string: it is consumed synchronously, not kept
in the control. Hidden rectangles and empty ranges do not evaluate labels. A
missing current value displays `Unknown`; activation chooses the first item.
Activation wraps at the end and reports a change only if the stored value differs.
The caller must keep a borrowed selected value valid; the control cannot extend a
catalog entry's lifetime. Projections should not mutate the range being visited.

`ValueStyle` only chooses presentation (inline, stacked, or card); it does not
change selection semantics. Application-specific values, translations and menu
destinations remain under `screens/`, not inside the generic control implementation.

## Value actions and lazy display data

`ui::valueButton` displays a caption/value pair and reports activation without
mutating the caller's data. It accepts either a value or a synchronous supplier;
a hidden field does not call the supplier. This is useful for catalog names,
formatted status or any other computed value, not just settings. `ui::select`
builds its cycling interaction on this same primitive.

## Numeric input

`ui::number` selects a slider or stepper from `NumberInput` without a form object.
Use a caller-owned `int` with explicit limits for ordinary numeric input; no
settings wrapper is required:

```cpp
// minutes belongs to the caller, outside the frame loop.
const bool changed = ui::number(context, durationRect, "Duration", minutes,
                                5, 60, 5, " min", ui::NumberInput::Stepper);
```

The bounded-value overload supplies `min()`, `max()` and `step()` to that same
input path. It reports whether the value actually stored changed, including when
assignment normalizes a proposed edit. Hidden rectangles, reversed bounds and
nonpositive steps return false before translating the caption or invoking a
primitive. This retains the existing integer slider/stepper semantics; it is not
a floating-point control or a replacement for domain validation.

The existing bounded-value contract (`min()`, `max()`, `step()`) also works with
`Context::rotary(rect, value, label)`. `ui::rotaryStepper` composes the dial and
increment/decrement buttons without requiring a form. It returns an actual edit,
not merely a button press at a numeric limit. Buttons fit beside the dial instead
of overlapping it on narrow rectangles. Callers still decide what an edit does.

## Geometry independent of rendering

`Geometry.h` contains rectangles and row/column/grid packing without Arduino,
font or application dependencies. Existing `Grid::next()` remains the single-cell
operation. `next(columns)` spans cells, and `next(Grid::FullRow)` starts a complete
row when necessary. `rowsUsed()` and `gridRows(range, columns, spanProjection)` use
the same packing rule. Full rows include any remainder from integer cell division.

`Grid` remains a cursor, not a scrolling container: the caller must choose a
viewport/paging policy. It does not guarantee a minimum touch size or clamp an
unbounded list into its height. Cell-index exhaustion returns an empty rectangle
instead of wrapping the index.

## Boundaries still being refactored

See [the general UI review](../../docs/ui-architecture-audit.md) for source-backed
engine findings, ownership boundaries and the regressions needed to close them.

These building blocks use the existing immediate controls. This change does not
replace slot-based identity, damage ordering, capture or the text/paint backend.
In particular, selection uses the existing control's paint-before-edit behavior;
it is not a new same-frame reconciliation engine. The master plan's owned-region,
structural repaint and capture work remains separate and unfinished.

No new fields are added to `Context`, and no heap widget tree or deferred callbacks
are introduced. The selector itself retains nothing and allocates nothing; caller
projections and the underlying text renderer may allocate. RSVP/page rendering,
prefetch, strip painting and benchmarks are not changed here. Unchanged source is
not proof of unchanged device timing: the 1,000-WPM hardware benchmark remains a
required validation step for the overall refactor.

## Tests

Run from the repository root:

```sh
python test/native_ui_inputs/run.py
CXX=clang++ python test/native_ui_inputs/run.py
CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie' python test/native_ui_inputs/run.py
```

The tests compile the real geometry, input/Context headers, bounded value, and
regular/watch settings screen sources. Primitive rendering is a recording backend;
platform/font owners and unrelated screen declarations are stubbed. The settings
model and bounded values are production types. This suite has no replacement
`ui/Ui.h`. Tests cover ranges/projections, empty and missing selections,
temporary text lifetime, hidden work, spans, bounded edits, navigation and both
presentations. These do not replace pixel/capture, firmware-build or hardware tests.

For this migration, GCC 14.2, Clang 17 and GCC ASan/UBSan host runs passed. A control
trace comparison against `806686d` covered ten sizes, two screens and controlled
page windows (300 cases per presentation). Regular traces matched; watch traces
changed only the two side-button bounds on the narrow portrait dial. `Context`
and `Grid` sizes matched in the host fixture. These are API/composition checks,
not display screenshots, target memory measurements, or hardware benchmarks.

The newer shared interface screen/layout split from `0b71a9f` is preserved and
migrated to these same primitives. Its existing `test/native_interface/run.py`
behavior cases remain and use a dedicated recording Context/catalog fixture;
that fixture now consumes production geometry and identifies fields from their
actual captions, not from translation-call order. Both suites pass with GCC,
Clang and GCC ASan/UBSan. No full firmware build or hardware run is claimed.
