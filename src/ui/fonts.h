// The UI's fonts (Phase 9): Inter for text, TrackerMono (converted from IBM Plex Mono) for
// numbers, so digits keep the same width and prices don't shift as they change.
// Generated from the .ttf files by tools/make_fonts.py into ui/fonts/*.h; licenses in
// ui/fonts/LICENSE-fonts.txt. Draw them with ui::text() (ui/gfx.h), not gfx.print().
//
// The fonts are ASCII plus five characters in the codes after '~'. Use these names in
// strings (as separate literals: "Wed" UI_DOT "1:46", never "\x80..." followed by a hex
// digit). ui::fitText() uses the ellipsis to shorten text. Mono96 has only 0-9 and ':'.
#pragma once
#include "DFRobot_GDL.h"   // GFXfont

#define UI_ELLIPSIS "\x7f"   // …
#define UI_DOT "\x80"        // ·  separator
#define UI_NEXT "\x81"       // ›  "opens something"
#define UI_PREV "\x82"       // ‹  back
#define UI_DASH "\x83"       // –  ranges, e.g. 22:00–07:00

extern const GFXfont Inter10;       // small labels, captions
extern const GFXfont Inter12;       // body text
extern const GFXfont InterSemi10;   // pills, small caps labels
extern const GFXfont InterSemi12;   // tabs, buttons, row labels
extern const GFXfont InterBold15;   // stock symbols, toast titles
extern const GFXfont InterBold18;   // screen titles
extern const GFXfont InterBold24;   // splash title
extern const GFXfont Mono10;        // chart axis labels
extern const GFXfont Mono12;        // small numbers, % pills
extern const GFXfont Mono14;        // prices in lists
extern const GFXfont Mono20;        // keypad keys
extern const GFXfont Mono30;        // big price, splash counter, keypad entry
extern const GFXfont Mono96;        // the overnight clock (digits and ':' only)
