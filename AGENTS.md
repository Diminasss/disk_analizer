# Disk Analyzer Development Guide

## Build

Use Qt 6.8 or newer and a compiler with C++23 support:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=A:/applications/Qt/6.8.1/mingw_64
cmake --build build
```

The current bundled GCC 13.1 toolchain does not implement C++26. Keep the code
compatible with C++23 until the local compiler is upgraded, then raise
`CMAKE_CXX_STANDARD`.

## Architecture

- `include/domain`: UI-independent file tree data.
- `include/services` and `source/services`: filesystem scanning and aggregation.
- `include/ui` and `source/ui`: Qt model, delegate, and widgets.

Keep scanning outside the UI thread. Preserve Unicode paths as `QString` and
avoid conversions through narrow platform strings. Do not traverse symbolic
links because links can introduce cycles.

Keep scan progress accurate and thread-safe. Count filesystem entries before
the size scan, report progress from the worker through queued UI updates, and
throttle notifications so large disks do not flood the event loop.

On Windows, report allocated disk space rather than logical stream length.
Deduplicate files by volume and file identity so NTFS hard links are charged
only once per scan. Include allocated space for alternate NTFS streams. Mark
duplicate hard-link nodes in the UI instead of silently charging the same
physical blocks to multiple folders.

Catch failures at application boundaries. Exceptions must not escape Qt slots,
background tasks, or `main`. Show actionable errors to the user in a modal
error dialog and mirror them in the status label when the main window exists.

## Verification

Build after source changes. Manually scan a directory that contains Cyrillic
names, expand nested folders, select a file and a folder, and verify that
`Open location` opens Explorer.

Run the automated scanner check with:

```powershell
ctest --test-dir cmake-build-debug --output-on-failure
```
