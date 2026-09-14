#pragma once

// The build selects one presentation; shared screen state must use the same selection.
#ifndef RSVP_UI_WATCH
#define RSVP_UI_WATCH 0
#endif
static_assert(RSVP_UI_WATCH == 0 || RSVP_UI_WATCH == 1);
