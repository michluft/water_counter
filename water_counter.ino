#include <Arduino.h>

#ifndef ZIGBEE_MODE_ZCZR
#error "Bitte Zigbee ZCZR (Coordinator/Router) auswählen!"
#endif

#include "Zigbee.h"
#include <Preferences.h>
#include "pulse.h"

#define FLOW_ENDPOINT     10  // Durchflussrate in L/min
#define TOTAL_ENDPOINT    11  // aufsummierter Gesamtverbrauch in L
#define INTERVAL_ENDPOINT 12  // Reportintervall in Sekunden

static const int PULSE_PIN = 10;  // GPIO 12 ist auf diesem Board defekt (liest LOW bei 3 V)
static const int LED_PIN = 13;  // blaue User-LED, an, solange der Kontakt geschlossen ist

static const uint32_t REPORT_INTERVAL_MIN_S = 5;
static const uint32_t REPORT_INTERVAL_MAX_S = 3600;
static const uint32_t REPORT_INTERVAL_DEFAULT_S = 30;

// Wird übernommen, sobald es sich vom zuletzt übernommenen Wert unterscheidet:
// Zählerstand vom Wasserzähler ablesen, hier eintragen, flashen (USB oder OTA).
// ponytail: denselben Wert ein zweites Mal setzen geht nicht, dann um 1 ändern.
static const uint32_t START_LITERS = 5269;

// Zigbee-OTA: OTA_FW_VERSION bei jedem Update erhöhen, sonst bietet Z2M es nicht
// an. make_ota.py liest diese drei Werte von hier. Eigener Image-Typ, damit das
// Image nie einem anderen ESP32-Gerät mit den Beispiel-Defaults angeboten wird.
#define OTA_FW_VERSION   3
#define OTA_MANUFACTURER 0x1001
#define OTA_IMAGE_TYPE   0x5743
static const uint32_t FLOW_STALE_MIN_MS = 5000;
static const uint32_t FLOW_STALE_MAX_MS = 120000;

ZigbeeAnalog flowSensor(FLOW_ENDPOINT);
ZigbeeAnalog totalMeter(TOTAL_ENDPOINT);
ZigbeeAnalog intervalOut(INTERVAL_ENDPOINT);
Preferences prefs;

PulseDebouncer pulse;
uint32_t pulseCount = 0;
uint32_t lastPulseMs = 0;
uint32_t lastIntervalMs = 0;
uint32_t lastSavedCount = 0;
uint32_t closedSamples = 0;  // Abtastungen mit geschlossenem Kontakt seit der letzten Ausgabe
volatile uint32_t reportIntervalS = REPORT_INTERVAL_DEFAULT_S;
volatile uint32_t lastReportMs = 0;
volatile bool intervalDirty = false;

uint32_t clampReportInterval(uint32_t seconds) {
  if (seconds < REPORT_INTERVAL_MIN_S) return REPORT_INTERVAL_MIN_S;
  if (seconds > REPORT_INTERVAL_MAX_S) return REPORT_INTERVAL_MAX_S;
  return seconds;
}

void onReportIntervalChange(float seconds) {
  uint32_t next = clampReportInterval((uint32_t)(seconds + 0.5f));
  if (next == reportIntervalS) return;
  reportIntervalS = next;
  lastReportMs = millis();  // neue Frequenz gilt ab jetzt, ohne Restart
  intervalDirty = true;
  Serial.print("Reportintervall: ");
  Serial.print(next);
  Serial.println(" s");
}

float flowFromInterval(uint32_t now, uint32_t last, uint32_t interval) {
  if (last == 0 || interval == 0 || interval > FLOW_STALE_MAX_MS) return 0;

  uint32_t stale = interval * 2;
  if (stale < FLOW_STALE_MIN_MS) stale = FLOW_STALE_MIN_MS;
  if (stale > FLOW_STALE_MAX_MS) stale = FLOW_STALE_MAX_MS;
  if (now - last > stale) return 0;

  return 60000.0f / interval;
}

void publishZigbee(float flowLmin, uint32_t count) {
  flowSensor.setAnalogInput(flowLmin);
  totalMeter.setAnalogInput((float)count);
  flowSensor.reportAnalogInput();
  totalMeter.reportAnalogInput();
}

void setup() {
  Serial.begin(115200);
  // Nicht warten, wenn am USB niemand mitliest: sonst blockiert jedes print bis zu
  // 2 s, loop() steht und Impulse gehen verloren.
  Serial.setTxTimeoutMs(0);
  delay(1000);

  Serial.println();
  Serial.println("ESP32-H2 Zigbee Wasserzaehler");

  prefs.begin("water", false);
  if (prefs.getUInt("start", 0) != START_LITERS) {
    prefs.putUInt("total", START_LITERS);
    prefs.putUInt("start", START_LITERS);
    Serial.println("Neuer Startwert uebernommen.");
  }
  pulseCount = prefs.getUInt("total", START_LITERS);
  lastSavedCount = pulseCount;

  reportIntervalS = clampReportInterval(prefs.getUInt("rpt_s", REPORT_INTERVAL_DEFAULT_S));

  Serial.print("Gesamtstand: ");
  Serial.print(pulseCount);
  Serial.print(" L, Report: ");
  Serial.print(reportIntervalS);
  Serial.print(" s, Firmware ");
  Serial.println(OTA_FW_VERSION);

  pinMode(PULSE_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  // Ein beim Start schon geschlossener Kontakt zählt nicht noch einmal.
  pulse.closed = pulse.raw = digitalRead(PULSE_PIN) == LOW;
  if (pulse.closed) pulse.level = PulseDebouncer::STABLE_SAMPLES;

  flowSensor.addAnalogInput();
  flowSensor.setAnalogInputDescription("FlowLmin");
  flowSensor.setAnalogInputMinMax(0.0, 100.0);
  flowSensor.setAnalogInputResolution(0.01);

  totalMeter.addAnalogInput();
  totalMeter.setAnalogInputDescription("WaterTotalL");
  totalMeter.setAnalogInputMinMax(0.0, 1000000.0);
  totalMeter.setAnalogInputResolution(1.0);

  intervalOut.addAnalogOutput();
  intervalOut.setAnalogOutputDescription("ReportIntervalS");
  intervalOut.setAnalogOutputMinMax((float)REPORT_INTERVAL_MIN_S, (float)REPORT_INTERVAL_MAX_S);
  intervalOut.setAnalogOutputResolution(1.0);
  intervalOut.onAnalogOutputChange(onReportIntervalChange);

  flowSensor.setManufacturerAndModel("Michael", "Zenner Water Meter");
  flowSensor.addOTAClient(OTA_FW_VERSION, OTA_FW_VERSION + 1, 1, OTA_MANUFACTURER, OTA_IMAGE_TYPE);

  Zigbee.addEndpoint(&flowSensor);
  Zigbee.addEndpoint(&totalMeter);
  Zigbee.addEndpoint(&intervalOut);

  Serial.println("Starte Zigbee Router...");

  // Alte Reporting-Config im Zigbee-NVS crasht beim Join — vor dem Start loeschen.
  // Wasserzaehler-Stand in Preferences bleibt. Danach in Z2M neu anlernen.
  bool wipeZigbee = !prefs.getBool("zb_wipe", false);
  if (wipeZigbee) {
    prefs.putBool("zb_wipe", true);
    Serial.println("Loesche Zigbee-NVS. Danach in Z2M neu anlernen.");
  }

  if (!Zigbee.begin(ZIGBEE_ROUTER, wipeZigbee)) {
    Serial.println("Fehler beim Starten von Zigbee!");
    ESP.restart();
  }

  // Kein Warten aufs Netz: gezählt wird auch, solange der Koordinator fehlt.
  Serial.println("Zigbee gestartet, warte auf Netzwerk...");
}

void printStatus(float flowLmin, uint32_t count) {
  Serial.print("Flow: ");
  Serial.print(flowLmin);
  Serial.print(" L/min, Zaehler: ");
  Serial.print(count);
  Serial.print(" L, Stoerflanken: ");
  Serial.print(pulse.edges);
  Serial.print(", zu: ");
  Serial.print(closedSamples);
  Serial.println(" ms");
  pulse.edges = 0;
  closedSamples = 0;
}

void loop() {
  static bool joined = false;
  if (!joined && Zigbee.connected()) {
    joined = true;
    Serial.println("Mit Zigbee-Netzwerk verbunden!");
    intervalOut.setAnalogOutput((float)reportIntervalS);
    flowSensor.requestOTAUpdate();
  }

  // Rohsignal, nicht entprellt: LED an, solange der Kontakt nach GND zieht.
  bool raw = digitalRead(PULSE_PIN) == LOW;
  digitalWrite(LED_PIN, raw ? HIGH : LOW);
  closedSamples += raw;

  uint32_t now = millis();
  if (pulse.update(raw)) {
    if (lastPulseMs != 0) lastIntervalMs = now - lastPulseMs;
    lastPulseMs = now;
    pulseCount++;
  }

  uint32_t count = pulseCount;
  float flowLmin = flowFromInterval(now, lastPulseMs, lastIntervalMs);
  bool incremented = count != lastSavedCount;
  bool dueReport = (now - lastReportMs) >= reportIntervalS * 1000UL;

  if (incremented) {
    prefs.putUInt("total", count);
    lastSavedCount = count;
  }

  if (intervalDirty) {
    intervalDirty = false;
    prefs.putUInt("rpt_s", reportIntervalS);
  }

  if (incremented || dueReport) {
    printStatus(flowLmin, count);
  }

  if (dueReport) {
    lastReportMs = now;
    if (joined) publishZigbee(flowLmin, count);
  }

  delay(1);
}
