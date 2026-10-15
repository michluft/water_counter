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
- Pulse contact between **GPIO 12** and **GND** (uses the internal pull-up)
- **GPIO 13**: LED that lights while the contact is closed (handy for wiring checks)

Pulses less than 250 ms apart are ignored as bounce. That still lets through ~240 L/min, well above a typical 3000 L/h house meter.

## Firmware

1. Install the [Arduino ESP32 core](https://github.com/espressif/arduino-esp32) (3.x, with Zigbee support).
2. In the Arduino IDE, select:
   - Board: **ESP32H2 Dev Module**
   - Zigbee mode: **Zigbee ZCZR (coordinator/router)**
   - Partition scheme: **Zigbee ZCZR 4MB with spiffs**
3. Set `START_LITERS` in `water_counter.ino` to the current reading of your meter.
4. Flash the board.

`START_LITERS` is only used when no total is stored in flash yet. To set it again, erase flash before upload (*Tools → Erase All Flash Before Sketch Upload*).

On first boot the firmware also clears the Zigbee network data once, so it joins cleanly. After that, pair it again in Zigbee2MQTT.

## Zigbee2MQTT

`ha_converter.js` is an external converter for Zigbee2MQTT 2.x.

1. Copy it to `<z2m data dir>/external_converters/`.
2. Restart Zigbee2MQTT.
3. Allow joining and power on the ESP32-H2.

The device shows up as `Zehner Water Meter`, and Home Assistant picks up its entities through MQTT discovery.

For the Home Assistant Energy dashboard, make a template sensor from `water_total` with `device_class: water` and `state_class: total_increasing`.

## License

MIT, see [LICENSE](LICENSE).
