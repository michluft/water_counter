#include <Arduino.h>

#ifndef ZIGBEE_MODE_ZCZR
#error "Bitte Zigbee ZCZR (Coordinator/Router) auswählen!"
#endif

#include "Zigbee.h"
#include <Preferences.h>

#define FLOW_ENDPOINT     10  // Durchflussrate in L/min
#define TOTAL_ENDPOINT    11  // aufsummierter Gesamtverbrauch in L
#define INTERVAL_ENDPOINT 12  // Reportintervall in Sekunden

static const int PULSE_PIN = 12;
static const int LED_PIN = 13;  // blaue User-LED, folgt dem Rohsignal (LOW = Kontakt)

static const uint32_t REPORT_INTERVAL_MIN_S = 5;
static const uint32_t REPORT_INTERVAL_MAX_S = 3600;
static const uint32_t REPORT_INTERVAL_DEFAULT_S = 30;

// Wird nur übernommen, wenn NVS noch 0 ist (Erststart / nach Wipe).
// Danach gilt der gespeicherte Stand. Zum erneuten Setzen NVS löschen.
static const uint32_t START_LITERS = 2372;

// 3000 L/h = 50 Impulse/min = min. 1200 ms Abstand. Bounce und Stoerimpulse
// liegen weit darunter; 250 ms laesst den Nenn-Maximaldurchfluss durch.
static const uint32_t DEBOUNCE_MS = 250;
static const uint32_t FLOW_STALE_MIN_MS = 5000;
static const uint32_t FLOW_STALE_MAX_MS = 120000;

ZigbeeAnalog flowSensor(FLOW_ENDPOINT);
ZigbeeAnalog totalMeter(TOTAL_ENDPOINT);
ZigbeeAnalog intervalOut(INTERVAL_ENDPOINT);
Preferences prefs;

volatile uint32_t pulseCount = 0;
volatile uint32_t lastPulseMs = 0;
volatile uint32_t lastIntervalMs = 0;
uint32_t lastSavedCount = 0;
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

void IRAM_ATTR onPulse() {
  uint32_t now = millis();
  uint32_t prev = lastPulseMs;
  uint32_t dt = now - prev;
  if (prev != 0 && dt < DEBOUNCE_MS) return;
  if (prev != 0) lastIntervalMs = dt;
  lastPulseMs = now;
  pulseCount++;
}

float flowFromInterval(uint32_t now, uint32_t last, uint32_t interval) {
  if (last == 0 || interval < DEBOUNCE_MS || interval > FLOW_STALE_MAX_MS) return 0;

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
  delay(1000);

  Serial.println();
  Serial.println("ESP32-H2 Zigbee Wasserzaehler");

  prefs.begin("water", false);
  uint32_t stored = prefs.getUInt("total", 0);
  pulseCount = (stored == 0) ? START_LITERS : stored;
  lastSavedCount = pulseCount;
  if (stored == 0 && START_LITERS != 0) {
    prefs.putUInt("total", START_LITERS);
  }

  reportIntervalS = clampReportInterval(prefs.getUInt("rpt_s", REPORT_INTERVAL_DEFAULT_S));

  Serial.print("Gesamtstand: ");
  Serial.print(pulseCount);
  Serial.print(" L, Report: ");
  Serial.print(reportIntervalS);
  Serial.println(" s");

  pinMode(PULSE_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  attachInterrupt(digitalPinToInterrupt(PULSE_PIN), onPulse, FALLING);

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

  flowSensor.setManufacturerAndModel("Michael", "Zehner Water Meter");

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

  Serial.println("Zigbee gestartet.");
  Serial.println("Warte auf Zigbee-Netzwerk...");

  while (!Zigbee.connected()) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Mit Zigbee-Netzwerk verbunden!");

  intervalOut.setAnalogOutput((float)reportIntervalS);
}

void printStatus(float flowLmin, uint32_t count) {
  Serial.print("Flow: ");
  Serial.print(flowLmin);
  Serial.print(" L/min, Zaehler: ");
  Serial.print(count);
  Serial.println(" L");
}

void loop() {
  // Rohsignal, nicht entprellt: LED an, solange der Kontakt nach GND zieht.
  digitalWrite(LED_PIN, digitalRead(PULSE_PIN) == LOW ? HIGH : LOW);

  noInterrupts();
  uint32_t count = pulseCount;
  uint32_t last = lastPulseMs;
  uint32_t interval = lastIntervalMs;
  interrupts();

  float flowLmin = flowFromInterval(millis(), last, interval);
  bool incremented = count != lastSavedCount;
  uint32_t now = millis();
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
    publishZigbee(flowLmin, count);
  }

  delay(1);
}
