#ifndef SECRETS_H
#define SECRETS_H

// --- OpenWeatherMap API ---
// Hol dir einen kostenlosen Key auf: https://openweathermap.org/
#define OWM_API_KEY "DEIN_OPENWEATHERMAP_API_KEY"

// --- WiFi Credentials ---
// Debug / Home Umgebung (optional)
#define WIFI_SSID_HOME "Dein_Zuhause_WLAN"
#define WIFI_PASS_HOME "Dein_Zuhause_Passwort"

// Produktiv / Work Umgebung (Hochschule)
#define WIFI_SSID_WORK "AgriPV_WLAN"
#define WIFI_PASS_WORK "AgriPV_Passwort"

// --- MQTT Broker ---
// IP-Adresse des Raspberry Pi oder Brokers
#define MQTT_SERVER_IP "192.168.178.XX" 

#endif
