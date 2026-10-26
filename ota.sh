#!/bin/sh
# Builds the firmware, uploads it to Zigbee2MQTT and starts the update over Zigbee.
# Raise OTA_FW_VERSION in water_counter.ino first. Needs .env with HA_TOKEN and the
# Terminal & SSH add-on with this Mac's key (see README).
set -e
cd "$(dirname "$0")"
. ./.env

HA=192.168.178.12
DEVICE=0x744dbdfffe65cf0e  # IEEE address, survives renames in Zigbee2MQTT
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"

"$CLI" compile --fqbn esp32:esp32:esp32h2:ZigbeeMode=zczr,PartitionScheme=zigbee_zczr,CDCOnBoot=cdc --output-dir build .
python3 make_ota.py build/water_counter.ino.bin
ssh root@$HA 'cat > /homeassistant/zigbee2mqtt/water_counter.ota' < water_counter.ota

api() { curl -sf -H "Authorization: Bearer $HA_TOKEN" -H "Content-Type: application/json" "http://$HA:8123/api/$@"; }
api services/mqtt/publish -d "{\"topic\": \"zigbee2mqtt/bridge/request/device/ota_update/update\", \"payload\": \"{\\\"id\\\": \\\"$DEVICE\\\"}\"}" >/dev/null

# Zigbee2MQTT first asks the device for its version, which can take up to a minute.
echo "Waiting for the update to start..."
for i in $(seq 30); do
  sleep 5
  if api states/update.gartenwasserzahler | grep -q '"in_progress": *true'; then
    echo "Update running (about 1 h). Progress: Zigbee2MQTT OTA page or update.gartenwasserzahler in HA."
    exit 0
  fi
done
echo "Update did not start: version not raised, or device unreachable? See the Zigbee2MQTT log." >&2
exit 1
