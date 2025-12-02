#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starte...");

  if (psramInit()) {
    if (ESP.getPsramSize() > 0) {
      Serial.println("PSRAM ist verfügbar!");
      Serial.printf("Verfügbares PSRAM: %d bytes\n", ESP.getPsramSize());
      Serial.printf("Gesamter Heap: %d bytes\n", ESP.getHeapSize());
    } else {
      Serial.println("PSRAM wurde nicht gefunden!");
    }
  } else {
    Serial.println("PSRAM konnte nicht initialisiert werden!");
  }
  
  // ... hier kommt dein Matrix-Setup-Code hin
}

void loop() {
  // dein Code
}

