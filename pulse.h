#pragma once
#include <stdint.h>

// Entprellt den Reed-Kontakt mit einem Integrator statt über Flanken: pro Abtastung
// zählt `level` hoch, solange der Kontakt geschlossen ist, und runter, solange er
// offen ist. Ein Impuls zählt, wenn `level` STABLE_SAMPLES erreicht; offen ist der
// Kontakt erst wieder bei 0. Ein überwiegend geschlossener, prellender Kontakt
// zählt so genau einmal, einzelne Störspitzen und 50-Hz-Brummen nie.
struct PulseDebouncer {
  // ponytail: Abtastungen statt ms, die loop() läuft mit ~1 ms (delay(1)), also
  // ~50 ms netto geschlossen. Bei Fehlzählungen höher, bei verpassten Litern niedriger.
  static constexpr uint32_t STABLE_SAMPLES = 50;

  bool closed = false;   // entprellter Zustand
  bool raw = false;      // letzter Rohwert
  uint32_t level = 0;
  uint32_t edges = 0;    // Rohwert-Wechsel, zeigt Störungen auf der Leitung

  // Mit jedem Rohwert aufrufen (true = Kontakt nach GND). true einmal pro Impuls.
  bool update(bool rawClosed) {
    if (rawClosed != raw) {
      raw = rawClosed;
      edges++;
    }
    if (raw && level < STABLE_SAMPLES) level++;
    if (!raw && level > 0) level--;

    if (!closed && level == STABLE_SAMPLES) {
      closed = true;
      return true;
    }
    if (closed && level == 0) closed = false;
    return false;
  }
};
