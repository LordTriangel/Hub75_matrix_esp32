#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>

//  Definiere deine Panel-Größe
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 64
#define PANELS_CHAINED 1

//  Erstelle das Haupt-Konfigurationsobjekt
// Die Pins werden in setup() konfiguriert
HUB75_I2S_CFG mxconfig(
    PANEL_WIDTH,
    PANEL_HEIGHT,
    PANELS_CHAINED);

// Deklariere das Display-Objekt global
// Es wird in setup() initialisiert
MatrixPanel_I2S_DMA *dma_display = nullptr;

// WLAN-Einstellungen (aus Doku)
const char *ssid = "HM-iotroam";
const char *password = "R3T1_PV4HM_ESP32!"; 

// MQTT-Einstellungen 
const char *mqtt_server = "10.19.68.2";//"10.144.1.1";127.0.0.1 //lokale IP RPI Pi
const int mqtt_port = 1883;

//  Globale Objekte für WLAN/MQTT 
WiFiClient espClient;
PubSubClient client(espClient);

// Globale Variablen zum Speichern der Livedaten 
// (Mit -1 initialisiert, um "keine Daten" zu erkennen)
float g_batt_voltage = -1.0;
float g_batt_current = -1.0;
float g_batt_soc = -1.0;
float g_pv1_voltage = -1.0;
float g_pv2_voltage = -1.0;

//  Timer für Display-Aktualisierung 
unsigned long lastDisplayUpdate = 0;
const long displayUpdateInterval = 500; // Display alle 500ms neu zeichnen


/**
 * @brief Diese Funktion wird jedes Mal aufgerufen, wenn eine 
 * Nachricht auf einem abonnierten Topic eintrifft.
 */
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Nachricht erhalten von Topic: ");
  // Payload in einen String umwandeln
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  float value = message.toFloat();

  // Daten 
  if (strcmp(topic, "Saftkiste/battery_voltage") == 0) {
    Serial.println("Saftkiste/battery_voltage");
    g_batt_voltage = value;
  } else if (strcmp(topic, "Saftkiste/battery_current") == 0) {
    Serial.println("Saftkiste/battery_current");
    g_batt_current = value;
  } else if (strcmp(topic, "Saftkiste/battery_state_of_charge") == 0) {
    Serial.println("Saftkiste/battery_state_of_charge");
    g_batt_soc = value;
  } else if (strcmp(topic, "chargeController/1/voltage") == 0) {
    Serial.println("chargeController/1/voltage");
    g_pv1_voltage = value;
  } else if (strcmp(topic, "chargeController/2/voltage") == 0) {
    Serial.println("chargeController/2/voltage");
    g_pv2_voltage = value;
  } else {
    Serial.print("Unbekanntes Topic: ");
    Serial.println(topic);
  }
  if (isnan(value)) {
    Serial.println("Konvertierungsfehler: Payload ist keine Zahl!");
  }
  // (Weitere Topics wie .../current können hier hinzugefügt werden)
}

/**
 * @brief Stellt die Verbindung zum MQTT-Broker her (oder wieder her)
 */
void reconnect()
{
  while (!client.connected())
  {
    Serial.print("Versuche MQTT-Verbindung...");
    dma_display->fillScreen(0);
    dma_display->setCursor(1, 20);
    dma_display->print("MQTT conn...");

    // Client-ID kann beliebig sein. 
    // Doku sagt "allow_anonymous true", 
    // daher kein User/Pass nötig.
    if (client.connect("esp32MatrixClient"))
    {
      Serial.println("verbunden!");
      dma_display->print("OK!");
      delay(500);

      // Topics abonnieren 
      client.subscribe("Saftkiste/#");
      client.subscribe("chargeController/#");
    }
    else
    {
      Serial.print("fehlgeschlagen, rc=");
      Serial.print(client.state());
      Serial.println(" Erneuter Versuch in 5 Sek.");
      dma_display->print("FAIL");
      delay(5000);
    }
  }
}

/**
 * @brief Stellt die Verbindung zum WLAN her
 */
void setup_wifi()
{
  Serial.println("Lese MAC-Adresse...");

  // Die Funktion WiFi.macAddress() gibt die Adresse direkt als String zurück
  String mac = WiFi.macAddress();
  
  Serial.print("Meine MAC-Adresse ist: ");
  Serial.println(mac);
  Serial.print("Verbinde mit ");
  Serial.println(ssid);
  dma_display->fillScreen(0);
  dma_display->setTextSize(1);
  dma_display->setTextColor(dma_display->color565(0, 150, 255));
  dma_display->setCursor(1, 5);
  dma_display->print("Netzwerk:");
  dma_display->setCursor(1, 15);
  dma_display->print(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    dma_display->print(".");
  }

  Serial.println("\nWLAN verbunden!");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());
  dma_display->setCursor(1, 30);
  dma_display->print("OK!");
  dma_display->setCursor(1, 40);
  dma_display->print(WiFi.localIP());

  delay(1500); // Kurz IP anzeigen
}
/**
 * @brief Zeichnet die aktuellen Daten auf das Display
 */
void updateDisplay()
{
    dma_display->fillScreen(0);
    dma_display->setTextSize(1);

    // --- Saftkiste ---
    dma_display->setTextColor(dma_display->color565(0, 255, 100)); // Grün
    dma_display->setCursor(1, 2);
    dma_display->print("SOC:");
    dma_display->setCursor(28, 2);
    dma_display->print(g_batt_soc, 1); // 1 Nachkommastelle
    dma_display->print("%");

    dma_display->setTextColor(dma_display->color565(0, 200, 255)); // Cyan
    dma_display->setCursor(1, 12);
    dma_display->print("Batt:");
    dma_display->setCursor(28, 12);
    dma_display->print(g_batt_voltage, 2); // 2 Nachkommastellen
    dma_display->print("V");

    dma_display->setCursor(1, 22);
    dma_display->print("Last:");
    dma_display->setCursor(28, 22);
    dma_display->print(g_batt_current, 1);
    dma_display->print("A");

    // --- PV-Laderegler ---    
    dma_display->setTextColor(dma_display->color565(255, 255, 0)); // Gelb
    dma_display->setCursor(1, 36);
    dma_display->print("PV1:");
    dma_display->setCursor(28, 36);
    dma_display->print(g_pv1_voltage, 1);
    dma_display->print("V");
  
    dma_display->setCursor(1, 46);
    dma_display->print("PV2:");
    dma_display->setCursor(28, 46);
    dma_display->print(g_pv2_voltage, 1);
    dma_display->print("V");
}
// 5. Setup-Funktion
void setup()
{
  Serial.begin(115200);
  delay(100);

  // 3. Pin Belegung YD-ESP32-S3
  // Daten-Block 
  mxconfig.gpio.r1 = 4;
  mxconfig.gpio.g1 = 5;
  mxconfig.gpio.b1 = 6;
  mxconfig.gpio.r2 = 7;
  mxconfig.gpio.g2 = 15;
  mxconfig.gpio.b2 = 16;
  mxconfig.gpio.clk = 41;
  mxconfig.gpio.lat = 40;
  // Adress-Block
  mxconfig.gpio.a = 18;
  mxconfig.gpio.b = 8;
  mxconfig.gpio.c = 3;
  mxconfig.gpio.d = 11;
  mxconfig.gpio.e = 9; // Wichtig für 64x64
  mxconfig.gpio.oe = 10;

  // Erstelle das Display-Objekt mit deiner eigenen Konfiguration
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);

  // Starte das Display
  dma_display->begin();

  // Stelle eine moderate Helligkeit ein (0-255)
  dma_display->setBrightness(50);

  setup_wifi(); // Stellt Verbindung zum WLAN her
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  // Initiale Anzeige (wird in loop() aktualisiert)
  updateDisplay();

  // Lösche den Bildschirm (fülle ihn mit Schwarz)

  Serial.println("Initialwerte:");
  Serial.print("g_batt_voltage: ");
  Serial.println(g_batt_voltage);
  Serial.print("g_batt_current: ");
  Serial.println(g_batt_current);
  Serial.print("g_batt_soc: ");
  Serial.println(g_batt_soc);
  Serial.println("Initialisierung abgeschlossen.");

  dma_display->fillScreen(0);


  // Setze die Textgröße (1 = 5x8 Pixel Schriftart)
  dma_display->setTextSize(1);

  // Setze die Textfarbe auf ein helles Grün
  dma_display->setTextColor(dma_display->color565(50, 255, 100));

  // Setze den "Cursor", wo der Text beginnen soll (x, y)
  dma_display->setCursor(0, 0);

  // Schreibe den Text!
  dma_display->print("Hello World!");
  dma_display->setTextColor(dma_display->color565(255, 100, 5)); // Rot
  dma_display->setCursor(5, 20);                               // Neue Position
  dma_display->print("ESP32-S3");
  dma_display->setTextColor(dma_display->color565(100, 100, 255)); // Blau
  dma_display->setCursor(5, 40);                               // Neue Position
  dma_display->print("Team RETI");
  dma_display->setTextColor(dma_display->color565(255, 255, 255)); // Weiss
  dma_display->setCursor(5, 60);                               // Neue Position 
  dma_display->print("~~~~~~~~~~~");
}

//Loop-Funktion
void loop()
{
 // 1. MQTT-Verbindung prüfen und ggf. wiederherstellen
  if (!client.connected())
  {
    reconnect();
  }
  // 2. WICHTIG: MQTT-Client-Schleife am Laufen halten
  client.loop();

  // 3. Display nur alle X Millisekunden aktualisieren (verhindert Flimmern)
  unsigned long currentMillis = millis();
  if (currentMillis - lastDisplayUpdate >= displayUpdateInterval)
  {
    lastDisplayUpdate = currentMillis;

    // Neue Funktion zum Zeichnen der Livedaten aufrufen
    updateDisplay();
  }
   Serial.print("g_batt_voltage: ");
   Serial.println(g_batt_voltage);
   Serial.print("g_batt_current: ");
   Serial.println(g_batt_current);
   Serial.print("g_batt_soc: ");
   Serial.println(g_batt_soc);
   Serial.print("g_pv1_voltage: ");
   Serial.println(g_pv1_voltage);

   delay(1000);
}
