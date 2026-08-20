# Linux Packaging

**Status: not implemented.** Milestone M8, and blocked on the application being
built and tested on Linux at all.

## Plan

**AppImage as the primary format.** A review player should be something an
animator can download and run without root, on whichever distribution the studio
happens to use. AppImage does that; a distribution package does not.

A `.deb` may follow if there is demand from Ubuntu-based studios, but only as an
addition.

## Contents

- `ATKPlayer` and its Qt libraries, dynamically linked
- FFmpeg shared objects, LGPL build
- A `.desktop` entry and icons at the standard sizes
- Licence texts

## Build approach

- Build against the oldest glibc that is still worth supporting, so the AppImage
  runs on older distributions. In practice this means building on an old base
  image, not on the newest one available.
- Use `linuxdeploy` with its Qt plugin to collect dependencies.
- Do **not** bundle the graphics drivers or anything that must match the host.

## Display servers

Test on both Wayland and X11. Qt picks a platform plugin at runtime and the two
behave differently for fullscreen, high-DPI scaling and screensaver inhibition —
which is exactly the code in `src/platform/linux/PlatformInfo_Linux.cpp` that M8
has to finish.
