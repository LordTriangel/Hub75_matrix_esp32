#include <Arduino.h> //hallo
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <time.h>
#include "bitmap.h"      // Unsere neue Header-Datei für die Bitmap
#include <HTTPClient.h>  // NEU: Für den Wetter-Abruf
#include <ArduinoJson.h> // NEU: Zum Verstehen der Wetter-Daten
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <AnimatedGIF.h> // NEU: GIF Library
#include <LittleFS.h>    // NEU: Dateisystem
#include "secrets.h"     // Credentials

// 1. WETTER EINSTELLUNGEN (München Maxvorstadt)

String openWeatherMapApiKey = OWM_API_KEY; // key

// Koordinaten für München Maxvorstadt
String lat = "48.148";
String lon = "11.568";

//  Definiere deine Panel-Größe
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 64
#define PANELS_CHAINED 3 // Beibehalten der bestehenden Konfiguration (2 Panels)

//  Erstelle das Haupt-Konfigurationsobjekt
// Die Pins werden in setup() konfiguriert
HUB75_I2S_CFG mxconfig(
    PANEL_WIDTH,
    PANEL_HEIGHT,
    PANELS_CHAINED);

// Deklariere das Display-Objekt global
// Es wird in setup() initialisiert
MatrixPanel_I2S_DMA *dma_display = nullptr;

// WLAN-Einstellungen
//#define DEBUG_NETWORK // Uncomment for home testing

#ifdef DEBUG_NETWORK
  const char *ssid = WIFI_SSID_HOME;
  const char *password = WIFI_PASS_HOME;
#else
  const char *ssid = WIFI_SSID_WORK;
  const char *password = WIFI_PASS_WORK;
#endif

// MQTT-Einstellungen
const char *mqtt_server = MQTT_SERVER_IP;
const int mqtt_port = 1883;

// Zohair
//   NTP (Uhrzeit) Einstellungen
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;     // CET = UTC+1 Stunde (Standardzeit Deutschland)
const int daylightOffset_sec = 3600; // Sommerzeit = +1 weitere Stunde
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
float g_batt_power = -1.0;

// Wetterdaten (Startwert -99 bedeutet: noch keine Daten)
float g_temperatur = -99.0;

// INA219 Sensor
Adafruit_INA219 ina219;
int w_chg_cnt = 0;              // Anzahl der ladenden Geräte (Wireless Charging)
float current_mA_prev = 0;      // Vorheriger Stromwert für Sprungerkennung
unsigned long lastInaUpdate = 0;// Timer für INA Check

// GIF Animation
AnimatedGIF gif;
const char *gifFilename = "/warp_chg_192x64.gif"; // Datei muss im 'data' Ordner liegen

//  Timer für Display-Aktualisierung
unsigned long lastDisplayUpdate = 0;
const long displayUpdateInterval = 500; // Display alle 100ms neu zeichnen

// Zohair
// Globale Variablen für die Sonnen-Animation

unsigned long letzteSonneAnimation = 0;   // Speichert den Zeitpunkt der letzten Animationsänderung
const long sonneAnimationIntervall = 100; // 2000ms = 2 Sekunden //legt fest, wie lange wir warten bis näcshte stufe
int sonnenStrahlStatus = 0;               // 0 = keine Strahlen, 1-3 = wachsende Strahlen
const int MAX_STRAHL_STATUS = 3;          // Wir wollen 3 Wachstums-Stufen
// wetter
unsigned long letztesWetterUpdate = 0;
const long wetterUpdateInterval = 900000; // 15 Minuten (in Millisekunden)

// ENDE Zohair

// Timer für MQTT Reconnect (Non-blocking)
unsigned long lastMqttReconnectAttempt = 0;

// State Machine Definitions
enum DisplayState {
  STATE_MAIN,      // Standard view (Sun, Battery, etc.)
  STATE_LOGO,      // HM Logo
  STATE_FULLSCREEN,// New processed data view (Slideshow)
  STATE_CHARGING_ANIMATION // NEU: Lade-Animation
};

DisplayState currentState = STATE_MAIN;
DisplayState previousState = STATE_MAIN; // Um zurückzukehren
unsigned long stateTimer = 0;

// Durations (in milliseconds)
const unsigned long DURATION_MAIN = 10000;      // 10 seconds
const unsigned long DURATION_LOGO = 2000;       // 2 seconds
const unsigned long DURATION_FULLSCREEN = 5000; // 5 seconds
const unsigned long DURATION_CHARGING = 5000;   // 5 Sekunden GIF abspielen

// --- Speicher für 7-Tage-PV-Historie ---
// Wir legen einen Pointer an. Der Speicher wird in setup() im PSRAM reserviert.
const int HISTORY_POINTS_PER_DAY = 288; // Alle 5 Minuten ein Wert (24 * 60 / 5)
const int HISTORY_DAYS = 7;
const int TOTAL_HISTORY_POINTS = HISTORY_POINTS_PER_DAY * HISTORY_DAYS;
float *g_pv_power_history = nullptr; // Pointer für unser PSRAM-Array

// FUNKTION: WETTER ABRUFEN
// ------------------------------------------------------
void holeWetter()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;

    // Die Adresse bauen, wo wir das Wetter abfragen
    String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + lat + "&lon=" + lon + "&units=metric&appid=" + openWeatherMapApiKey;

    // Anfrage starten
    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
      String payload = http.getString(); // Die Antwort vom Server

      // Die Antwort "verstehen" (JSON parsen)
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);

      if (!error)
      {
        float temp = doc["main"]["temp"]; // Temperatur auslesen
        g_temperatur = temp;              // Temperatur speichern
        Serial.print("Neues Wetter: ");
        Serial.println(g_temperatur);
      }
      else
      {
        Serial.print("JSON Fehler: ");
        Serial.println(error.c_str());
      }
    }
    else
    {
      Serial.print("Fehler beim Internet-Abruf: ");
      Serial.println(httpResponseCode);
    }
    http.end(); // Verbindung schließen
  }
}

// CALLBACK: Datei-Funktionen für LittleFS (Brücke für AnimatedGIF Library)
// -------------------------------------------------------------------------
void *GIFOpenFile(const char *fname, int32_t *pSize)
{
  File f = LittleFS.open(fname);
  if (f)
  {
    *pSize = f.size();
    // Trick: Wir geben neuen Speicher zurück, der das File-Objekt hält.
    // Sauberer wäre ein "new File(f)", aber File ist oft nur ein Wrapper.
    // Wir nutzen hier einfach new File Kopie.
    File *fnew = new File(f); 
    return (void *)fnew;
  }
  return NULL;
}

void GIFCloseFile(void *pHandle)
{
  File *f = (File *)pHandle;
  if (f)
  {
    f->close();
    delete f;
  }
}

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
  int32_t iBytesRead;
  iBytesRead = iLen;
  File *f = (File *)pFile->fHandle;
  // Note: If you read a file all the way to the last byte, seek() stops working
  if ((pFile->iSize - pFile->iPos) < iLen)
    iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
  if (iBytesRead <= 0)
    return 0;
  iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
  pFile->iPos = f->position();
  return iBytesRead;
}

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{
  File *f = (File *)pFile->fHandle;
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}
// -------------------------------------------------------------------------

// CALLBACK: GIF Zeichnen
// Wird von der Library für jeden Pixel-Block aufgerufen
void GIFDraw(GIFDRAW *pDraw)
{
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[320];
  int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > PANEL_WIDTH * PANELS_CHAINED)
    iWidth = PANEL_WIDTH * PANELS_CHAINED;

  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y; // current line

  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) // restore to background color
  {
    for (x = 0; x < iWidth; x++)
    {
      if (s[x] == pDraw->ucTransparent)
        s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }
  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) // if transparency used
  {
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int x, iCount;
    pEnd = s + pDraw->iWidth;
    x = 0;
    iCount = 0; // count non-transparent pixels
    while (x < pDraw->iWidth)
    {
      c = ucTransparent - 1;
      d = usTemp;
      while (c != ucTransparent && s < pEnd)
      {
        c = *s++;
        if (c == ucTransparent) // done, stop
        {
          s--; // back up to treat it like transparent
        }
        else // opaque
        {
          *d++ = usPalette[c];
          iCount++;
        }
      }           // while looking for opaque block
      if (iCount) // any opaque pixels?
      {
        for (int i = 0; i < iCount; i++)
        {
          dma_display->drawPixel(x + i, y, usTemp[i]); // <--- Aufs Display zeichnen!
        }
        x += iCount;
        iCount = 0;
      }
      // no, look for a run of transparent pixels
      c = ucTransparent;
      while (c == ucTransparent && s < pEnd)
      {
        c = *s++;
        if (c == ucTransparent)
          x++;
        else
          s--;
      }
    }
  }
  else
  {
    s = pDraw->pPixels;
    // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
    for (x = 0; x < pDraw->iWidth; x++)
    {
      dma_display->drawPixel(x, y, usPalette[*s++]);
    }
  }
}

// FUNKTION: Wireless Charging Check (INA219)
// ------------------------------------------------------
void checkWirelessChargingCurrent() {
  // Aktuellen Strom in mA lesen
  float current_mA = ina219.getCurrent_mA();
  
  // Differenz zum letzten Wert
  float diff = current_mA - current_mA_prev;

  bool triggerAnimation = false;

  // Debug Ausgabe
  // Serial.print("Strom: "); Serial.print(current_mA); Serial.print(" mA | Diff: "); Serial.println(diff);

  // Schwelle für "Sprung" festlegen (1 Ampere = 1000mA)
  // Positive Flanke (Gerät aufgelegt)
  if (diff >= 1000.0) {
    w_chg_cnt++;
    Serial.print("Sprung nach OBEN erkannt! Neuer Count: ");
    Serial.println(w_chg_cnt);
    triggerAnimation = true;
  }
  // Negative Flanke (Gerät weggenommen)
  // Wir prüfen auf <= -1000, damit auch ein Abfall erkannt wird
  else if (diff <= -1000.0) {
    w_chg_cnt--;
    if (w_chg_cnt < 0) w_chg_cnt = 0; // Nicht unter 0 gehen
    Serial.print("Sprung nach UNTEN erkannt! Neuer Count: ");
    Serial.println(w_chg_cnt);
    triggerAnimation = true; // Auch beim Wegnehmen Animation zeigen? (Optional)
    // Wenn NUR beim Auflegen: if (diff >= 1000.0) triggerAnimation = true; else triggerAnimation = false;
  }

  // Animation starten, wenn was passiert ist UND wir nicht schon animieren
  if (triggerAnimation && currentState != STATE_CHARGING_ANIMATION) {
     previousState = currentState; // Merken wo wir waren
     currentState = STATE_CHARGING_ANIMATION;
     stateTimer = millis();
     // GIF öffnen mit unseren neuen Callback-Funktionen
     if (gif.open(gifFilename, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw))
     {
        Serial.println("GIF gestartet!");
        dma_display->fillScreen(0);
     } else {
        Serial.println("Fehler: Konnte GIF nicht öffnen! (Liegt es in /data/warp_chg_192x64.gif?)");
        // Fallback: Einfach im aktuellen State bleiben oder nur Text zeigen
        currentState = previousState; 
     }
  }

  // Aktuellen Wert als "Vorherigen" für das nächste Mal speichern
  current_mA_prev = current_mA;
}

/**
 * @brief Diese Funktion wird jedes Mal aufgerufen, wenn eine
 * Nachricht auf einem abonnierten Topic eintrifft.
 */
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  // Payload in einen String umwandeln
  String message;
  for (int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }
  float value = message.toFloat();

  // Daten
  if (strcmp(topic, "Saftkiste/battery_voltage") == 0)
  {
    g_batt_voltage = value;
  }
  else if (strcmp(topic, "Saftkiste/battery_current") == 0)
  {
    g_batt_current = value;
  }
  else if (strcmp(topic, "Saftkiste/battery_power") == 0)
  {
    g_batt_power = value;
  }
  else if (strcmp(topic, "Saftkiste/battery_state_of_charge") == 0)
  {
    g_batt_soc = value;
  }
  else if (strcmp(topic, "chargeController/1/voltage") == 0)
  {
    g_pv1_voltage = value;
  }
  else if (strcmp(topic, "chargeController/2/voltage") == 0)
  {
    g_pv2_voltage = value;
  }
  // (Weitere Topics wie .../current können hier hinzugefügt werden)
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

  // Wir warten hier kurz, aber nicht ewig blockierend im Loop später
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20)
  {
    delay(500);
    Serial.print(".");
    dma_display->print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWLAN verbunden!");
    Serial.print("IP-Adresse: ");
    Serial.println(WiFi.localIP());
    dma_display->setCursor(1, 30);
    dma_display->print("OK!");
    dma_display->setCursor(1, 40);
    dma_display->print(WiFi.localIP());
    delay(1500); // Kurz IP anzeigen
  }
  else
  {
    Serial.println("\nWLAN Fehler!");
    dma_display->setCursor(1, 30);
    dma_display->print("Fehler!");
  }
}

// Zoahir
/**
 * @brief Zeichnet ein Akku-Symbol oben rechts auf das Display.
 * Der Füllstand und die Farbe basieren auf g_batt_soc.
 */
void zeichneAkku()
{
  g_batt_soc=100.0;

  // 1. Definiere Farben
  uint16_t weiss = dma_display->color565(255, 255, 255);
  uint16_t rot = dma_display->color565(255, 0, 0);
  uint16_t gelb = dma_display->color565(255, 255, 0);
  uint16_t gruen = dma_display->color565(0, 255, 0);
  uint16_t schwarz = 0; // 0 ist immer schwarz

  // 2. Definiere Position und Größe des Akkus
  // (0,0) ist oben links, (63, 63) ist unten rechts
  int batt_breite = 10; // Breite des Akku-Körpers
  int batt_hoehe = 18;  // Höhe des Akku-Körpers
  int batt_x = 51;      // X-Position (oben rechts)
  int batt_y = 4;       // Y-Position (oben)

  // 3. Bestimme Füllfarbe basierend auf g_batt_soc (0.0 - 100.0)
  uint16_t fuellFarbe;
  if (g_batt_soc <= 20.0)
  {
    fuellFarbe = rot;
  }
  else if (g_batt_soc <= 60.0)
  {
    fuellFarbe = gelb;
  }
  else
  {
    fuellFarbe = gruen;
  }

  // Wenn g_batt_soc noch -1 ist (keine Daten empfangen),
  // zeichne nur den leeren Akku
  if (g_batt_soc < 0.0)
  {
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
  if (aktuelleFuellHoehe > maxInnenHoehe)
  {
    aktuelleFuellHoehe = maxInnenHoehe;
  }
  if (aktuelleFuellHoehe < 0)
  {
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

void zeichneAkku2()
{
  int gesamtBreite = PANEL_WIDTH * PANELS_CHAINED; // 192
  int hoehe = 10; 
  
  // Farben
  uint16_t weiss = dma_display->color565(255, 255, 255);
  uint16_t schwarz = dma_display->color565(0, 0, 0);

  // Sicherheit: Wert begrenzen (0-100)
  float soc = g_batt_soc;
  if (soc < 0) soc = 0; // Falls Sensorfehler (-1), zeige leer
  if (soc > 100) soc = 100;

  // 1. Rahmen zeichnen (über volle Breite)
  // drawRect(x, y, w, h, farbe) -> Nur die Umrandung
  dma_display->drawRect(0, 0, gesamtBreite, hoehe, weiss);

  // 2. Füllung berechnen
  // Wir haben links und rechts je 1 Pixel Rand, also -2 bei der Breite
  int maxFuellBreite = gesamtBreite - 2; 
  int aktuelleBreite = (int)((soc / 100.0) * maxFuellBreite);

  // 3. Den "vollen" Teil weiß ausmalen (von links nach rechts)
  if (aktuelleBreite > 0) {
    // x=1, y=1 (wegen Rahmen), Höhe-2
    dma_display->fillRect(1, 1, aktuelleBreite, hoehe - 2, weiss);
  }

  // 4. Den "leeren" Teil schwarz ausmalen (damit der Rahmen bleibt)
  // Startet dort, wo der weiße Balken aufhört
  int restBreite = maxFuellBreite - aktuelleBreite;
  if (restBreite > 0) {
    dma_display->fillRect(1 + aktuelleBreite, 1, restBreite, hoehe - 2, schwarz);
  }

  // Textfarbe basierend auf Akku-Stand
  // Wir nehmen etwas dunklere Farben für Grün/Gelb, damit man sie 
  // auf dem WEISSEN Hintergrund lesen kann!
  uint16_t textFarbe;
  if (soc > 60.0) {
    // Dunkelgrün (damit lesbar auf Weiß)
    textFarbe = dma_display->color565(0, 150, 0); 
  } else if (soc > 20.0) {
    // Gold/Dunkelgelb
    textFarbe = dma_display->color565(200, 180, 0); 
  } else {
    // Rot (auf Weiß gut lesbar)
    textFarbe = dma_display->color565(255, 0, 0); 
  }

  // Text vorbereiten
  String prozentText = String((int)soc) + "%";
  
  dma_display->setTextSize(1);
  dma_display->setTextColor(textFarbe);

  // Mitte berechnen
  // Ein Zeichen ist ca 6px breit (5px Font + 1px Space)
  // Mitte vom Display ist 192 / 2 = 96
  int textPixelBreite = prozentText.length() * 6;
  int textX = (gesamtBreite / 2) - (textPixelBreite / 2);
  
  // Y-Position: Balken ist 10 hoch, Schrift ist 8 hoch -> Offset 1
  int textY = 1;

  dma_display->setCursor(textX, textY);
  dma_display->print(prozentText);

}

// Zoahir

/**
 * @brief Zeichnet die Uhrzeit
 */
void zeichneUhrzeit()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    // Zeit ist noch nicht synchronisiert
    dma_display->setCursor(5, 40);
    dma_display->setTextSize(1);
    dma_display->setTextColor(dma_display->color565(255, 0, 0)); // Rot
    dma_display->print("Zeit...");
    return;
  }

  // Zeit in einen String formatieren (z.B. "14:35")
  char timeString[6]; // HH:MM + Null-Terminierer
  strftime(timeString, sizeof(timeString), "%H:%M", &timeinfo);

  // Text-Eigenschaften
  dma_display->setTextSize(2);                                     // "emnisschen größer"
  dma_display->setTextColor(dma_display->color565(255, 255, 255)); // Weiß
  
  // Position "mitte (x) und embisschen unten (y)"
  // TextSize 2 ist 10px breit pro Zeichen (5*2). "14:35" = 5 Zeichen = 50px
  // Mitte X = (64 - 50) / 2 = 7
  int x_pos = 4;
  int y_pos = 45; // "embisschen unten"

  dma_display->setCursor(x_pos, y_pos);
  dma_display->print(timeString);
}

// Zohair
/**
 * @brief Zeichnet die Sonne und ihre Strahlen
 * @param strahlStatus 0 = keine Strahlen, 1-3 = wachsende Strahlen
 */
void zeichneSonne(int strahlStatus)
{

  // Die Mitte ist jetzt OBEN LINKS
  int centerX = 20; // Vorher 32
  int centerY = 20; // Vorher 32

  // Radius ist ca. 1/3 kleiner
  int sonnenRadius = 7; // Vorher 10

  uint16_t sonnenFarbe = dma_display->color565(255, 255, 0);

  // 1. Die gelbe Kugel (immer sichtbar)
  dma_display->fillCircle(centerX, centerY, sonnenRadius, sonnenFarbe);

  // 2. Die Strahlen (nur zeichnen, wenn der Status > 0 ist)
  if (strahlStatus > 0)
  {

    // Strahlenlänge ist ca. 1/3 kleiner
    int strahlLaenge = strahlStatus * 4;      // Vorher strahlStatus * 5
    int strahlStartOffset = sonnenRadius + 2; // Startet 2px außerhalb der Kugel

    for (int i = 0; i < 6; i++)
    {
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

// NEU: Wetter-Funktion hier einfügen, damit sie existiert!
void zeichneWetter(int x, int y)
{
  if (g_temperatur == -99.0)
    return; // Noch keine Daten

  uint16_t farbe = dma_display->color565(255, 255, 255);
  if (g_temperatur < 10)
    farbe = dma_display->color565(0, 100, 255);
  if (g_temperatur > 25)
    farbe = dma_display->color565(255, 100, 0);

  dma_display->setTextSize(1);
  dma_display->setTextColor(farbe);
  dma_display->setCursor(x, y);
  dma_display->print((int)round(g_temperatur));
  dma_display->write(247); // Grad-Zeichen
  dma_display->print("C");
}
// Zohair

/**
 * @brief Zeichnet die aktuellen Daten auf das Display
 */
void updateDisplay()
{
  unsigned long jetzt = millis();

  // --- 1. STATE MACHINE TRANSITIONS ---
  switch (currentState) {
    case STATE_MAIN:
      if (jetzt - stateTimer >= DURATION_MAIN) {
        currentState = STATE_LOGO;
        stateTimer = jetzt;
        dma_display->fillScreen(0); // Clear screen on transition
      }
      break;

    case STATE_LOGO:
      if (jetzt - stateTimer >= DURATION_LOGO) {
        currentState = STATE_FULLSCREEN;
        stateTimer = jetzt;
        dma_display->fillScreen(0);
      }
      break;

    case STATE_FULLSCREEN:
      if (jetzt - stateTimer >= DURATION_FULLSCREEN) {
        currentState = STATE_MAIN;
        stateTimer = jetzt;
        dma_display->fillScreen(0);
      }
      break;
  }

  // --- 2. STATE DRAWING ---
  switch (currentState) {
    case STATE_MAIN:
      // Linkes Panel (Grafiken) löschen
      dma_display->fillRect(0, 0, 64, 64, 0);

      // Zohair
      //  Wir rufen unsere neue Funktion auf und übergeben ihr
      //  den aktuellen Animations-Status
      zeichneSonne(sonnenStrahlStatus);
      // akku auffüllen
      //zeichneAkku();
      zeichneAkku2();
      // Zohair
      zeichneUhrzeit();
      // Rechtes Panel (Text) löschen
      dma_display->fillRect(64, 0, 64, 64, 0);
      ///*
      dma_display->setTextSize(1);

      // --- Saftkiste ---
      dma_display->setTextColor(dma_display->color565(0, 255, 100)); // Grün
      dma_display->setCursor(1 + 64, 2);
      dma_display->print("SOC:");
      dma_display->setCursor(28 + 64, 2);
      dma_display->print(g_batt_soc, 1); // 1 Nachkommastelle
      dma_display->print("%");

      dma_display->setTextColor(dma_display->color565(0, 200, 255)); // Cyan
      dma_display->setCursor(1 + 64, 12);
      dma_display->print("Batt:");
      dma_display->setCursor(28 + 64, 12);
      dma_display->print(g_batt_voltage, 2); // 2 Nachkommastellen
      dma_display->print("V");

      dma_display->setCursor(1 + 64, 22);
      dma_display->print("Last:");
      dma_display->setCursor(28 + 64, 22);
      dma_display->print(g_batt_current, 1);
      dma_display->print("A");

      // --- PV-Laderegler ---
      dma_display->setTextColor(dma_display->color565(255, 255, 0)); // Gelb
      dma_display->setCursor(1 + 64, 36);
      dma_display->print("PV1:");
      dma_display->setCursor(28 + 64, 36);
      dma_display->print(g_pv1_voltage, 1);
      dma_display->print("V");

      dma_display->setCursor(1 + 64, 46);
      dma_display->print("PV2:");
      dma_display->setCursor(28 + 64, 46);
      dma_display->print(g_pv2_voltage, 1);
      dma_display->print("V");
      //*/

      // 3tes Panel
      //  3tes Panel (Text) löschen
      dma_display->fillRect(128, 0, 64, 64, 0);
      zeichneWetter(138, 22);
      dma_display->setTextColor(dma_display->color565(255, 200, 200)); //
      // dma_display->setCursor(1+128, 12);
      // dma_display->print("hallo");
      dma_display->setTextSize(2);
      //dma_display->setFont()
      dma_display->setCursor(1 + 128, 0);
      //dma_display->print("༼ つ ◕_◕ ༽つ");

      dma_display->print(int(round(g_batt_power)));
      dma_display->print("W");
      break;

    case STATE_CHARGING_ANIMATION:
      // GIF Frame abspielen
      if (gif.playFrame(true, NULL)) // true = wait if needed (delay handling)
      {
         // Frame wurde gezeichnet.
      }
      else
      {
         // GIF ist am Ende (oder Loop zu Ende)
         // Wir setzen es zurück für den nächsten Loop (falls wir loopen wollen) oder beenden es.
         gif.reset(); 
      }

      // Prüfen ob Zeit abgelaufen ist
      if (jetzt - stateTimer >= DURATION_CHARGING) {
        gif.close(); // Wichtig: Datei schließen
        currentState = previousState; // Zurück zum alten State
        stateTimer = jetzt;
        dma_display->fillScreen(0);
      }
      break;

    case STATE_LOGO:
      dma_display->drawRGBBitmap(32, 0, HMlogo, 128, 64);
      break;

    case STATE_FULLSCREEN:
      // Placeholder for Fullscreen Mode
      // You can implement your slideshow logic here
      dma_display->setTextSize(2);
      dma_display->setTextColor(dma_display->color565(255, 50, 255)); // Magenta
      dma_display->setCursor(2, 0);
      dma_display->print("Energie Generiert heute:");
      dma_display->setCursor(1, 34);
      dma_display->setTextSize(1);
      dma_display->setTextColor(dma_display->color565(167, 165, 232)); //lavendel
      dma_display->print("100 Handyladungen ");
      break;
  }
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
  dma_display->setBrightness(80);

  // --- PSRAM für PV-Historie reservieren ---
  Serial.printf("Reserviere Speicher im PSRAM für %d Datenpunkte...\n", TOTAL_HISTORY_POINTS);
  // ps_malloc stellt sicher, dass der Speicher aus dem PSRAM kommt
  g_pv_power_history = (float *)ps_malloc(TOTAL_HISTORY_POINTS * sizeof(float));

  if (g_pv_power_history == nullptr)
  {
    Serial.println("FEHLER: Konnte keinen Speicher im PSRAM reservieren!");
    // Hier könntest du eine Fehlermeldung auf dem Display ausgeben
  }
  else
  {
    Serial.println("PSRAM-Speicher erfolgreich reserviert.");
    // Optional: Array mit Nullen initialisieren
    memset(g_pv_power_history, 0, TOTAL_HISTORY_POINTS * sizeof(float));
  }
  // --- Ende PSRAM ---

  setup_wifi(); // Stellt Verbindung zum WLAN her

  // Zohair
  // Uhrzeit-Synchronisierung starten
  Serial.println("Synchronisiere Uhrzeit via NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // Wetter sofort beim Start holen
  holeWetter();

  // INA219 Initialisierung
  if (!ina219.begin()) {
    Serial.println("Fehler: INA219 Chip nicht gefunden!");
  } else {
    Serial.println("INA219 Chip gefunden.");
    // Optional: Kalibrierung für höhere Stromstärken, falls nötig
    // ina219.setCalibration_32V_2A(); // Oder _32V_1A(), je nach Shunt
    // Standard ist 32V_2A, was bis 3.2A misst. Für mehr Range Doku prüfen.
    // Wir lesen hier erstmal den ersten Wert, damit der "Sprung" nicht beim Boot passiert
    current_mA_prev = ina219.getCurrent_mA();
  }

  // LittleFS starten
  if (!LittleFS.begin()) {
    Serial.println("LittleFS konnte nicht gestartet werden. Formatiere...");
    LittleFS.format();
    LittleFS.begin();
  }
  
  // GIF vor-initialisieren (Buffer)
  gif.begin(LITTLE_ENDIAN_PIXELS); // Unsere Palette ist schon Little Endian durch color565 logic (meistens)

  // Zohair

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

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
  dma_display->setCursor(5, 20);                                 // Neue Position
  dma_display->print("ESP32-S3");
  dma_display->setTextColor(dma_display->color565(100, 100, 255)); // Blau
  dma_display->setCursor(5, 40);                                   // Neue Position
  dma_display->print("Team RETI");
  dma_display->setTextColor(dma_display->color565(255, 255, 255)); // Weiss
  dma_display->setCursor(5, 60);                                   // Neue Position
  dma_display->print("~");
}

// Loop-Funktion
void loop()
{
  // 1. MQTT-Verbindung prüfen und ggf. wiederherstellen (Non-blocking)
  // WICHTIG: Nicht während der Animation versuchen zu verbinden, da das blockieren kann!
  if (currentState != STATE_CHARGING_ANIMATION && !client.connected())
  {
    unsigned long now = millis();
    // Alle 5 Sekunden versuchen
    if (now - lastMqttReconnectAttempt > 5000)
    {
      lastMqttReconnectAttempt = now;
      Serial.print("Versuche MQTT-Verbindung...");
      // Client-ID kann beliebig sein.
      if (client.connect("esp32MatrixClient"))
      {
        Serial.println("verbunden!");
        dma_display->print("OK!");
        // Topics abonnieren
        client.subscribe("Saftkiste/#");
        client.subscribe("chargeController/#");
      }
      else
      {
        Serial.print("fehlgeschlagen, rc=");
        Serial.print(client.state());
        Serial.println(" Erneuter Versuch in 5 Sek.");
      }
    }
  }
  else
  {
    // 2. WICHTIG: MQTT-Client-Schleife am Laufen halten
    client.loop();
  }

  // Zohair

  unsigned long currentMillis_Sonne = millis();

  // --- NEU: WETTER UPDATE (alle 15 Min) ---
  if (currentMillis_Sonne - letztesWetterUpdate >= wetterUpdateInterval)
  {
    letztesWetterUpdate = currentMillis_Sonne;
    holeWetter(); // Wetter neu abrufen
  }
  if (currentMillis_Sonne - letzteSonneAnimation >= sonneAnimationIntervall)
  {
    // Es ist Zeit für den nächsten Schritt
    letzteSonneAnimation = currentMillis_Sonne;

    sonnenStrahlStatus++; // Nächste Wachstumsstufe

    // Wenn Status größer als max (3), zurück auf 0 (Strahlen verschwinden)
    if (sonnenStrahlStatus > MAX_STRAHL_STATUS)
    {
      sonnenStrahlStatus = 0;
    }
  }
  
  // INA219 Check (jede Sekunde)
  if (currentMillis_Sonne - lastInaUpdate >= 1000) {
    lastInaUpdate = currentMillis_Sonne;
    checkWirelessChargingCurrent();
  }
  // Zohair

  // 3. Display nur alle X Millisekunden aktualisieren (verhindert Flimmern)
  unsigned long currentMillis = millis();
  if (currentMillis - lastDisplayUpdate >= displayUpdateInterval)
  {
    lastDisplayUpdate = currentMillis;

    // Neue Funktion zum Zeichnen der Livedaten aufrufen
    updateDisplay();
  }
}