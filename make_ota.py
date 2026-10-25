#!/usr/bin/env python3
"""Wraps the compiled sketch into a Zigbee OTA file for Zigbee2MQTT.

    python3 make_ota.py build/water_counter.ino.bin   ->  water_counter.ota

Version, manufacturer code and image type come from water_counter.ino, so the
file always matches what the running firmware asks for. Same layout as
Espressif's image_builder_tool.py: 56-byte ZCL OTA header, then one element
(tag 0x0000 = upgrade image) holding the .bin.
"""
import re
import struct
import sys
from pathlib import Path

here = Path(__file__).parent
sketch = (here / "water_counter.ino").read_text()


def define(name):
    return int(re.search(rf"#define {name}\s+(\w+)", sketch).group(1), 0)


version, manufacturer, image_type = (define(n) for n in ("OTA_FW_VERSION", "OTA_MANUFACTURER", "OTA_IMAGE_TYPE"))
image = Path(sys.argv[1]).read_bytes()
element = struct.pack("<HI", 0x0000, len(image)) + image
header_len = 56
header = struct.pack(
    "<IHHHHHIH32sI",
    0x0BEEF11E,  # OTA file identifier
    0x0100,  # header version
    header_len,
    0,  # field control: no optional fields
    manufacturer,
    image_type,
    version,
    0x0002,  # Zigbee PRO stack
    b"Zehner Water Meter",
    header_len + len(element),
)
out = here / "water_counter.ota"
out.write_bytes(header + element)
print(f"{out}: version {version}, {len(image)} bytes firmware")
