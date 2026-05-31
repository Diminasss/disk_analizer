# Disk Analyzer

Native Qt application for inspecting disk usage. Select a disk or directory,
wait for the background scan, then expand folders to inspect their contents.
Each row includes a colored size bar scaled against the largest neighboring
item. Files and folders are both shown.

## Build

The local Qt installation and CLion MinGW toolchain can be used as follows:

```powershell
$env:PATH='A:\applications\CLion 2025.3\bin\mingw\bin;' + $env:PATH
& 'A:\applications\CLion 2025.3\bin\cmake\win\x64\bin\cmake.exe' `
  -S . -B cmake-build-debug -G Ninja `
  -DCMAKE_PREFIX_PATH=A:/applications/Qt/6.8.1/mingw_64
& 'A:\applications\CLion 2025.3\bin\cmake\win\x64\bin\cmake.exe' `
  --build cmake-build-debug --parallel
```

Run `cmake-build-debug\DiskAnalyzer.exe`. The post-build step copies the
required Qt runtime files next to the executable.

Run the Unicode scanner test with:

```powershell
& 'A:\applications\CLion 2025.3\bin\cmake\win\x64\bin\ctest.exe' `
  --test-dir cmake-build-debug --output-on-failure
```
