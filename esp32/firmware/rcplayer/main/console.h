// Line-oriented serial console on the board's console port.
//
// Commands (one per line):
//   snap          dump the current frame (half resolution, RGB565, base64) for tools/snap.py
//   next / prev   switch embedded document
//   doc N         open embedded document N (0-based)
//   list          list embedded documents
//   stats         print frame timing now
//   bench         time raw full-screen raster passes (clear, opaque rect, blended rect)
//   act NAME[=VALUE]          run a host action, as a document's hostAction(0, ...) would
//   named [PREFIX]            the showing document's named variables, and their values
//   res full|half render at panel resolution, or at half and let the PPA upscale 2x
#pragma once
#include <atomic>
#include <cstdint>

namespace console {

enum class Cmd : int { None, Snap, Next, Prev, Doc, List, Stats, Res, Bench, Act };

struct Request {
    Cmd cmd = Cmd::None;
    int arg = 0;
    char text[96] = "";       // Act: "name[=value]"
};

void start();                 // spawns the reader task
bool poll(Request& out);      // non-blocking; called from the player loop

}  // namespace console
