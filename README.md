# AsthmaGuard

A wrist-worn device that detects a likely asthma attack (cough sounds + heart rate + SpO2), alerts the patient, delivers one inhaler puff after the patient raises their arm **and** confirms with the button, and escalates to family by SMS + GPS through a paired phone app.

> **Student prototype, not a certified medical device.** The patient must still carry their normal inhaler.

| Folder | What |
|---|---|
| `device/` | Raspberry Pi Zero 2 W, one C++ program (`asthmaguardd`, systemd service). Main loop: sensors, detector, state machine, BLE (BlueZ). Audio thread: I2S microphone + cough model (TensorFlow Lite Micro). |
| `ml/` | Cough model training (Kaggle kernel), int8 export, feature-parity tests. See `ml/DATASETS.md`. Trained model + model card: [huggingface.co/baraa00109/asthmaguard-cough](https://huggingface.co/baraa00109/asthmaguard-cough) (int8 test accuracy 97.1%). |
| `app/` | Flutter app (Arabic-first): live view, history, episode charts, doctor area (PIN, dose limits, PDF), emergency SMS. |
| `docs/` | `PLAN.md` (spec), `REPORT.md` (diagrams, wiring, results, limitations). |

## Hardware (Raspberry Pi Zero 2 W, USB powered)

| Part | Zero 2 W pin (BCM GPIO) |
|---|---|
| MAX30102 (HR/SpO2) + MPU6050 (motion), I2C | SDA GPIO2 (pin 3), SCL GPIO3 (pin 5), 3V3 pin 1 |
| INMP441 microphone, I2S | SCK GPIO18 (pin 12), WS GPIO19 (pin 35), SD GPIO20 (pin 38), L/R to GND |
| Inhaler servo (hardware PWM) | signal GPIO12 (pin 32), 5 V pin 2, 470 µF across 5 V/GND |
| Buzzer / vibration motor (via 2N2222) | GPIO23 (pin 16) / GPIO24 (pin 18) |
| Button to GND | GPIO17 (pin 11) |

Power: a 5 V ≥ 2.5 A USB supply into the PWR IN port. No battery, so the app shows no battery level.

## Build & test

```sh
# device program + tests (all 13 scenarios of docs/PLAN.md §8, model golden test)
# needs: build-essential cmake pkg-config libsystemd-dev libasound2-dev
cmake -S device -B build && cmake --build build -j && ctest --test-dir build

# on the Raspberry Pi Zero 2 W (Raspberry Pi OS Bookworm): build, install, enable
sudo sh device/deploy/setup.sh          # then reboot once (boot config)
sudo asthmaguardd --selftest            # sensors + model check (stop the service first)
sudo nc -U /run/asthmaguard/console     # HELP, STATUS, VERBOSE:1, SELFTEST, SIM:...

# feature parity C++ vs numpy + data pipeline smoke test
python -m pytest ml/tests

# app
cd app && flutter test && flutter build apk --release
```

CI (`.github/workflows/ci.yml`) runs all of the above and uploads the APK and the arm64 `asthmaguardd` binary as artifacts.

## Safety rules built in

- Dosing is never automatic on arm-raise alone: arm raise **and** a button press are required.
- Dosing stays disabled until the doctor sets both dose limits in the app.
- Readings are used only when the PPG signal passes its quality check and the wrist was still for 5 s.
- A 5 s button hold at any time sends a manual SOS.

## License

Code: MIT (`LICENSE`). Model weights: CC BY-NC 4.0, because the training data includes ESC-50 (non-commercial).

## Third-party code

`device/third_party/tflm` is TensorFlow Lite Micro (Apache 2.0), generated from tensorflow/tflite-micro. `device/third_party` also holds ArduinoJson and Unity (MIT). `app/assets/fonts` is Amiri (SIL OFL 1.1).
