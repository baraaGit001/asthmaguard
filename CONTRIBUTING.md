# Contributing

Issues and pull requests are welcome.

- Device logic changes: add or update a test in `device/tests/test_logic.cpp` and run `cmake -S device -B build && cmake --build build -j && ctest --test-dir build`. Every scenario of `docs/PLAN.md` §8 must still pass.
- Feature (log-mel) changes: `device/src/core/sensors/features.cpp` is shared by the device and training. Run `python -m pytest ml/tests`, and retrain the model afterwards (the old model expects the old features).
- App changes: `flutter analyze` must be clean and `flutter test` must pass.
- Safety rules are not negotiable: no dosing on arm-raise alone, no dosing until the doctor sets both limits, invalid PPG readings are never used for decisions.
- New training data: add it to `ml/DATASETS.md` with its source and license. Only openly licensed data.
