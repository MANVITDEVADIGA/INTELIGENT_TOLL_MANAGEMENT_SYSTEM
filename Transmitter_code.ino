#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <math.h>

// GPS Setup
TinyGPSPlus gps;
HardwareSerial GPSserial(1);
#define RXD2 16
#define TXD2 17

// Wi-Fi Credentials
const char* ssid = "F1";
const char* password = "123456789";

// Google API Key
const String googleApiKey = "AIzaSyBVfpNIEFmxKtnKn9IPm718eu6s0Oj9EzM";

// API Endpoints
const String roadsApiEndpoint = "https://roads.googleapis.com/v1/nearestRoads";
const String placesApiEndpoint = "https://maps.googleapis.com/maps/api/place/details/json";

// NH Distance calculation
bool onNH = false;
double lastLat = 0.0, lastLng = 0.0;
double nhDistanceKm = 0.0;

// Preferences object for flash storage
Preferences preferences;

// Function prototypes
void connectToWiFi();
void getRoadName(double latitude, double longitude);
void getPlaceName(String placeId, double latitude, double longitude);
double calculateDistance(double lat1, double lon1, double lat2, double lon2);
void saveDistance();
void loadDistance();

void setup() {
  Serial.begin(115200);
  Serial.println("[Setup] Starting setup...");

  GPSserial.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial.println("[Setup] GPS serial initialized at 9600 baud.");

  Serial.println("[Setup] Connecting to WiFi...");
  connectToWiFi();

  // Load stored NH distance from Flash
  preferences.begin("NH_Tracker", false);
  loadDistance();

  Serial.println("[Setup] Setup complete.");
}

void loop() {
  while (GPSserial.available() > 0) {
    char c = GPSserial.read();
    gps.encode(c);
  }

  if (gps.location.isUpdated()) {
    double latitude = gps.location.lat();
    double longitude = gps.location.lng();

    Serial.print("[Loop] GPS location: ");
    Serial.print(latitude, 6);
    Serial.print(", ");
    Serial.println(longitude, 6);

    getRoadName(latitude, longitude);
  }

  delay(5000); // check every 5 seconds
}

// Connect to Wi-Fi
void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("[WiFi] Connecting to WiFi");

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempt++;

    if (attempt > 40) {
      Serial.println("\n[WiFi] Failed to connect. Restarting...");
      ESP.restart();
    }
  }

  Serial.println("\n[WiFi] Connected!");
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.localIP());
}

// Step 1: Call Roads API
void getRoadName(double latitude, double longitude) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = roadsApiEndpoint + "?points=" + String(latitude, 6) + "," + String(longitude, 6) + "&key=" + googleApiKey;

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String response = http.getString();
      DynamicJsonDocument doc(4096);
      DeserializationError error = deserializeJson(doc, response);

      if (!error && doc.containsKey("snappedPoints")) {
        String placeId = doc["snappedPoints"][0]["placeId"].as<String>();
        getPlaceName(placeId, latitude, longitude);
      }
    } else {
      Serial.print("[HTTP] Roads API failed, code: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  }
}

// Step 2: Call Places API to get road name
void getPlaceName(String placeId, double latitude, double longitude) {
  HTTPClient http;
  String url = placesApiEndpoint + "?place_id=" + placeId + "&fields=name&key=" + googleApiKey;

  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String response = http.getString();
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, response);

    if (!error && doc.containsKey("result")) {
      String roadName = doc["result"]["name"].as<String>();
      Serial.print("🚗 Vehicle is on: ");
      Serial.println(roadName);

      String roadUpper = roadName;
      roadUpper.toUpperCase();

      if (roadUpper.indexOf("NH") != -1) {
        Serial.println("✅ Vehicle is on a National Highway!");

        if (!onNH) {
          onNH = true;
          nhDistanceKm = 0.0;
          saveDistance();
          Serial.println("📍 Entered NH → Reset distance to 0 km");
        }

        // If already on NH, calculate incremental distance
        if (lastLat != 0.0 && lastLng != 0.0) {
          double d = calculateDistance(lastLat, lastLng, latitude, longitude);
          nhDistanceKm += d;
          saveDistance();

          Serial.println("------------------------------------------------");
          Serial.print("📏 Distance traveled on NH so far: ");
          Serial.print(nhDistanceKm, 3);
          Serial.println(" km");
          Serial.println("------------------------------------------------");
        }
      } else {
        if (onNH) {
          Serial.println("❌ Vehicle left NH. Final NH distance:");
          Serial.print("📏 Total distance traveled on NH: ");
          Serial.print(nhDistanceKm, 3);
          Serial.println(" km");
          saveDistance();
        }
        onNH = false;
      }

      lastLat = latitude;
      lastLng = longitude;
    }
  }

  http.end();
}

// Haversine formula to calculate distance between two GPS points
double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  double R = 6371.0; // Earth radius in km
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);
  lat1 = radians(lat1);
  lat2 = radians(lat2);

  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1) * cos(lat2) *
             sin(dLon / 2) * sin(dLon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

// Save distance in Flash
void saveDistance() {
  preferences.putDouble("nhDistance", nhDistanceKm);
}

// Load distance from Flash
void loadDistance() {
  nhDistanceKm = preferences.getDouble("nhDistance", 0.0);
  Serial.print("[Flash] Loaded stored NH distance: ");
  Serial.print(nhDistanceKm, 3);
  Serial.println(" km");
}
