#include <TinyGPS++.h>
#include <HardwareSerial.h>

TinyGPSPlus gps;
HardwareSerial GPSserial(1);  // Use UART1

// Define RX and TX pins for ESP32
#define RXD2 16  // Connect to TX of NEO-6M
#define TXD2 17  // Connect to RX of NEO-6M

void setup() {
  Serial.begin(115200);           // For debugging
  GPSserial.begin(9600, SERIAL_8N1, RXD2, TXD2);  // GPS baud rate

  Serial.println("[Step 1] Serial monitor started.");
  Serial.println("[Step 2] Initializing GPS module...");
}

void loop() {
  Serial.println("[Step 3] Checking for available GPS data...");
  while (GPSserial.available() > 0) {
    char c = GPSserial.read();
    gps.encode(c);
    Serial.print("[Step 4] Received char: ");
    Serial.println(c);
  }

  if (gps.location.isUpdated()) {
    Serial.println("[Step 5] GPS location updated.");
    Serial.print("Latitude: ");
    Serial.println(gps.location.lat(), 6);
    Serial.print("Longitude: ");
    Serial.println(gps.location.lng(), 6);
    Serial.print("Altitude: ");
    Serial.println(gps.altitude.meters());
    Serial.print("Satellites: ");
    Serial.println(gps.satellites.value());
    Serial.print("Speed (km/h): ");
    Serial.println(gps.speed.kmph());
    Serial.println("------");
  } else {
    Serial.println("[Step 6] No new location data.");
  }

  delay(10000);  // Delay for readability
}
