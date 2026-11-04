# IOU Tracker source

Upstream: https://github.com/bochinski/iou-tracker

Original Python source blob: `12cd6bb6526b9651cf185d75189be266ba376c28`.
The original `iou_tracker.py` and MIT `LICENSE` are retained here.

`software/src/iou_tracker.h` is a board adaptation: C++11, online processing,
fixed 16-track storage, one-to-one highest-IoU matching, and the existing
2.8-second lost-track timeout. It uses the existing detector and fall-state
storage. The default single-person path does not use this matcher.
