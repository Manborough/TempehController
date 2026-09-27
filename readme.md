# TempehController

ESP32 firmware for a tempeh fermentation chamber. It reads a DS18B20 substrate probe and an SHT31 chamber sensor, switches a heating mat through a TP-Link Kasa smart plug and a fan through a MOSFET, classifies fermentation progress, and publishes to Adafruit IO over MQTT.

## Cloning

Arduino requires the sketch folder to have the same name as the `.ino` file, so
clone into a folder named `TempehController`:

```sh
git clone <repository-url> TempehController
cd TempehController
```

Run every command below from this folder.

## Setup

Install [arduino-cli](https://arduino.github.io/arduino-cli/) and the ESP32 core:

```sh
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

Install the libraries:

```sh
arduino-cli lib install OneWire DallasTemperature "Adafruit SHT31 Library" \
  "Adafruit NeoPixel" "Adafruit IO Arduino" ArduinoJson
```

Install [KasaPlug](https://github.com/Manborough/KasaPlug) from GitHub.
Installing from a Git URL must be enabled first:

```sh
arduino-cli config set library.enable_unsafe_install true
arduino-cli lib install --git-url https://github.com/Manborough/KasaPlug.git
```

Validated with ESP32 core 3.3.11 and Adafruit IO Arduino 4.3.7.

## Configuration

Copy the credentials template and fill in your Wi-Fi and Adafruit IO details.
`Secrets.h` is ignored by Git.

```sh
cp Secrets.example.h Secrets.h
```

Set the heating plug's MAC address in `Config.h` (`kHeaterPlugMac`). Pin
assignments and control thresholds are also in `Config.h`.

## Running the tests

The controller and fermentation model are compiled on the PC with a mocked plug
and fan. No ESP32 or plug is needed.

```sh
c++ -std=c++17 -I src/control -I src/hardware src/control/*.cpp \
  tests/control_test.cpp -o /tmp/control_test && /tmp/control_test
```

## Flashing the ESP32

First identify the connected ESP32:

```sh
arduino-cli board list
```

Use the USB serial port shown by that command. On the current machine it is
`/dev/cu.usbserial-0001`, but macOS may assign a different name after reconnecting
the board or moving it to another USB-C port.

Compile and upload the normal production firmware:

```sh
arduino-cli compile --upload \
  --port /dev/cu.usbserial-0001 \
  --fqbn esp32:esp32:esp32 \
  .
```

Compile and upload the demo build, which uses short timings and enables the
serial demo console.

```sh
arduino-cli compile --upload \
  --port /dev/cu.usbserial-0001 \
  --fqbn esp32:esp32:esp32 \
  --build-property compiler.cpp.extra_flags=-DDEMO_MODE=1 \
  .
```

## Serial commands in demo mode

Open the Serial Monitor at baud 115200

| Command | Effect |
|---|---|
| `s 34.5` | force substrate temperature to 34.5 C |
| `c 22.0` | force chamber temperature to 22.0 C |
| `u 90`   | force humidity to 90 % |
| `x`      | clear all overrides, return to real sensors |
| `f`      | toggle simulated sensor failure |
| `r`      | reset heater-off evidence and fermentation state |
| `t 95`   | fast-forward 95 evidence minutes with the current readings |
| `?`      | help |

Open a serial monitor at 115200 baud:

```sh
arduino-cli monitor \
  --port /dev/cu.usbserial-0001 \
  --config baudrate=115200
```
