
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <time.h>
#include <HTTPClient.h> // NEU: Für den Wetter-Abruf
#include <ArduinoJson.h> // NEU: Zum Verstehen der Wetter-Daten



// 1. WETTER EINSTELLUNGEN (München Maxvorstadt)

String openWeatherMapApiKey = "008007c24f86988c633c6b98d67bcaa3"; // key

// Koordinaten für München Maxvorstadt
String lat = "48.148";
String lon = "11.568";


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
const char *password = "KJ01YTCX2iaIRhgciNK713cI"; 

// MQTT-Einstellungen 
const char *mqtt_server = "10.144.1.1"; //lokale IP RPI Pi
const int mqtt_port = 1883;

//Zohair
//  NTP (Uhrzeit) Einstellungen
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;     // CET = UTC+1 Stunde (Standardzeit Deutschland)
const int   daylightOffset_sec = 3600; // Sommerzeit = +1 weitere Stunde
// Zohair


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

// Wetterdaten (Startwert -99 bedeutet: noch keine Daten)
float g_temperatur = -99.0;

//  Timer für Display-Aktualisierung 
unsigned long lastDisplayUpdate = 0;
const long displayUpdateInterval = 500; // Display alle 500ms neu zeichnen

//Zohair
//Globale Variablen für die Sonnen-Animation

unsigned long letzteSonneAnimation = 0; //Speichert den Zeitpunkt der letzten Animationsänderung
const long sonneAnimationIntervall = 2000; // 2000ms = 2 Sekunden //legt fest, wie lange wir warten bis näcshte stufe
int sonnenStrahlStatus = 0; // 0 = keine Strahlen, 1-3 = wachsende Strahlen
const int MAX_STRAHL_STATUS = 3; // Wir wollen 3 Wachstums-Stufen


unsigned long letztesWetterUpdate = 0;
const long wetterUpdateInterval = 900000; // 15 Minuten (in Millisekunden)
// ENDE Zohair

// FUNKTION: WETTER ABRUFEN
// ------------------------------------------------------
void holeWetter() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // Die Adresse bauen, wo wir das Wetter abfragen
    String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + lat + "&lon=" + lon + "&units=metric&appid=" + openWeatherMapApiKey;
    
    // Anfrage starten
    http.begin(url);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      String payload = http.getString(); // Die Antwort vom Server
      
      // Die Antwort "verstehen" (JSON parsen)
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        float temp = doc["main"]["temp"]; // Temperatur auslesen
        g_temperatur = temp;              // Temperatur speichern
        Serial.print("Neues Wetter: ");
        Serial.println(g_temperatur);
      } else {
        Serial.print("JSON Fehler: ");
        Serial.println(error.c_str());
      }
    } else {
      Serial.print("Fehler beim Internet-Abruf: ");
      Serial.println(httpResponseCode);
    }
    http.end(); // Verbindung schließen
  }
}



/**
 * @brief Diese Funktion wird jedes Mal aufgerufen, wenn eine 
 * Nachricht auf einem abonnierten Topic eintrifft.
 */
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  // Payload in einen String umwandeln
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  float value = message.toFloat();

  // Daten 
  if (strcmp(topic, "Saftkiste/battery_voltage") == 0) {
    g_batt_voltage = value;
  } else if (strcmp(topic, "Saftkiste/battery_current") == 0) {
    g_batt_current = value;
  } else if (strcmp(topic, "Saftkiste/battery_state_of_charge") == 0) {
    g_batt_soc = value;
  } else if (strcmp(topic, "chargeController/1/voltage") == 0) {
    g_pv1_voltage = value;
  } else if (strcmp(topic, "chargeController/2/voltage") == 0) {
    g_pv2_voltage = value;
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

//Zoahir
/**
 * @brief Zeichnet ein Akku-Symbol oben rechts auf das Display.
 * Der Füllstand und die Farbe basieren auf g_batt_soc.
 */
void zeichneAkku() {
  
  // 1. Definiere Farben
  uint16_t weiss = dma_display->color565(255, 255, 255);
  uint16_t rot   = dma_display->color565(255, 0, 0);
  uint16_t gelb  = dma_display->color565(255, 255, 0);
  uint16_t gruen = dma_display->color565(0, 255, 0);
  uint16_t schwarz = 0; // 0 ist immer schwarz

  // 2. Definiere Position und Größe des Akkus
  // (0,0) ist oben links, (63, 63) ist unten rechts
  int batt_breite = 10; // Breite des Akku-Körpers
  int batt_hoehe  = 18; // Höhe des Akku-Körpers
  int batt_x = 51;      // X-Position (oben rechts)
  int batt_y = 4;       // Y-Position (oben)

  // 3. Bestimme Füllfarbe basierend auf g_batt_soc (0.0 - 100.0)
  uint16_t fuellFarbe;
  if (g_batt_soc <= 20.0) {
    fuellFarbe = rot;
  } else if (g_batt_soc <= 60.0) {
    fuellFarbe = gelb;
  } else {
    fuellFarbe = gruen;
  }
  
  // Wenn g_batt_soc noch -1 ist (keine Daten empfangen),
  // zeichne nur den leeren Akku
  if (g_batt_soc < 0.0) {
    fuellFarbe = schwarz; // Innen leer
  }

  // 4. Zeichne das weiße Gehäuse
  // Der "Pluspol" oben (ein gefülltes Rechteck)
  dma_display->fillRect(batt_x + 3, batt_y - 2, 4, 2, weiss);
  // Der Akku-Körper (nur der Umriss)
  dma_display->drawRect(batt_x, batt_y, batt_breite, batt_hoehe, weiss);

  // 5. Zeichne die Füllung
  // Die Füllung ist 1 Pixel kleiner als das Gehäuse (innen)
  int innen_x = batt_x + 1;
  int innen_y = batt_y + 1;
  int innen_breite = batt_breite - 2;
  int maxInnenHoehe = batt_hoehe - 2; // Die Höhe bei 100%

  // Berechne die aktuelle Füllhöhe (von 0 bis maxInnenHoehe)
  int aktuelleFuellHoehe = (g_batt_soc / 100.0) * maxInnenHoehe;

  // Stelle sicher, dass die Höhe nicht über das Maximum geht
  if (aktuelleFuellHoehe > maxInnenHoehe) {
      aktuelleFuellHoehe = maxInnenHoehe;
  }
  if (aktuelleFuellHoehe < 0) {
      aktuelleFuellHoehe = 0;
  }

  // Berechne den Y-Startpunkt für das Füllen von UNTEN nach OBEN
  // (Ein fillRect fängt immer oben an zu zeichnen)
  int y_start_fuellung = innen_y + (maxInnenHoehe - aktuelleFuellHoehe);

  // Zuerst den leeren (schwarzen) Teil zeichnen
  dma_display->fillRect(innen_x, innen_y, innen_breite, maxInnenHoehe - aktuelleFuellHoehe, schwarz);
  
  // Dann den gefüllten Teil darüber zeichnen
  dma_display->fillRect(innen_x, y_start_fuellung, innen_breite, aktuelleFuellHoehe, fuellFarbe);
}
// Zoahir

void zeichneUhrzeit() { // Unten Links
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return;
  char timeStr[6]; 
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);

  dma_display->setTextSize(2); 
  dma_display->setTextColor(dma_display->color565(255, 255, 255)); 
  dma_display->setCursor(2, 45); 
  dma_display->print(timeStr);
}


// Zohair
/**
 * @brief Zeichnet die Sonne und ihre Strahlen
 * @param strahlStatus 0 = keine Strahlen, 1-3 = wachsende Strahlen
 */



 void zeichneUhrzeit() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    // Zeit ist noch nicht synchronisiert
    dma_display->setCursor(10, 40);
    dma_display->setTextSize(1);
    dma_display->setTextColor(dma_display->color565(255, 0, 0)); // Rot
    dma_display->print("Zeit...");
    return;
  }

  // Zeit in einen String formatieren (z.B. "14:35")
  char timeString[6]; // HH:MM + Null-Terminierer
  strftime(timeString, sizeof(timeString), "%H:%M", &timeinfo);

  // Text-Eigenschaften
  dma_display->setTextSize(2); // "emnisschen größer"
  dma_display->setTextColor(dma_display->color565(255, 255, 255)); // Weiß

  // Position "mitte (x) und embisschen unten (y)"
  // TextSize 2 ist 10px breit pro Zeichen (5*2). "14:35" = 5 Zeichen = 50px
  // Mitte X = (64 - 50) / 2 = 7
  int x_pos = 7; 
  int y_pos = 45; // "embisschen unten"

  dma_display->setCursor(x_pos, y_pos);
  dma_display->print(timeString);
}


// NEU: Wetter-Funktion hier einfügen, damit sie existiert!
void zeichneWetter() {
  if (g_temperatur == -99.0) return; // Noch keine Daten

  uint16_t farbe = dma_display->color565(255, 255, 255); 
  if (g_temperatur < 10) farbe = dma_display->color565(0, 100, 255); 
  if (g_temperatur > 25) farbe = dma_display->color565(255, 100, 0); 

  dma_display->setTextSize(1);
  dma_display->setTextColor(farbe);
  dma_display->setCursor(32, 52); 
  dma_display->print((int)g_temperatur); 
  dma_display->write(247); // Grad-Zeichen
  dma_display->print("C"); 
}

void zeichneSonne(int strahlStatus) {


// Die Mitte ist jetzt OBEN LINKS
int centerX = 20; // Vorher 32
int centerY = 20; // Vorher 32

// Radius ist ca. 1/3 kleiner
int sonnenRadius = 7; // Vorher 10

uint16_t sonnenFarbe = dma_display->color565(255, 255, 0); 

// 1. Die gelbe Kugel (immer sichtbar)
dma_display->fillCircle(centerX, centerY, sonnenRadius, sonnenFarbe);

// 2. Die Strahlen (nur zeichnen, wenn der Status > 0 ist)
if (strahlStatus > 0) {
  
  // Strahlenlänge ist ca. 1/3 kleiner
  int strahlLaenge = strahlStatus * 4; // Vorher strahlStatus * 5
  int strahlStartOffset = sonnenRadius + 2; // Startet 2px außerhalb der Kugel

  for (int i = 0; i < 6; i++) {
    float winkel = (i * 60.0) * (M_PI / 180.0); 

    // Startpunkt (am Rand der Kugel)
    int x_start = centerX + cos(winkel) * strahlStartOffset;
    int y_start = centerY + sin(winkel) * strahlStartOffset;

    // Endpunkt (wächst mit dem Status)
    int x_end = centerX + cos(winkel) * (strahlStartOffset + strahlLaenge);
    int y_end = centerY + sin(winkel) * (strahlStartOffset + strahlLaenge);

    dma_display->drawLine(x_start, y_start, x_end, y_end, sonnenFarbe);
    }
  }
}
//Zohair


/**
 * @brief Zeichnet die aktuellen Daten auf das Display
 */
void updateDisplay()
{
  // Bildschirm komplett löschen
    dma_display->fillScreen(0);


//Zohair
// Wir rufen unsere neue Funktion auf und übergeben ihr
    // den aktuellen Animations-Status
    zeichneSonne(sonnenStrahlStatus);
    //akku auffüllen
    zeichneAkku();
    //Zohair
    zeichneUhrzeit();
    zeichneWetter();
    /*
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
    */

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


  //Zohair
   //Uhrzeit-Synchronisierung starten
  Serial.println("Synchronisiere Uhrzeit via NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  //Zohair 


  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);


  // Wetter sofort beim Start holen
  holeWetter();

  // Initiale Anzeige (wird in loop() aktualisiert)
  updateDisplay();

  // Lösche den Bildschirm (fülle ihn mit Schwarz)
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
  dma_display->print("~");
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



  // Zohair
 
  unsigned long currentMillis_Sonne = millis();
  
// --- NEU: WETTER UPDATE (alle 15 Min) ---
if (jetzt - letztesWetterUpdate >= wetterUpdateInterval) {
  letztesWetterUpdate = jetzt;
  holeWetter(); // Wetter neu abrufen
}


  if (currentMillis_Sonne - letzteSonneAnimation >= sonneAnimationIntervall)
  {
    // Es ist Zeit für den nächsten Schritt
    letzteSonneAnimation = currentMillis_Sonne;

    sonnenStrahlStatus++; // Nächste Wachstumsstufe

    // Wenn Status größer als max (3), zurück auf 0 (Strahlen verschwinden)
    if (sonnenStrahlStatus > MAX_STRAHL_STATUS) {
      sonnenStrahlStatus = 0; 
    }
  }
  // Zohair

  // 3. Display nur alle X Millisekunden aktualisieren (verhindert Flimmern)
  unsigned long currentMillis = millis();
  if (currentMillis - lastDisplayUpdate >= displayUpdateInterval)
  {
    lastDisplayUpdate = currentMillis;

    // Neue Funktion zum Zeichnen der Livedaten aufrufen
    updateDisplay();}

}