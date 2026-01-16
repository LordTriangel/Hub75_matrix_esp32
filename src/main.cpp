#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
// Font header removed (now in renderer)
#include <time.h>
#include "bitmap.h"      
#include <HTTPClient.h>  
#include <ArduinoJson.h> 
#include <Wire.h>
#include <Adafruit_INA219.h>
#include "frames.h"      
#include "qrcode_bitmap.h" 
#include "secrets.h"     
#include "system_state.h"     // [REF] New
#include "display_renderer.h" // [REF] New

// Global SystemState instance
SystemState sysState;


// I2C Pins für INA219
// Matrix nutzt: 3,4,5,6,7,8,9,10,11,15,16,18,40,41
// Wir nehmen 13 und 14
#define I2C_SDA_PIN 13
#define I2C_SCL_PIN 14

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

// --- Externe Hardware & Logik Variablen ---
Adafruit_INA219 ina219;
unsigned long lastDisplayUpdate = 0;
unsigned long currentDisplayInterval = 500; // Standard 500ms
float current_mA_prev = 0;
unsigned long lastInaUpdate = 0;

// Wetter Timer
unsigned long letztesWetterUpdate = 0;
const unsigned long wetterUpdateInterval = 15 * 60 * 1000; // 15 Min

// Sonne Animation
unsigned long letzteSonneAnimation = 0;
const unsigned long sonneAnimationIntervall = 500; // Slower animation (250ms)
const int MAX_STRAHL_STATUS = 3; 

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

// Globals moved to SystemState
DisplayRenderer* renderer = nullptr;

// Zohair
//   NTP (Uhrzeit) Einstellungen
// ... (ntpServer etc remains, but variables moved) -> wait, constants remain.
// Just remove the `float g_...` variables.


// Timer für MQTT Reconnect (Non-blocking)
unsigned long lastMqttReconnectAttempt = 0;

// State Machine Definitions
enum DisplayState {
  STATE_MAIN,      // Standard view (Sun, Battery, etc.)
  STATE_LOGO,      // HM Logo
  STATE_FULLSCREEN,// New processed data view (Slideshow)
  STATE_CHARGING_ANIMATION, // NEU: Lade-Animation
  STATE_AKKU2,     // Akkuanzeige 2 (10s)
  STATE_TIME_LARGE,// Uhrzeit groß (5s)
  STATE_TEMP_LARGE,// Temperatur groß (5s)
  STATE_POWER_LARGE// Leistung groß (5s)
};

DisplayState currentState = STATE_MAIN;
DisplayState previousState = STATE_MAIN; // Um zurückzukehren
unsigned long stateTimer = 0;

// Durations (in milliseconds)
const unsigned long DURATION_MAIN = 15000;      // 10 seconds
const unsigned long DURATION_LOGO = 8000;       // 2 seconds
const unsigned long DURATION_FULLSCREEN = 5000; // 5 seconds
const unsigned long DURATION_CHARGING = 5000;   // 5 Sekunden GIF abspielen
const unsigned long DURATION_AKKU2 = 10000;     // 10 seconds
const unsigned long DURATION_TIME_LARGE = 5000; // 5 seconds
const unsigned long DURATION_TEMP_LARGE = 5000; // 5 seconds
const unsigned long DURATION_POWER_LARGE = 5000;// 5 seconds

// --- Speicher für 7-Tage-PV-Historie ---
// Wir legen einen Pointer an. Der Speicher wird in setup() im PSRAM reserviert.
const int HISTORY_POINTS_PER_DAY = 288; // Alle 5 Minuten ein Wert (24 * 60 / 5)
const int HISTORY_DAYS = 7;
const int TOTAL_HISTORY_POINTS = HISTORY_POINTS_PER_DAY * HISTORY_DAYS;
float *g_pv_power_history = nullptr; // Pointer für unser PSRAM-Array

bool ina219_active = false;
unsigned long lastStateChangeTime = 0; // [FIX] Track transitions


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
        sysState.temperature = temp;      // Temperatur speichern
        Serial.print("Neues Wetter: ");
        Serial.println(sysState.temperature);
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

// (Animation Settings)
int currentFrameIndex = 0;
unsigned long lastFrameTime = 0;
const int FRAME_DELAY = 100; // Zeit pro Frame in ms
uint16_t *frames_psram[38]; // Pointer-Array für PSRAM
bool psramInitialized = false;

// (GIF Callbacks entfernt)

// FUNKTION: Wireless Charging Check (INA219)
// ------------------------------------------------------
// FUNKTION: Wireless Charging Check (INA219)
// ------------------------------------------------------
void checkWirelessChargingCurrent() {
  if (!ina219_active) return;

  // Configuration
  const float NOISE_THRESHOLD = 50.0;       // mA: Below this, we update baseline immediately
  const float ANIMATION_THRESHOLD = 200.0;  // mA: Increased to filter 170mA spikes
  const float DEVICE_THRESHOLD = 250.0;     // mA: Real device detection (0.5A is min usually) [UPDATED: Lowered from 400 to 250]
  const unsigned long STABILIZE_TIMEOUT = 5000; // ms: If value "hangs" in between for 5s, accept it
  
  static float baseline_mA = -1.0; 
  static unsigned long lastStabilityCheck = 0;
  static bool animationTriggeredForThisEvent = false;
  static unsigned long lastEventTime = 0;

  static unsigned long lastPrintTime = 0;
  float current_mA = ina219.getCurrent_mA();
  unsigned long now = millis();

  if (now - lastPrintTime > 1000) {
       Serial.printf("Current: %.2f mA | Diff: %.2f\n", current_mA, current_mA - baseline_mA); 
       lastPrintTime = now;
  }
  
  // [FIX] Absolute Low Threshold Failsafe
  // If current is very low, we assume no device is charging, regardless of logic history.
  // This handles cases where removal drop was too small or baseline drifted.
  if (current_mA < 100.0) { // Lowered to 100mA for hysteresis (Add > 150, Valid > 100, Reset < 100)
      if (sysState.w_chg_cnt > 0) {
          sysState.w_chg_cnt = 0;
          Serial.printf("RESET: Current (%.1f mA) below idle threshold. Resetting count to 0.\n", current_mA);
      }
      // Ensure baseline tracks this low state so re-connection is detected cleanly
      baseline_mA = current_mA; 
      // Do not return here, let the logic flow (diff will be small/zero) but we force count to 0.
  }
  
  // [NEW] Slow Charge Detection (Plateau) for 0 -> 1
  // Handles cases where current rises slightly (12mA -> 215mA) and stays there.
  static unsigned long slowChargeStartTime = 0;
  // Cooldown-Static Definition (Needed here for visibility)
  static unsigned long lastAnimationTriggerTime = 0;

  if (sysState.w_chg_cnt == 0 && current_mA > 150.0) {
       if (slowChargeStartTime == 0) slowChargeStartTime = now;
       if (now - slowChargeStartTime > 3000) {
            sysState.w_chg_cnt = 1;
            Serial.printf("DETECT: Slow Charge Plateau Confirmed! Current: %.1f\n", current_mA);
            
            baseline_mA = current_mA;
            lastEventTime = now;
            slowChargeStartTime = 0;
            
            // Trigger Animation Logic
            if (currentState != STATE_CHARGING_ANIMATION) { 
                previousState = currentState; 
                currentState = STATE_CHARGING_ANIMATION;
                currentState = STATE_CHARGING_ANIMATION;
                currentDisplayInterval = 33; // 30 FPS for smoothness
                stateTimer = now;
                currentFrameIndex = 0;
                dma_display->fillScreen(0);
                lastAnimationTriggerTime = now;
            }
       }
  } else {
       slowChargeStartTime = 0;
  }

  // Initialize Baseline
  if (baseline_mA < 0) {
      baseline_mA = current_mA;
      lastStabilityCheck = millis();
      return; 
  }

  float diff = current_mA - baseline_mA;
  // unsigned long now = millis(); // Already defined above

  /*
   * LOGIC:
   * 1. Interactive Trigger: High enough jump to look like a start -> Animation
   * 2. Device Count: High enough jump/drop to be a device -> Update Count
   * 3. Baseline Tracking: 
   *    - Small noise? Follow immediately.
   *    - Medium change? Hold baseline to see if it's a ramp.
   *    - Timeout? If it stays medium for too long, just accept it (drift/throttling).
   */

  // --- 1. Interactive Animation Trigger ---
  // If we see a sharp rise, give instant feedback
  // Only if we haven't just triggered (bounce protection) AND we are not in the cooldown of an event

  // Cooldown-Static für Animation
  // static unsigned long lastAnimationTriggerTime = 0; // Moved up

  // [NEW] Interactive Trigger Debounce
  // Require 2 consecutive checks (> 200mA) to trigger.
  static int consecutiveTriggerCounts = 0;

  if (diff > ANIMATION_THRESHOLD && !animationTriggeredForThisEvent && (now - lastEventTime > 2000)) {
       consecutiveTriggerCounts++;
       if (consecutiveTriggerCounts >= 2) {
            // Only trigger visual if not already running AND enough time passed since LAST run (5s cooldown)
            if (currentState != STATE_CHARGING_ANIMATION && (now - lastAnimationTriggerTime > 5000)) {
               previousState = currentState; 
               currentState = STATE_CHARGING_ANIMATION;
               currentDisplayInterval = 33; // 30 FPS (Smoother than 20ms/50fps)
               stateTimer = millis();
               currentFrameIndex = 0;
               dma_display->fillScreen(0);
               Serial.println("INTERACTIVE: Animation triggered by confirmed jump!");
               
               lastAnimationTriggerTime = millis(); // Cooldown stamp
            }
            animationTriggeredForThisEvent = true; 
            consecutiveTriggerCounts = 0; // Reset
       }
  } else {
       // Reset count if signal drops or event already triggered
       // But be careful not to reset if we are just fluctuating slightly above threshold?
       // Let's reset if diff drops below threshold significantly.
       if (diff < ANIMATION_THRESHOLD * 0.8) {
           consecutiveTriggerCounts = 0;
       }
  }

  // --- 2. Device Detection (The "Smart" Part) ---
  
  static unsigned long potentialAddStartTime = 0;

  // CHECK: If potentially adding, verify stability
  if (potentialAddStartTime > 0) {
      // If signal dropped, it was a spike (e.g. removal)
      if (diff < DEVICE_THRESHOLD) {
           Serial.println("SPIKE: Signal dropped before confirmation. Ignoring.");
           potentialAddStartTime = 0; // Cancel
           animationTriggeredForThisEvent = false; // Allow re-trigger
           return;
      }
      
      // If signal sustained for > 1000ms, CONFIRM ADD
      if (now - potentialAddStartTime > 1000) {
           sysState.w_chg_cnt++;
           if (sysState.w_chg_cnt > 2) sysState.w_chg_cnt = 2;
           Serial.printf("DETECT: Device ADDED CONFIRMED! Count: %d\n", sysState.w_chg_cnt);
           
           baseline_mA = current_mA; 
           lastEventTime = now;
           potentialAddStartTime = 0; // Reset
           // animationTriggeredForThisEvent stays true until reset by logic below
      }
      return; // Wait for confirmation
  }

  // --- Normal Detection ---
  if (abs(diff) >= DEVICE_THRESHOLD) {
      if (diff > 0) {
          // START ADD SEQUENCE (Wait for stability)
           // Only if enough time passed since last distinct event
           if (now - lastEventTime > 2000) { 
              Serial.println("DETECT: Potential ADD (High Current detected)... Waiting for 1s stability.");
              potentialAddStartTime = now;
              // Note: Animation ALREADY triggered above by ANIMATION_THRESHOLD check
           }
      } else {
          // REMOVE DEVICE
          if (now - lastEventTime > 1000) {
              
              // HYBRID REMOVAL LOGIC
              // If we have 1 device, and current drops but stays > 100mA, it might be negotiation/throttle.
              if (sysState.w_chg_cnt == 1 && current_mA > 100.0) {
                  Serial.printf("IGNORE REMOVAL: Current (%.1f) > 100mA. Assuming negotiation.\n", current_mA);
                  baseline_mA = current_mA; // Update baseline to new lower level
                  lastEventTime = now;
              } else {
                  // Confirm Removal
                  sysState.w_chg_cnt--;
                  if (sysState.w_chg_cnt < 0) sysState.w_chg_cnt = 0;
                  Serial.printf("DETECT: Device REMOVED! Count: %d (Diff: %.1f)\n", sysState.w_chg_cnt, diff);
                  
                  baseline_mA = current_mA; 
                  lastEventTime = now;
                  animationTriggeredForThisEvent = false; 
              }
          }
      }
      lastStabilityCheck = now; // Reset timeouts
  } else {
      // Reset logic if diff is small (but not if we are waiting for add confirmation)
      if (potentialAddStartTime == 0) {
           // ... (Existing Baseline Logic) ...
           if (abs(diff) < NOISE_THRESHOLD) {
                baseline_mA = current_mA;
                lastStabilityCheck = now; 
                if (abs(diff) < 20.0) animationTriggeredForThisEvent = false;
           } 
           // ...
      }
  }
  
  current_mA_prev = current_mA; // Keep for legacy or debug
}

/**
 * @brief Diese Funktion wird jedes Mal aufgerufen, wenn eine
 * Nachricht auf einem abonnierten Topic eintrifft.
 */
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  // Payload in einen C-String umwandeln (Performance & Speicher Optimierung)
  char messageBuffer[32]; 
  // Sicherstellen, dass wir nicht überlaufen
  unsigned int copyLen = (length < sizeof(messageBuffer) - 1) ? length : (sizeof(messageBuffer) - 1);
  memcpy(messageBuffer, payload, copyLen);
  messageBuffer[copyLen] = '\0'; // Null-Terminierung
  
  // Konvertierung
  float value = atof(messageBuffer);

  // Daten
  if (strcmp(topic, "Saftkiste/battery_voltage") == 0)
  {
    sysState.batt_voltage = value;
  }
  else if (strcmp(topic, "Saftkiste/battery_current") == 0)
  {
    sysState.batt_current = value;
  }
  else if (strcmp(topic, "Saftkiste/netto_battery_power_W") == 0)
  {
    sysState.batt_power = value;
  }
  else if (strcmp(topic, "Saftkiste/State_Of_Charge") == 0)
  {
    sysState.batt_soc = value;
  }
  else if (strcmp(topic, "chargeController/1/voltage") == 0)
  {
    sysState.pv1_voltage = value;
  }
  else if (strcmp(topic, "chargeController/2/voltage") == 0)
  {
    sysState.pv2_voltage = value;
  }
  else if (strcmp(topic, "Anlagendaten/PV_Ladeleistung_W") == 0)
  {
    sysState.pv_power = value;
  }
  else if (strcmp(topic, "Anlagendaten/Verbrauchsleistung_W") == 0)
  {
    sysState.verbrauchsleistung = value;
  }
  else if (strcmp(topic, "Anlagendaten/Einspeisung_Heute_Wh") == 0)
  {
    sysState.einspeisung_heute_Wh = value;
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

// Drawing logic moved to DisplayRenderer


// Helper to determine next state in the cycle
DisplayState getNextState(DisplayState current) {
    switch(current) {
        case STATE_MAIN: return STATE_LOGO;
        case STATE_LOGO: return STATE_FULLSCREEN;
        case STATE_FULLSCREEN: return STATE_AKKU2;
        case STATE_AKKU2: return STATE_TIME_LARGE;
        case STATE_TIME_LARGE: return STATE_TEMP_LARGE;
        case STATE_TEMP_LARGE: return STATE_POWER_LARGE;
        case STATE_POWER_LARGE: return STATE_MAIN;
        default: return STATE_MAIN;
    }
}

/**
 * @brief Zeichnet die aktuellen Daten auf das Display
 */
void updateDisplay()
{
  unsigned long jetzt = millis();
  
  // STATICS für Redraw-Logic (Optimierung gegen Flackern/Smearing)
  static DisplayState lastProcessedState = STATE_MAIN; // Track state changes
  static String lastTimeStr = "";
  static int lastTemp = -999;
  static int lastPower = -999;
  static bool triggerFadeIn = false; // Trigger für FadeIn NACH dem Zeichnen

  // --- 1. STATE MACHINE TRANSITIONS ---
  switch (currentState) {
    case STATE_MAIN:
      if (jetzt - stateTimer >= DURATION_MAIN) {
        renderer->fadeOut(0);
        currentState = STATE_LOGO; 
        stateTimer = jetzt;
        renderer->clearScreen();
        triggerFadeIn = true;
      }
      break;

    case STATE_LOGO:
      if (jetzt - stateTimer >= DURATION_LOGO) {
        renderer->fadeOut(0);
        currentState = STATE_FULLSCREEN;
        stateTimer = jetzt;
        renderer->clearScreen();
        triggerFadeIn = true;
      }
      break;

    case STATE_FULLSCREEN:
      if (jetzt - stateTimer >= DURATION_FULLSCREEN) {
        renderer->fadeOut(0);
        currentState = STATE_AKKU2; 
        stateTimer = jetzt;
        renderer->clearScreen();
        triggerFadeIn = true;
      }
      break;

    case STATE_AKKU2:
      if (jetzt - stateTimer >= DURATION_AKKU2) {
        renderer->fadeOut(0);
        currentState = STATE_TIME_LARGE;
        stateTimer = jetzt;
        renderer->clearScreen();
        triggerFadeIn = true;
      }
      break;

    case STATE_TIME_LARGE:
      if (jetzt - stateTimer >= DURATION_TIME_LARGE) {
        renderer->fadeOut(0);
        currentState = STATE_TEMP_LARGE;
        stateTimer = jetzt;
        renderer->clearScreen();
        triggerFadeIn = true;
      }
      break;

    case STATE_TEMP_LARGE:
      if (jetzt - stateTimer >= DURATION_TEMP_LARGE) {
        renderer->fadeOut(0);
        currentState = STATE_POWER_LARGE;
        stateTimer = jetzt;
        renderer->clearScreen();
        triggerFadeIn = true;
      }
      break;

    case STATE_POWER_LARGE:
      if (jetzt - stateTimer >= DURATION_POWER_LARGE) {
        renderer->fadeOut(0);
        currentState = STATE_MAIN; 
        stateTimer = jetzt;
        renderer->clearScreen();
        triggerFadeIn = true;
      }
      break;
  }


  // --- 2. PREPARE DRAWING (CLEAR SCREEN EVERY FRAME) ---
  // Double Buffering: We clear the BACK BUFFER every frame.
  renderer->clearScreen(); 
  
  bool stateChanged = (currentState != lastProcessedState);
      
  if (stateChanged) {
      lastProcessedState = currentState;
      lastTimeStr = "";
      lastTemp = -999;
      lastPower = -999;
      // Fade Logic reset? No, relying on loop logic.
  }
  
  // --- 3. DRAWING ---
  switch (currentState) {
    case STATE_MAIN:
      // Interval auf 30ms für Animation
      currentDisplayInterval = 30;

      // Matrix 1 & 2 (Static / Slow)
      // With Double Buffering, we MUST redraw everything every frame.
      // (Global ClearScreen handles background, we just draw content)
      
      // Linkes Panel (Grafiken) already cleared by global clear.
      
      renderer->drawSun(sysState.sonnenStrahlStatus);
      renderer->drawBattery(sysState);
      renderer->drawClock();
      
      // Rechtes Panel (Text) already cleared.
      
      dma_display->setTextSize(1);
      // ... (Rest of drawing code follows, standard text drawing)
      // Note: I need to duplicate the drawing code here or just let it fall through?
      // The previous code had `///*` and `//*/` blocks.
      // I need to ensure I don't delete the drawing logic itself.
      // The `startLine` was 626.
      // Let's replace the BLOCK start.
      
      // --- Saftkiste --- (This was inside the throttle block, logic must be kept)


      // --- Saftkiste ---
      dma_display->setTextColor(dma_display->color565(0, 255, 100)); // Grün
      dma_display->setCursor(1 + 64, 2);
      dma_display->print("SOC:");
      dma_display->setCursor(28 + 64, 2);
      dma_display->print(sysState.batt_soc, 1); // 1 Nachkommastelle
      dma_display->print("%");

      dma_display->setTextColor(dma_display->color565(0, 200, 255)); // Cyan
      dma_display->setCursor(1 + 64, 12);
      dma_display->print("Batt:");
      dma_display->setCursor(28 + 64, 12);
      dma_display->print(sysState.batt_voltage, 2); // 2 Nachkommastellen
      dma_display->print("V");

      dma_display->setCursor(1 + 64, 22);
      dma_display->print("Last:");
      dma_display->setCursor(28 + 64, 22);
      dma_display->print(sysState.batt_current, 1);
      dma_display->print("A");

      // --- PV-Laderegler ---
      dma_display->setTextColor(dma_display->color565(255, 255, 0)); // Gelb
      dma_display->setCursor(1 + 64, 36);
      dma_display->print("PV1:");
      dma_display->setCursor(28 + 64, 36);
      dma_display->print(sysState.pv1_voltage, 1);
      dma_display->print("V");

      dma_display->setCursor(1 + 64, 46);
      dma_display->print("PV2:");
      dma_display->setCursor(28 + 64, 46);
      dma_display->print(sysState.pv2_voltage, 1);
      dma_display->print("V");
      //*/
      // 3tes Panel (128-191) - ALWAYS Refresh (30ms)
      // Wir löschen hier NICHT "fillRect", da drawMatrix3() Pixel einzeln zeichnet 
      // und den Rest des Panels (Rain) managed.
      // Aber: drawMatrix3 schreibt Text und Pixel, überschreibt aber nicht alles schwarz.
      // Da wir einen Regeneffekt wollen (Trail?), ist "nicht löschen" gut.
      // Aber: Der Text muss sauber bleiben.
      // Die drawMatrix3 Funktion sollte idealerweise "clear" machen oder wir machen es hier leicht transparent (Fade effect).
      dma_display->fillRect(128, 0, 64, 64, 0); // Full Clear for now to prevent smear, improve later for trails
      
      renderer->drawMatrix3(sysState);
      
      // [FIX] Add State Transition Logic for STATE_MAIN
      // duration 20s or 60s? Default was usually 15-20s.
      if (jetzt - stateTimer >= DURATION_MAIN) {
          renderer->fadeOut(0);
          currentState = STATE_LOGO; 
          stateTimer = jetzt;
          renderer->clearScreen();
          triggerFadeIn = true;
          currentDisplayInterval = 200; // Reset refresh rate
      }
      break;

    case STATE_CHARGING_ANIMATION:
      // Logic: Advance Frame if time passed
      if (jetzt - lastFrameTime >= FRAME_DELAY) {
        lastFrameTime = jetzt;
        currentFrameIndex++;
        if (currentFrameIndex >= FRAME_COUNT) {
          currentFrameIndex = 0; // Loop
        }
      }
      
      // Drawing: ALWAYS draw the current frame (Double Buffering)
      if (psramInitialized) {
           dma_display->drawRGBBitmap(0, 0, frames_psram[currentFrameIndex], FRAME_WIDTH, FRAME_HEIGHT);
      } else {
           dma_display->drawRGBBitmap(0, 0, animation_frames[currentFrameIndex], FRAME_WIDTH, FRAME_HEIGHT);
      }

      // Prüfen ob Zeit abgelaufen ist
      if (jetzt - stateTimer >= DURATION_CHARGING) {
        currentState = getNextState(previousState); // Weiter zum NÄCHSTEN State
        currentDisplayInterval = 500; // TURBO-MODUS AUS (500ms)
        stateTimer = jetzt;

        // Reset Timers to prevent black screen / blocking
        lastDisplayUpdate = 0;              // Force immediate update
        lastMqttReconnectAttempt = jetzt;   // Delay MQTT check by 5s
        
        renderer->clearScreen();
      }
      break;

    case STATE_LOGO:
      dma_display->drawRGBBitmap(32, 0, HMlogo, 128, 64);
      break;

    case STATE_FULLSCREEN:
      {
         // 54x54 QR Code. Display height 64.
         // Center on the 3-panel display (192 width).
         // X = (192 - 54) / 2 = 69
         // Y = (64 - 54) / 2 = 5
         
         // Clear screen first (redundant but safe)
         // dma_display->fillScreen(0); 
         
         int x = (PANEL_WIDTH * PANELS_CHAINED - QR_WIDTH) / 2;
         int y = (PANEL_HEIGHT - QR_HEIGHT) / 2;
         
         dma_display->drawBitmap(x, y, qrcode_bitmap, QR_WIDTH, QR_HEIGHT, dma_display->color565(255, 255, 255));
      }
      break;

    case STATE_AKKU2:
      // Force Redraw every frame for Double Buffering
      renderer->drawBattery2(sysState, true); 
      break;

    case STATE_TIME_LARGE:
      {
        dma_display->setFont(&IBMPlexMono24pt7b);
        dma_display->setTextColor(dma_display->color565(255, 255, 255));
        dma_display->setTextSize(1);

        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char timeBuffer[6];
            strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", &timeinfo);
            String timeString = String(timeBuffer);

             // Redraw ALWAYS (Double Buffering)
             // ClearRect not needed, full screen cleared at start.
             
             // Alten Text löschen? Unnötig, Screen ist leer.
             // Wir zeichnen einfach neu.

             int16_t x1, y1;
             uint16_t w, h;
             dma_display->getTextBounds(timeString, 0, 0, &x1, &y1, &w, &h);
             int x = (PANEL_WIDTH * PANELS_CHAINED - w) / 2;
             int y = (PANEL_HEIGHT - h) / 2 - y1; 
             dma_display->setCursor(x, y);  
             dma_display->print(timeString);
             
             lastTimeStr = timeString; 
        } else {
             dma_display->setFont(NULL);
             dma_display->setCursor(0, 0);
             dma_display->print("Wait for Time...");
        }
      }
      break;

    case STATE_TEMP_LARGE:
      {
        int tempNow = (int)round(sysState.temperature);
        
         // Redraw ALWAYS (Double Buffering)
         // if (tempNow != lastTemp) { // REMOVED optimization
             
             dma_display->setFont(&IBMPlexMono24pt7b);
             dma_display->setTextColor(dma_display->color565(255, 255, 255));
             dma_display->setTextSize(1);
             
             String tempVal = String(tempNow);
             
             int16_t x1, y1;
             uint16_t wNum, hNum;
             dma_display->getTextBounds(tempVal, 0, 0, &x1, &y1, &wNum, &hNum);
             
             uint16_t wC, hC;
             dma_display->getTextBounds("C", 0, 0, &x1, &y1, &wC, &hC);
             
             int gap = 15; // Platz für den Kreis
             int totalW = wNum + gap + wC;
             
             int xStart = (PANEL_WIDTH * PANELS_CHAINED - totalW) / 2;
             int yStart = (PANEL_HEIGHT - hNum) / 2 - y1;
             
             // 1. Zahl
             dma_display->setCursor(xStart, yStart);
             dma_display->print(tempVal);
             
             // 2. Kreis (manuell)
             int r = 3;
             int xCircle = xStart + wNum + 12; 
             int yCircle = yStart - 20; // Etwas höher als die baseline
             
             uint16_t c = dma_display->color565(255, 255, 255);
             dma_display->drawCircle(xCircle, yCircle, r, c);
             dma_display->drawCircle(xCircle, yCircle, r-1, c); // Etwas dicker
             
             // 3. "C"
             dma_display->setCursor(xStart + wNum + gap, yStart);
             dma_display->print("C");
             
             lastTemp = tempNow;
         // }
      }
      break;

    case STATE_POWER_LARGE:
      {
         int pwrNow = (int)round(sysState.pv_power);
         
          // if (pwrNow != lastPower) { // REMOVED Optimization
              dma_display->setFont(&IBMPlexMono24pt7b);
              dma_display->setTextColor(dma_display->color565(255, 255, 255));
              dma_display->setTextSize(1);
     
              
              int pwrDisplay = abs(pwrNow);
              String pwrStr = String(pwrDisplay) + "W";
     
              int16_t x1, y1;
              uint16_t w, h;
              dma_display->getTextBounds(pwrStr, 0, 0, &x1, &y1, &w, &h);
              int x = (PANEL_WIDTH * PANELS_CHAINED - w) / 2;
              int y = (PANEL_HEIGHT - h) / 2 - y1;
              dma_display->setCursor(x, y);
              dma_display->print(pwrStr);
              
              lastPower = pwrNow;
          // }
      }
      break;
  }
  
  // [NEW] Flip Buffer to show new frame (Essential for Double Buffering)
  dma_display->flipDMABuffer(); 
  
  // --- 4. FADE IN (AFTER FLIP) ---
  if (triggerFadeIn) {
      // Wir faden auf die gewünschte Zielhelligkeit ein
      // Jetzt zeig der Front Buffer bereits das neue Bild (noch dunkel)
      renderer->fadeIn(sysState.target_brightness);
      triggerFadeIn = false;
  } 
}


// --- Auto Brightness Control ---
// --- Auto Brightness Control ---
void updateAutoBrightness() {
    static int lastTargetStart = -1;
    static unsigned long lastCheck = 0;
    
    // Nur alle 10 Minuten (600000 ms) prüfen, wie gewünscht
    // Beim ersten Start (lastTargetStart == -1) sofort ausführen
    if (millis() - lastCheck < 600000 && lastTargetStart != -1) {
        return;
    }
    lastCheck = millis();

    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return;
    }

    int hour = timeinfo.tm_hour;
    int newTarget = 180; 

    // Nachtmodus zwischen 22:00 und 05:00 Uhr
    if (hour >= 22 || hour < 5) {
        newTarget = 50;
    } else {
        newTarget = 180;
    }

    if (newTarget != sysState.target_brightness) {
        sysState.target_brightness = newTarget;
        Serial.printf("Auto-Helligkeit: Ziel geändert auf %d\n", newTarget);
        
        // Initial einmal setzen, sonst macht es der Fade-Loop
        if (lastTargetStart == -1) {
             dma_display->setBrightness8(newTarget);
        }
        lastTargetStart = newTarget;
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
  mxconfig.clkphase = false;
  //mxconfig.latch_blanking = 4;
  //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;

  // Erstelle das Display-Objekt mit deiner eigenen Konfiguration
  mxconfig.double_buff = true; // [NEW] Enable Double Buffering to stop flicker
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  
  // Renderer initialisieren
  renderer = new DisplayRenderer(dma_display);

  // Starte das Display
  dma_display->begin();

  // Stelle eine moderate Helligkeit ein (0-255)
  dma_display->setBrightness(160);

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

  // I2C Manuell auf freien Pins starten, damit es nicht mit der Matrix kollidiert (GPIO 8/9)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000); // 100kHz Standard-Speed für Stabilität

  // INA219 Initialisierung
  if (!ina219.begin()) {
    Serial.println("Fehler: INA219 Chip nicht gefunden! (Deaktiviere Current-Checks)");
    ina219_active = false;
  } else {
    Serial.println("INA219 Chip gefunden.");
    ina219_active = true;
    
    // Optional: Kalibrierung für höhere Stromstärken, falls nötig
    //ina219.setCalibration_32V_2A(); // Oder _32V_1A(), je nach Shunt
    
    // Wir lesen hier erstmal den ersten Wert, damit der "Sprung" nicht beim Boot passiert
    current_mA_prev = ina219.getCurrent_mA();
  }

  // LittleFS starten - optional, aber gut
  // --- PSRAM Caching für Animation ---
  if (psramFound()) {
    Serial.println("PSRAM gefunden! Kopiere Frames...");
    size_t frameSize = FRAME_WIDTH * FRAME_HEIGHT * sizeof(uint16_t);
    bool success = true;
    for (int i = 0; i < FRAME_COUNT; i++) {
        // Reserviere Speicher im PSRAM
        frames_psram[i] = (uint16_t*) ps_malloc(frameSize);
        if (frames_psram[i] == NULL) {
            Serial.printf("Fehler: Kein PSRAM mehr für Frame %d!\n", i);
            success = false;
            break;
        }
        // Kopiere vom Flash (animation_frames[i]) in den PSRAM (frames_psram[i])
        // memcpy_P kopiert aus dem Programmspeicher
        memcpy_P(frames_psram[i], animation_frames[i], frameSize);
        
        // [BRIGHTNESS BOOST]
        // Iterate through all pixels and double the brightness
        int pixelCount = FRAME_WIDTH * FRAME_HEIGHT;
        for (int p = 0; p < pixelCount; p++) {
            uint16_t color = frames_psram[i][p];
            
            // Extract RGB (565 format)
            int r = (color >> 11) & 0x1F;
            int g = (color >> 5) & 0x3F;
            int b = color & 0x1F;
            
            // Boost Brightness (Multiplying by 2.0, you can adjust this)
            r = (int)(r * 2.0);
            g = (int)(g * 2.0);
            b = (int)(b * 2.0);
            
            // Clamp to max values
            if (r > 31) r = 31;
            if (g > 63) g = 63;
            if (b > 31) b = 31;
            
            // Re-pack
            frames_psram[i][p] = (r << 11) | (g << 5) | b;
        }
    }
    if (success) {
        Serial.println("Alle Frames erfolgreich in PSRAM geladen!");
        psramInitialized = true;
    }
  } else {
    Serial.println("Warnung: Kein PSRAM gefunden. Animation läuft langsam vom Flash.");
  }

  // LittleFS entfernt (nicht mehr benötigt für PROGMEM Animation)
  
  // GIF Init entfernt

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
        client.subscribe("Anlagendaten/#");
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
  
  updateAutoBrightness(); // Check Brightness

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

    sysState.sonnenStrahlStatus++; // Nächste Wachstumsstufe

    // Wenn Status größer als max (3), zurück auf 0 (Strahlen verschwinden)
    if (sysState.sonnenStrahlStatus > MAX_STRAHL_STATUS)
    {
      sysState.sonnenStrahlStatus = 0;
    }
  }
  
  // INA219 Check (jede 100ms für schnellere Reaktionszeit)
  if (currentMillis_Sonne - lastInaUpdate >= 100) {
    lastInaUpdate = currentMillis_Sonne;
    checkWirelessChargingCurrent();
  }
  // Zohair

  // 3. Display nur alle X Millisekunden aktualisieren (verhindert Flimmern)
  unsigned long currentMillis = millis();
  if (currentMillis - lastDisplayUpdate >= currentDisplayInterval)
  {
    lastDisplayUpdate = currentMillis;

    // Neue Funktion zum Zeichnen der Livedaten aufrufen
    updateDisplay();
  }
}