// SmartAttendance_ESP32_RFID.ino
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

//////////////////// USER CONFIG ////////////////////
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASS";
const char* serverUrl = "http://192.168.1.100:3000/api/attendance"; // change to your server IP/domain
const long utcOffsetSeconds = 5 * 3600 + 30 * 60; // India IST = +5:30
//////////////////////////////////////////////////////

// RC522 pins (change if needed)
const int RST_PIN = 22;
const int SS_PIN  = 21;

MFRC522 mfrc522(SS_PIN, RST_PIN);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetSeconds);

unsigned long lastReadMillis = 0;
const unsigned long readDebounce = 1500; // ms

void setup() {
  Serial.begin(9600);
  SPI.begin();        // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522
  delay(1000);
  Serial.println("RFID Smart Attendance - ESP32");

  // WiFi connect
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  int wcount=0;
  while (WiFi.status() != WL_CONNECTED && wcount < 30) {
    delay(500); Serial.print(".");
    wcount++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi connect failed. Running offline.");
  }

  timeClient.begin();
  timeClient.update();
}

String uidToString(MFRC522::Uid uid) {
  String s = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) s += "0";
    s += String(uid.uidByte[i], HEX);
    if (i + 1 < uid.size) s += ":";
  }
  s.toUpperCase();
  return s;
}

void loop() {
  // update time
  if (millis() % 60000 < 50) timeClient.update();

  // Look for new cards
  if ( ! mfrc522.PICC_IsNewCardPresent()) return;
  if ( ! mfrc522.PICC_ReadCardSerial()) return;

  unsigned long now = millis();
  if (now - lastReadMillis < readDebounce) {
    // debounce: ignore quick repeated reads
    return;
  }
  lastReadMillis = now;

  String uidStr = uidToString(mfrc522.uid);
  String timestamp = timeClient.getFormattedTime(); // HH:MM:SS (we'll also send epoch)
  unsigned long epoch = timeClient.getEpochTime();

  Serial.println("Card detected UID: " + uidStr + " time: " + timestamp);

  // Build JSON
  String json = "{";
  json += "\"uid\":\"" + uidStr + "\",";
  json += "\"timestamp_epoch\":" + String(epoch) + ",";
  json += "\"timestamp\":\"" + timestamp + "\",";
  json += "\"device\":\"esp32_rfid_1\"";
  json += "}";

  // Send to server via HTTP POST if connected
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(json);
    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("HTTP " + String(httpResponseCode) + " Response: " + payload);
    } else {
      Serial.println("HTTP POST failed, error: " + String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("Not connected to WiFi. Can't send record.");
  }

  // Halt until card removed
  while (mfrc522.PICC_IsNewCardPresent() || mfrc522.PICC_ReadCardSerial()) {
    delay(50);
  }
}
