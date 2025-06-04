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

  Serial.println("GPS module started.");
}

void loop() {
  while (GPSserial.available() > 0) {
    char c = GPSserial.read();
    gps.encode(c);
  }

  if (gps.location.isUpdated()) {
    Serial.print(gps.location.lat(), 6);
    Serial.print(",");
    Serial.println(gps.location.lng(), 6);
  }

  delay(1000);  // Delay for readability
}
