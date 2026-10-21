# Developer Guide

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure    # 1 of 1 pass on this snapshot
```

Tests live in `Tests/` (`EngineTests.cpp`, `DrumMachine.h`, `TestFramework.h`). CI: `.github/workflows/ci.yml`. The plugin targets are planned, not in this build.
