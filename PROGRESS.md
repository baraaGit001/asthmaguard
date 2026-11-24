# AsthmaGuard — progress

Spec: docs/PLAN.md. This file names the next task.

## Done
- **Phase 2** core logic (pure C++, host-tested): config (JSON, range-checked), detector rules, cough counter, state machine, event log, simulation mode, button gestures, activity + arm-raise detector, PPG HR/SpO2 algorithm. `pio test -e native`: all 13 section-8 scenarios + unit tests pass.
- **Phase 1/3** code written: MPU6050 (register driver), MAX30102 (SparkFun) + own PPG algorithm, actuators (non-blocking patterns, servo puff), BLE GATT service on the Arduino-Pico `BLE` library, serial console (`HELP`, `VERBOSE:1` bring-up print), main.cpp core split. Not yet compiled for the board (framework download).
- **Phase 4** features: firmware/src/sensors/features.cpp is compiled into the Python pipeline (ctypes) and matches an independent numpy implementation (ml/tests). Training kernel pushed to Kaggle (`<kaggle-user>/asthmaguard-cough-train`, see ml/make_kernel.py).
- **Phase 5/6** Flutter app (app/): onboarding, pairing, home, history, episode charts, doctor area (PIN, dose limits, thresholds, baselines, PDF), settings, hidden simulation screen (tap the version line 5x), emergency flow (GPS + SMS + ACK), baseline calibration, foreground service. `flutter analyze` clean, `flutter test` passes.

## Decisions / deviations
- FFT: portable radix-2 in features.cpp instead of CMSIS-DSP, so the identical file runs on the Pico and in training.
- Extra event types: DOSE_BLOCKED (dose refused: limits not set / episode limit / interval / servo timeout), EMERGENCY_CANCELLED (2-5 s press during EMERGENCY), BOOT.
- Button: SHORT < 2 s, LONG = released between 2 and 5 s (cancel), SOS fires at 5 s while held.
- SPO2_CRITICAL: an invalid second (wrist moving) resets the 20 s run. A long-press cannot cancel an alert once an emergency was sent.
- Emergency re-sent at most every 10 min per episode (reescalate_s) so a persistent low SpO2 does not spam SMS.
- Phone sends SMS only for emergencies < 10 min old (older ones arrive after a long disconnect: logged, not sent).
- APK is built on GitHub Actions (Android build tools do not run on the aarch64 server).

## Next
- Compile firmware for pico2w, fix build errors, check RAM/flash.
- Fetch Kaggle model output, copy model_data.* + golden.h into firmware/lib/model, report into ml/.
