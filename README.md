# Zigbee Water Counter

An ESP32-H2 that reads the pulse output of a water meter and reports it to Zigbee2MQTT / Home Assistant as a Zigbee router.

It reports:

| Property          | Unit  | Access     | Description                                   |
|-------------------|-------|------------|-----------------------------------------------|
| `flow`            | L/min | read       | Current flow, worked out from the time between pulses |
| `water_total`     | L     | read       | Total consumption, kept in flash across reboots |
| `report_interval` | s     | read/write | How often to report over Zigbee (5–3600 s, default 30) |

## Hardware

- ESP32-H2 board
- Water meter with a potential-free pulse contact (reed), **1 pulse = 1 litre**
- Pulse contact between **GPIO 10** and **GND**, with 470 Ω in series, a 10 kΩ pull-up to 3V3 and 100 nF to GND at the pin (see below). GPIO 12 was used first and got damaged, most likely by a surge over the cable.
- **GPIO 13**: LED that lights while the contact is closed (handy for wiring checks)

Pulses are debounced with an integrator (`pulse.h`): a counter goes up while the contact reads closed and down while it reads open. A pulse counts when it reaches 50 samples (about 50 ms), and the contact is open again only when it is back at 0. A bouncing contact that is mostly closed counts once; short spikes and 50 Hz mains hum never count. 3000 L/h is ~1.2 s per litre, so there is plenty of room.

The internal pull-up (~45 kΩ) is too weak for a cable to the meter, and the bare input has no protection. The 470 Ω limits surge current into the pin, the 10 kΩ pull-up keeps hum and leakage from pulling it down, and the 100 nF filters spikes. With these values, a closed contact gives at most ~0.7 V at the pin even if the sensor has up to 2.2 kΩ built in, safely below the H2's LOW threshold (~0.8 V).

The serial status line shows `Stoerflanken`, the number of raw level changes, and `zu`, how many ms the contact read closed since the last line. With no water flowing, both should be 0.

Test: `g++ -std=c++17 -I. test/pulse_test.cpp -o /tmp/pulse_test && /tmp/pulse_test`

## Firmware

1. Install the [Arduino ESP32 core](https://github.com/espressif/arduino-esp32) (3.x, with Zigbee support).
2. In the Arduino IDE, select:
   - Board: **ESP32H2 Dev Module**
   - Zigbee mode: **Zigbee ZCZR (coordinator/router)**
   - Partition scheme: **Zigbee ZCZR 4MB with spiffs**
   - USB CDC On Boot: **Enabled** (otherwise the serial output goes to the UART pins, not USB)
3. Set `START_LITERS` in `water_counter.ino` to the current reading of your meter.
4. Flash the board.

The firmware takes over `START_LITERS` whenever it differs from the value it took over last time. To correct the total later, put in the new reading and flash again (USB or OTA). Setting the same value twice does nothing, so change it by 1 in that case.

On first boot the firmware also clears the Zigbee network data once, so it joins cleanly. After that, pair it again in Zigbee2MQTT.

## Zigbee2MQTT

`ha_converter.js` is an external converter for Zigbee2MQTT 2.x.

1. Copy it to `<z2m data dir>/external_converters/`.
2. Restart Zigbee2MQTT.
3. Allow joining and power on the ESP32-H2.

The device shows up as `Zenner Water Meter` (firmware 2 and older: `Zehner Water Meter`), and Home Assistant picks up its entities through MQTT discovery.

## Firmware update over Zigbee (OTA)

The board has no Wi-Fi, so updates go through Zigbee2MQTT. The first firmware with OTA support (version 2) has to be flashed over USB. After that:

1. Raise `OTA_FW_VERSION` in `water_counter.ino`.
2. Build and wrap it:
   ```sh
   arduino-cli compile --fqbn "esp32:esp32:esp32h2:ZigbeeMode=zczr,PartitionScheme=zigbee_zczr,CDCOnBoot=cdc" --output-dir build .
   python3 make_ota.py build/water_counter.ino.bin    # writes water_counter.ota
   ```
3. Copy `water_counter.ota` into the Zigbee2MQTT data dir (next to `configuration.yaml`). Once, also put `ota_index.json` there with `[{"url": "water_counter.ota"}]`, and enter `ota_index.json` under Settings → OTA updates → *Zigbee OTA override index location* in the Zigbee2MQTT frontend. Don't edit `configuration.yaml` by hand while Zigbee2MQTT runs: it writes its own settings back and drops the change.
4. If the device was paired with a firmware older than 2, run *Interview* on it once. Zigbee2MQTT only learns about the OTA cluster that way; otherwise the check fails with "No endpoint found with OTA cluster support".
5. On the OTA page, check the water meter for updates, then use *Update device firmware*.

From this Mac, `./ota.sh` does steps 2–5 in one go: it builds, uploads the `.ota` over SSH and asks Zigbee2MQTT to update (through Home Assistant's `mqtt.publish`). One-time setup: the Terminal & SSH add-on with the Mac's `~/.ssh/id_ed25519.pub` in `authorized_keys` and port 22 enabled, and a Home Assistant long-lived access token in `.env` as `HA_TOKEN=…` (git ignores `.env`).

The transfer takes about an hour: Zigbee2MQTT sends 50-byte blocks with at least 250 ms between them. The board keeps counting during it, then reboots into the new firmware; the total is kept. A broken image that can no longer join Zigbee has to be fixed over USB.

For the Home Assistant Energy dashboard, make a template sensor from `water_total` with `device_class: water` and `state_class: total_increasing`.

## License

MIT, see [LICENSE](LICENSE).
