# AgriPV Smart Display & Charging Station ☀️🔋

Dieses Projekt ist eine Kollaboration zwischen der **Fakultät 04 (Elektrotechnik/Informationstechnik)** und der **Fakultät 12 (Design)** der **Hochschule München**.
Ziel ist es, eine AgriPV-Anlage durch ein interaktives Matrix-Display und Wireless-Charging-Pads aufzuwerten, um Erneuerbare Energien (Photovoltaik) für Nutzer direkt erlebbar zu machen.

---

## 🏗 Hardware Architektur

Das System basiert auf einer modularen Hardware-Plattform:

*   **ESP32-S3-R8**: Leistungsstarker Mikrocontroller mit integriertem WiFi und genug PSRAM für flüssige Grafiken.
*   **HUB75 LED Matrix**: Ein 192x64 Pixel Display (gebildet aus drei 64x64 Panels), das über DMA angesteuert wird.
*   **INA219 Stromsensor**: Überwacht den Energiefluss zu den Wireless Charging Pads, um Ladevorgänge ("Auflegen eines Smartphones") in Echtzeit zu erkennen.
*   **Fast Charging Pads**: Induktive Ladestationen, die über die PV-Anlage gespeist werden.

## 💻 Software Architektur

Die Code-Basis ist modular in C++ (PlatformIO) aufgebaut, um Wartbarkeit und Performance zu gewährleisten.

### Datenfluss
1.  **MQTT (Mosquitto)**: Das System empfängt Echtzeit-Daten (PV-Leistung, Batterie-SoC, Verbrauch) direkt vom Raspberry Pi der PV-Anlage.
2.  **API Integration**: Wetterdaten werden regelmäßig per REST-API von OpenWeatherMap abgerufen.
3.  **Zeitsynchronisation**: NTP-Server-Abgleich für präzise Uhrzeit-Anzeige.

### Code-Struktur
Der Code wurde in logische Module unterteilt:

*   **`src/main.cpp`**: Zentrale Steuerung (Setup, Loop, MQTT-Callbacks). Verwaltet die State-Machine des Displays.
*   **`src/system_state.h`**: Eine zentrale Datenstruktur (`struct SystemState`), die alle globalen Systemzustände (Sensordaten, Helligkeit, Animationen) hält.
*   **`src/display_renderer.cpp`**: Kapselt die gesamte Zeichenlogik. Hier sind Funktionen wie `drawBattery`, `drawSun`, oder `fadeOut`/`fadeIn` implementiert.

### Features
*   **Smart Transitions**: Weiche Fade-Effekte beim Wechseln der Anzeige-Modi.
*   **Auto-Brightness**: Automatische Dimmung des Displays in den Nachtstunden (22:00 - 05:00).
*   **Charging Detection**: Intelligente Erkennung von Ladevorgängen durch Überwachung des Stromanstiegs ("Smart Baseline"), inklusive visueller Lade-Animation.
*   **Visuals**: Hochwertige Typografie (IBM Plex Mono), animierte Icons (Sonne) und dynamische SoC-Visualisierung.

---

## 🚀 Getting Started

### Voraussetzungen
*   **PlatformIO** (VS Code Plugin)
*   **ESP32-S3 Board Support Package**

### Installation
1.  Repository klonen
2.  `secrets.h` erstellen (siehe `secrets.example.h`) und WLAN/MQTT- und API-Credentials eintragen.
3.  Projekt via PlatformIO kompilieren und uploaden.

---
*Hochschule München - FK04 & FK12*
