# Immediate UI construction

The application owns its data. `Context` owns interaction and rendering state;
layouts choose geometry. Submitting a control does not create a second model or
require a retained widget object. An input's return value reports an edit, not a
request to repaint or persist anything.

## One drawing description

Standard controls submit an internal `Item` before processing input. Submission
validates the slot's geometry and kind before a button, slider, stepper or rotary
can consume capture. The item then receives a stateless painter and its arguments.
`DrawCall` borrows that same argument pack for both its visual fingerprint and
its synchronous invocation. There is no separate per-control hash field list.

The unchanged path skips the painter, including text measurement, layout and
geometry preparation inside it. The dirty path paints and marks the frame drawn.
Strings are fingerprinted by length and contents, not their addresses; rectangle
fields are serialized explicitly instead of hashing object padding. The painter
and argument types are part of the signature. The binding itself does not allocate
or copy the model; existing text layout and caller formatting may still allocate.

This construction is used by labels, separators, setting/value surfaces, toggles,
buttons, icon buttons, tabs, batteries, progress bars, steps, sliders, steppers,
dials, cards, dock items, progress rings and rotary controls. It preserves the
existing direct and aligned-strip rendering paths. The rotary uses the same
capture owner as other inputs rather than an independent rectangle-owned gesture.

### Custom drawing

Pass drawing values once. Do not compute a signature, maintain a dirty flag, or
call `markDrawn` separately:

```cpp
context.draw(bounds,
    [](Arduino_GFX& output, ui::Rect rect, int fillWidth, uint16_t ink) {
        output.fillRect(rect.x, rect.y, fillWidth, rect.h, ink);
    },
    model.fillWidth, context.color(ui::themes::Accent));
```

The callback receives graphics and translated bounds, not `Context`: it is a
paint operation, not a container for nested input/widget submissions. It must
be stateless and draw only from its arguments and the supplied output. Capturing
lambdas and unsupported argument types are rejected. Do not read unbound mutable
globals; C++ cannot discover such dependencies. Scalars, enums, strings and
rectangles are supported; arbitrary model objects are deliberately not hashed
by memory representation.

Arguments, including temporary strings, are consumed during the call. Nothing
is retained for another frame. The painter may run once per display strip, so
input handling, model mutations and external operations must stay outside it.
Its region must own its background; this is not an alpha-compositing API.

## Existing inputs operate on existing values

Use the actual input, rather than an additional numeric-control selector:

```cpp
// These are caller-owned values, not UI copies.
const bool moved = context.slider(positionBounds, "Position", model.position, 0, 100);
const bool resized = context.stepper(sizeBounds, "Size", model.size, 1, 64);
const bool turned = context.rotary(angleBounds, model.angle, -180, 180, 5, "Angle");
```

The bounded-value overloads obtain `min()`, `max()` and `step()` from the existing
value type. They return whether assignment actually changed the stored value,
including normalization. Sliders retain preview-on-drag/commit-on-release
semantics. Invalid numeric ranges, nonpositive steps and hidden rectangles do not
invoke their scalar primitives. The `number`/`NumberInput` wrapper has been removed.

Regular and watch interface layouts select the native slider or stepper in their
own application presentation source. Brightness application remains in the shared
screen workflow; the UI library does not own that device operation.

## Existing ranges and composition

`ui::select` in `Inputs.h` consumes an existing forward range. Label and value
projections allow ordinary domain records without manufacturing option objects:

```cpp
const bool selected = ui::select(context, deviceBounds, "Device", model.deviceId,
                                 devices, &DeviceInfo::name, {}, &DeviceInfo::id);
```

Projected text is consumed synchronously. Hidden fields and empty ranges do not
resolve labels. An unknown selection displays `Unknown`; activation selects the
first item. A caller borrowing a selected string or record must preserve that
record's lifetime. Projections must not mutate the range being traversed.

`valueButton` and the existing `rotaryStepper` composition remain optional helpers,
not required control construction. The former accepts a synchronous lazy display
value; the latter combines existing buttons and rotary input. Neither owns app
settings or persistence. Selection/value actions still paint before returning
activation; this continuation does not implement same-frame reconciliation for
those compositions.

`Geometry.h` contains independent rectangles and row/column/grid cursors.
`Grid::next(columns)`, `next(Grid::FullRow)`, `rowsUsed()` and `gridRows()` share
packing rules. Grid is not a scrolling container or a minimum-touch-size policy.

## Remaining engine boundaries

This is an incremental construction migration, not a completed ImGui replacement.
Submission order still supplies slot identity: dynamic same-kind row reordering
needs stable IDs. Interleaved clearing of moved/retired regions is not yet safe
for arbitrary overlapping content. Those require production input/pixel tests,
not screen-specific forced redraws.

The hourglass, keyboard and legacy `redraw(signature)` consumers have not all
migrated to argument-bound drawing. Font lifetimes, text line limits, independent
paging and overflow behavior still need their own work. The fixed-capacity cache
uses 32-bit fingerprints; field-boundary ambiguity is fixed, but hash collisions
are not mathematically excluded.

## Tests

```sh
python test/native_ui_drawing/run.py
CXX=clang++ python test/native_ui_drawing/run.py
CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' python test/native_ui_drawing/run.py
python test/native_ui_inputs/run.py
python test/native_interface/run.py
```

The new drawing suite links production `Ui.cpp`, `Controls.cpp` and `Icons.cpp`
against the shared host canvas. It checks unchanged-frame pixel/flush/text work,
string mutations and field boundaries, capture cancellation, same-frame toggle
pixels, dock icon-only changes and custom drawing on direct/aligned paths. Its
platform assets and ASCII text services are stubs; it does not validate actual
font decoding, Unicode shaping, the firmware matrix or device timing.

The input suite's numeric cases now exercise native slider/stepper templates and
stored-value normalization. Its rendering primitives are a recording fixture,
separate from the production rendering tests. The interface suite retains its
existing recording UI/catalog boundaries. None replaces hardware benchmarks.
