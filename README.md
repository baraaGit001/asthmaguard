# AsthmaGuard

A wrist-worn device that detects a likely asthma attack (cough sounds + heart rate + SpO2), alerts the patient, delivers one inhaler puff after the patient raises their arm **and** confirms with the button, and escalates to family by SMS + GPS through a paired phone app.

> **Student prototype, not a certified medical device.** The patient must still carry their normal inhaler.

| Folder | What |
|---|---|
| `firmware/` | Raspberry Pi Pico 2 W, C++ (Arduino-Pico core, PlatformIO). Core 0: sensors, detector, state machine, BLE. Core 1: microphone + cough model. |
| `ml/` | Cough model training (Kaggle kernel), int8 export, feature-parity tests. See `ml/DATASETS.md`. |
| `app/` | Flutter app (Arabic-first): live view, history, episode charts, doctor area (PIN, dose limits, PDF), emergency SMS. |
| `docs/` | `PLAN.md` (spec), `REPORT.md` (diagrams, wiring, results, limitations). |

## Build & test

```sh
# firmware logic tests on the computer (all 13 scenarios of docs/PLAN.md §8)
cd firmware && pio test -e native
# firmware for the board (.pio/build/pico2w/firmware.uf2)
pio run -e pico2w
# serial console: HELP, STATUS, VERBOSE:1, SELFTEST, SIM:...

# feature parity C++ vs numpy + data pipeline smoke test
python -m pytest ml/tests

# app
cd app && flutter test && flutter build apk --release
```

CI (`.github/workflows/ci.yml`) runs all of the above and uploads the APK and firmware `.uf2` as artifacts.

## Safety rules built in

- Dosing is never automatic on arm-raise alone: arm raise **and** a button press are required.
- Dosing stays disabled until the doctor sets both dose limits in the app.
- Readings are used only when the PPG signal passes its quality check and the wrist was still for 5 s.
- A 5 s button hold at any time sends a manual SOS.

## Third-party code

`firmware/lib/tflm` is TensorFlow Lite Micro (Apache 2.0) from tensorflow/tflite-micro-arduino-examples. `app/assets/fonts` is Amiri (SIL OFL 1.1).
