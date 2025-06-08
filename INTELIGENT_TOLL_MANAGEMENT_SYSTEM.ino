#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <HTTPClient.h>

// GPS Setup
TinyGPSPlus gps;
HardwareSerial GPSserial(1);
#define RXD2 16
#define TXD2 17

// Wi-Fi Credentials — Replace with your network info
const char* ssid = "F1";
const char* password = "123456789";

// CircuitDigest Cloud API Key — Replace with your API key from CircuitDigest Cloud profile
const String apiKey = "ODMxDRxK1nZf";

// CircuitDigest GeoLinker API endpoint
const String apiEndpoint = "https://www.circuitdigest.cloud/geolinker";

void setup() {
  Serial.begin(115200);
  Serial.println("[Setup] Starting setup...");

  GPSserial.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial.println("[Setup] GPS serial initialized at 9600 baud.");

  Serial.println("[Setup] Connecting to WiFi...");
  connectToWiFi();

  Serial.println("[Setup] Setup complete.");
}

void loop() {
  Serial.println("[Loop] Checking for GPS data...");

  while (GPSserial.available() > 0) {
    char c = GPSserial.read();
    // Serial.print(c);  // Raw GPS data (commented out to reduce noise)
    gps.encode(c);
  }
  Serial.println("[Loop] GPS data read and encoded.");

  if (gps.location.isUpdated()) {
    double latitude = gps.location.lat();
    double longitude = gps.location.lng();

    Serial.print("[Loop] GPS location updated: Latitude = ");
    Serial.print(latitude, 6);
    Serial.print(", Longitude = ");
    Serial.println(longitude, 6);

    Serial.println("[Loop] Preparing to send data to CircuitDigest Cloud...");
    sendGPSDataToCloud(latitude, longitude);
  } else {
    Serial.println("[Loop] No new GPS location data available.");
  }

  Serial.println("[Loop] Waiting 1 second before next GPS read.\n");
  delay(1000);
}

// Connect to Wi-Fi function with status prints
void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("[WiFi] Connecting to WiFi");

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempt++;

    if (attempt > 40) { // ~20 seconds timeout
      Serial.println("\n[WiFi] Failed to connect to WiFi. Restarting...");
      ESP.restart();
    }
  }

  Serial.println("\n[WiFi] WiFi connected.");
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.localIP());
}

// Send GPS coordinates as JSON POST to CircuitDigest Cloud
void sendGPSDataToCloud(double latitude, double longitude) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[HTTP] WiFi connected, starting HTTP client...");

    HTTPClient http;
    http.begin(apiEndpoint);

    Serial.println("[HTTP] Setting HTTP headers...");
    http.addHeader("Authorization", apiKey);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"lat\":[" + String(latitude, 6) + "], \"long\":[" + String(longitude, 6) + "]}";

    Serial.print("[HTTP] Payload prepared: ");
    Serial.println(payload);

    Serial.println("[HTTP] Sending HTTP POST request...");
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
      Serial.print("[HTTP] POST successful, response code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("[HTTP] POST failed, error code: ");
      Serial.println(httpResponseCode);
    }

    http.end();
    Serial.println("[HTTP] HTTP client ended.");
  } else {
    Serial.println("[HTTP] WiFi not connected. Cannot send data.");
  }
}
