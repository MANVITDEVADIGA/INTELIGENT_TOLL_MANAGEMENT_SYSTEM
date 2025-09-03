#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>  // Install ArduinoJson library

// GPS Setup
TinyGPSPlus gps;
HardwareSerial GPSserial(1);
#define RXD2 16
#define TXD2 17

// Wi-Fi
const char* ssid = "F1";
const char* password = "123456789";

// Google Maps API Key
const String googleApiKey = "AIzaSyBdLHTP-IuHKP1JPxzuKFY0w4ZKSYfuURE";

// Roads API endpoint
const String roadsEndpoint = "https://roads.googleapis.com/v1/nearestRoads";

void setup() {
  Serial.begin(115200);
  GPSserial.begin(9600, SERIAL_8N1, RXD2, TXD2);
  connectToWiFi();
}

void loop() {
  while (GPSserial.available() > 0) {
    char c = GPSserial.read();
    gps.encode(c);
  }

  if (gps.location.isUpdated()) {
    double latitude = gps.location.lat();
    double longitude = gps.location.lng();

    Serial.print("[GPS] Lat: ");
    Serial.print(latitude, 6);
    Serial.print("  Lng: ");
    Serial.println(longitude, 6);

    checkIfOnHighway(latitude, longitude);
  }

  delay(5000); // check every 5 seconds
}

// Connect to WiFi
void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected!");
}

// Check if vehicle is on National Highway
void checkIfOnHighway(double lat, double lng) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    String url = roadsEndpoint + "?points=" + String(lat, 6) + "," + String(lng, 6) + "&key=" + googleApiKey;
    Serial.println("[HTTP] URL: " + url);

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("[HTTP] Response:");
      Serial.println(response);

      // Parse JSON response
      StaticJsonDocument<2048> doc;
      DeserializationError error = deserializeJson(doc, response);

      if (!error && doc.containsKey("snappedPoints")) {
        const char* roadName = doc["snappedPoints"][0]["placeId"]; // Some APIs return road name differently
        Serial.print("[Roads API] Road detected: ");
        Serial.println(roadName);

        String roadStr = String(roadName);
        if (roadStr.indexOf("NH") != -1 || roadStr.indexOf("National Highway") != -1) {
          Serial.println("✅ Vehicle is on a National Highway!");
        } else {
          Serial.println("❌ Vehicle is NOT on a National Highway.");
        }
      } else {
        Serial.println("[JSON] Failed to parse response.");
      }

    } else {
      Serial.print("[HTTP] Error code: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  }
}
