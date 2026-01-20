/*
  VEHICLE NH TRACKER - Complete Project
  
  Components:
  - ESP32 Dev Module
  - GPS Module (UART2: RX=16, TX=17)
  - SD Card Reader (CS=5)
  
  Classes:
  1. GPSModule - Get accurate GPS location (≤9m accuracy)
  2. SerialMonitorPrint - Print all status messages
  3. SDCardReader - Read coordinates file from SD card
  4. CompareGpsSd - Compare GPS with SD card coordinates
  5. CalculateNHDistance - Calculate NH distance traveled
  6. TransmittingData - Send data via ESP-NOW
*/

#include <Arduino.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ==================== CONFIGURATION ====================
#define GPS_SERIAL_NUM 2
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define SD_CS_PIN 5

// ST7735S Display SPI pins
#define TFT_CS   15
#define TFT_RST  4
#define TFT_DC   2

const char* COORDS_FILENAME = "/gps_log.csv";
const char* META_FILENAME = "/meta.txt";
const char* VEHICLE_ID = "KA20AB1234";
const char* FASTAG_NUMBER = "1234567890123";

// GPS accuracy threshold (meters)
const double ACCURACY_THRESHOLD_M = 9.0;
const double HDOP_THRESHOLD = 5.0;

// NH matching thresholds
const double MATCH_DISTANCE_M = 9.0;
const unsigned long GPS_CHECK_INTERVAL_MS = 2000;

// Remote ESP32 MAC address
uint8_t REMOTE_MAC[6] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};

// ==================== GLOBAL OBJECTS ====================
TinyGPSPlus gps;
HardwareSerial GPSSerial(GPS_SERIAL_NUM);

// ST7735S display uses default SPI pins (MOSI=23, SCK=18)
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// Haversine distance calculation (meters)
static double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);
  double a = sin(dLat/2)*sin(dLat/2) + cos(radians(lat1))*cos(radians(lat2))*sin(dLon/2)*sin(dLon/2);
  double c = 2 * atan2(sqrt(a), sqrt(1-a));
  return R * c;
}

/* ==================== Display Helper Functions ==================== */
void initDisplay() {
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);  // Portrait mode for 128x128
  tft.fillScreen(ST7735_BLACK);
}

void displayUpdate(const char* id, const char* fastag, double nh_km, double total_km, bool onNH) {
  tft.fillScreen(ST7735_BLACK);
  
  // ===== HEADER =====
  tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
  tft.setTextSize(2);
  tft.setCursor(5, 2);
  tft.println("VEHICLE");
  tft.setCursor(10, 18);
  tft.println("TRACKER");
  
  // ===== DIVIDER LINE =====
  tft.drawLine(0, 36, 128, 36, ST7735_CYAN);
  
  // ===== VEHICLE ID =====
  tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(5, 42);
  tft.println("ID:");
  tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
  tft.setCursor(5, 52);
  tft.setTextSize(1);
  tft.println(id);
  
  // ===== FASTAG =====
  tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
  tft.setCursor(5, 64);
  tft.println("FASTAG:");
  tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
  tft.setCursor(5, 74);
  tft.println(fastag);
  
  // ===== DIVIDER LINE =====
  tft.drawLine(0, 84, 128, 84, ST7735_GREEN);
  
  // ===== NH DISTANCE =====
  tft.setTextColor(ST7735_GREEN, ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(5, 90);
  tft.print("NH:");
  char nh_str[8];
  dtostrf(nh_km, 6, 3, nh_str);
  tft.setCursor(35, 90);
  tft.print(nh_str);
  tft.setCursor(80, 90);
  tft.println("km");
  
  // ===== TOTAL DISTANCE =====
  tft.setTextColor(ST7735_BLUE, ST7735_BLACK);
  tft.setCursor(5, 101);
  tft.print("TOTAL:");
  char total_str[8];
  dtostrf(total_km, 6, 3, total_str);
  tft.setCursor(45, 101);
  tft.print(total_str);
  tft.setCursor(90, 101);
  tft.println("km");
  
  // ===== STATUS =====
  tft.setTextSize(2);
  tft.setTextColor(onNH ? ST7735_GREEN : ST7735_RED, ST7735_BLACK);
  tft.setCursor(20, 112);
  tft.println(onNH ? "ON NH" : "OFF");
}

/* ==================== CLASS 1: GPSModule ==================== */
class GPSModule {
  public:
    GPSModule(HardwareSerial &hwSerial): _serial(hwSerial) {}
    
    void begin(unsigned long baud = 9600, int rx = GPS_RX_PIN, int tx = GPS_TX_PIN) {
      _serial.begin(baud, SERIAL_8N1, rx, tx);
      delay(200);
    }
    
    void feed() {
      while (_serial.available()) {
        char c = _serial.read();
        gps.encode(c);
      }
    }
    
    // Get location only if accurate (≤9m HDOP)
    bool getLocationIfAccurate(double &lat, double &lon) {
      if (!gps.location.isValid()) return false;
      
      double hdop_val = gps.hdop.hdop();
      if (hdop_val <= 0) hdop_val = 99.0;
      
      if (hdop_val <= HDOP_THRESHOLD) {
        lat = gps.location.lat();
        lon = gps.location.lng();
        return true;
      }
      return false;
    }
    
    bool getLastLocation(double &lat, double &lon) {
      if (!gps.location.isValid()) return false;
      lat = gps.location.lat();
      lon = gps.location.lng();
      return true;
    }

  private:
    HardwareSerial &_serial;
};

/* ==================== CLASS 2: SerialMonitorPrint ==================== */
class SerialMonitorPrint {
  public:
    void begin(unsigned long baud = 115200) {
      Serial.begin(baud);
      while (!Serial) { delay(1); }
      printLine("===== VEHICLE NH TRACKER STARTED =====");
    }
    
    void printLine(const char* msg) {
      Serial.println(msg);
    }
    
    void printFmt(const char* fmt, ...) {
      char buf[256];
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(buf, sizeof(buf), fmt, ap);
      va_end(ap);
      Serial.println(buf);
    }

    void printGpsInit(bool ok) {
      if (ok) printLine("GPS >> Initialized");
      else printLine("GPS >> Failed");
    }
    
    void printGpsLocation(double lat, double lon, double hdop) {
      printFmt("GPS >> LAT: %.6f | LON: %.6f | HDOP: %.2f", lat, lon, hdop);
    }
    
    void printSdInit(bool ok) {
      if (ok) printLine("SD Card >> Initialized");
      else printLine("SD Card >> Failed");
    }
    
    void printSdReadSuccess() {
      printLine("SD Card >> Coordinates loaded");
    }
    
    void printOnNH(bool isOn) {
      if (isOn) printLine("Vehicle >> ON NH");
      else printLine("Vehicle >> OFF NH");
    }
    
    void printDistances(double nh, double total) {
      // Convert meters to kilometers
      double nh_km = nh / 1000.0;
      double total_km = total / 1000.0;
      printFmt("Distance >> NH: %.3f km | Total: %.3f km", nh_km, total_km);
    }
    
    void printSentSuccess() {
      printLine("Data >> Sent to remote ESP32");
    }
    
    void printConfirmedReset() {
      printLine("Data >> Confirmed - Distances reset to ZERO");
    }
};

/* ==================== CLASS 3: SDCardReader ==================== */
class SDCardReader {
  public:
    SDCardReader(int csPin, const char* coordsFile, const char* metaFile)
      : _csPin(csPin), _coordsFile(coordsFile), _metaFile(metaFile) {}
    
    bool begin() {
      if (!SD.begin(_csPin)) return false;
      
      if (!SD.exists(_metaFile)) {
        File f = SD.open(_metaFile, FILE_WRITE);
        if (f) { f.println("0.0"); f.close(); }
      }
      return true;
    }

    // Stream through entire coords file and check if GPS matches any coordinate
    template<typename Fn> 
    bool checkIfNearAnyCoord(double lat, double lon, double threshold_m, Fn callback) {
      File f = SD.open(_coordsFile, FILE_READ);
      if (!f) return false;
      
      char line[128];
      size_t idx = 0;
      
      while (f.available()) {
        char c = f.read();
        if (c == '\n' || idx >= sizeof(line)-1) {
          line[idx] = 0;
          idx = 0;
          
          if (strlen(line) > 5) {
            double rlat = 0, rlon = 0;
            if (parseLatLon(line, rlat, rlon)) {
              double d = haversineMeters(lat, lon, rlat, rlon);
              if (d <= threshold_m) {
                f.close();
                callback(rlat, rlon, d);
                return true;
              }
            }
          }
        } else {
          line[idx++] = c;
        }
      }
      
      // Handle last line
      if (idx > 0) {
        line[idx] = 0;
        double rlat = 0, rlon = 0;
        if (parseLatLon(line, rlat, rlon)) {
          double d = haversineMeters(lat, lon, rlat, rlon);
          if (d <= threshold_m) {
            f.close();
            callback(rlat, rlon, d);
            return true;
          }
        }
      }
      
      f.close();
      return false;
    }

    double readTotalDistance() {
      File f = SD.open(_metaFile, FILE_READ);
      if (!f) return 0.0;
      String s = f.readStringUntil('\n');
      f.close();
      return s.toDouble();
    }

    bool writeTotalDistance(double meters) {
      File f = SD.open(_metaFile, FILE_WRITE);
      if (!f) return false;
      f.seek(0);
      f.print(String(meters, 6));
      f.close();
      return true;
    }

  private:
    int _csPin;
    const char* _coordsFile;
    const char* _metaFile;

    static bool parseLatLon(const char* line, double &lat, double &lon) {
      const char* p = strchr(line, ',');
      if (!p) return false;
      char left[64], right[64];
      size_t l = p - line;
      if (l >= sizeof(left)) return false;
      strncpy(left, line, l);
      left[l] = 0;
      strncpy(right, p+1, sizeof(right)-1);
      right[sizeof(right)-1] = 0;
      lat = atof(left);
      lon = atof(right);
      return true;
    }
};

/* ==================== CLASS 4: CompareGpsSd ==================== */
class CompareGpsSd {
  public:
    CompareGpsSd(SDCardReader &sd, SerialMonitorPrint &serial)
      : _sd(sd), _serial(serial) {
      resetWindow();
      _isOnNH = false;
    }
    
    // STEP1, STEP2, STEP3 logic implemented here
    bool evaluate(double lat, double lon) {
      bool matched = _sd.checkIfNearAnyCoord(lat, lon, MATCH_DISTANCE_M, 
        [](double rlat, double rlon, double d){});
      
      pushWindow(matched);
      bool prev = _isOnNH;
      
      if (!_isOnNH) {
        // STEP1: Check if first 5 matches -> declare ON NH
        if (windowAllTrue()) {
          _isOnNH = true;
          resetWindow();
        }
      } else {
        // STEP3: Check if 5 consecutive non-matches -> declare OFF NH
        if (windowAllFalse()) {
          _isOnNH = false;
          resetWindow();
        }
        // STEP2: If 4 or less non-matches, stay ON NH
      }
      
      if (prev != _isOnNH) {
        _serial.printOnNH(_isOnNH);
        return true;
      }
      return false;
    }

    bool isOnNH() const { return _isOnNH; }

  private:
    SDCardReader &_sd;
    SerialMonitorPrint &_serial;
    bool _window[5];
    int _idx = 0;
    bool _isOnNH;

    void resetWindow() {
      for (int i = 0; i < 5; i++) _window[i] = false;
      _idx = 0;
    }
    
    void pushWindow(bool val) {
      _window[_idx] = val;
      _idx = (_idx + 1) % 5;
    }
    
    bool windowAllTrue() {
      for (int i = 0; i < 5; i++) if (!_window[i]) return false;
      return true;
    }
    
    bool windowAllFalse() {
      for (int i = 0; i < 5; i++) if (_window[i]) return false;
      return true;
    }
};

/* ==================== CLASS 5: CalculateNHDistance ==================== */
class CalculateNHDistance {
  public:
    CalculateNHDistance(SDCardReader &sd, SerialMonitorPrint &serial)
      : _sd(sd), _serial(serial) {
      _nhDistance = 0.0;
      _totalDistance = _sd.readTotalDistance();
      _prevLat = NAN;
      _prevLon = NAN;
      _onNH = false;
    }
    
    void begin() {
      // Already read meta in constructor
    }

    void updateState(bool onNH) {
      if (onNH && !_onNH) {
        // Starting NH tracking
        _nhDistance = 0.0;
        _prevLat = NAN;
        _prevLon = NAN;
      } else if (!onNH && _onNH) {
        // Stopped NH - add to total and persist
        _totalDistance += _nhDistance;
        _sd.writeTotalDistance(_totalDistance);
        _nhDistance = 0.0;
        _prevLat = NAN;
        _prevLon = NAN;
      }
      _onNH = onNH;
      _serial.printDistances(_nhDistance, _totalDistance);
    }

    void feedLocationForDistance(double lat, double lon) {
      if (!_onNH) return;
      
      if (!isfinite(_prevLat) || !isfinite(_prevLon)) {
        _prevLat = lat;
        _prevLon = lon;
        return;
      }
      
      double d = haversineMeters(_prevLat, _prevLon, lat, lon);
      
      // Only count movement > 2 meters (filters out GPS noise/drift)
      if (d > 2.0) {
        _nhDistance += d;
        _prevLat = lat;
        _prevLon = lon;
      }
    }

    double getNHDistance() const { return _nhDistance; }
    double getTotalDistance() const { return _totalDistance + _nhDistance; }

    void resetAllAndPersistZero() {
      _nhDistance = 0.0;
      _totalDistance = 0.0;
      _sd.writeTotalDistance(0.0);
      _serial.printConfirmedReset();
    }

  private:
    SDCardReader &_sd;
    SerialMonitorPrint &_serial;
    double _nhDistance;
    double _totalDistance;
    double _prevLat, _prevLon;
    bool _onNH;
};

/* ==================== CLASS 6: TransmittingData (ESP-NOW) ==================== */
#pragma pack(push,1)
struct TxPacket {
  char vehicle_id[16];
  char fastag[32];
  double total_distance_m;
  uint32_t seq;
};
#pragma pack(pop)

#pragma pack(push,1)
struct RxPacket {
  uint8_t cmd; // 0x01 = confirmation reset
  uint32_t seq;
};
#pragma pack(pop)

void onDataSentStatic(const wifi_tx_info_t *info, esp_now_send_status_t status);
void onDataRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);

class TransmittingData {
  public:
    TransmittingData(SerialMonitorPrint &serial, CalculateNHDistance &calc)
      : _serial(serial), _calc(calc), _seq(0) {}

    void begin(const uint8_t peerMac[6]) {
      WiFi.mode(WIFI_STA);
      if (esp_now_init() != ESP_OK) {
        _serial.printLine("ESP-NOW init failed");
        return;
      }
      
      esp_now_register_send_cb(onDataSentStatic);
      esp_now_register_recv_cb(onDataRecvStatic);
      memcpy(_peerMac, peerMac, 6);

      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, _peerMac, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      
      if (!esp_now_is_peer_exist(_peerMac)) {
        esp_err_t r = esp_now_add_peer(&peerInfo);
        if (r != ESP_OK) {
          _serial.printFmt("Failed to add peer: %d", r);
        }
      }
      _instance = this;
    }

    void sendNow() {
      TxPacket pkt;
      memset(&pkt, 0, sizeof(pkt));
      strncpy(pkt.vehicle_id, VEHICLE_ID, sizeof(pkt.vehicle_id)-1);
      strncpy(pkt.fastag, FASTAG_NUMBER, sizeof(pkt.fastag)-1);
      pkt.total_distance_m = _calc.getTotalDistance();
      pkt.seq = ++_seq;
      
      esp_err_t res = esp_now_send(_peerMac, (uint8_t*)&pkt, sizeof(pkt));
      if (res == ESP_OK) {
        _serial.printSentSuccess();
      } else {
        _serial.printFmt("ESP-NOW send failed: %d", res);
      }
    }

    static TransmittingData* _instance;

  private:
    SerialMonitorPrint &_serial;
    CalculateNHDistance &_calc;
    uint8_t _peerMac[6];
    uint32_t _seq;

    void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
      _serial.printFmt("Send status: %s", 
        status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAILED");
    }

    void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
      if (len < (int)sizeof(RxPacket)) return;
      const RxPacket* rp = (const RxPacket*)data;
      if (rp->cmd == 0x01) {
        // Only reset if Total_Distance is > 0
        if (_calc.getTotalDistance() > 0) {
          _calc.resetAllAndPersistZero();
        }
      }
    }

    friend void onDataSentStatic(const wifi_tx_info_t *info, esp_now_send_status_t status);
    friend void onDataRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
};

TransmittingData* TransmittingData::_instance = nullptr;

void onDataSentStatic(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (TransmittingData::_instance) 
    TransmittingData::_instance->onDataSent(info, status);
}

void onDataRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (TransmittingData::_instance) 
    TransmittingData::_instance->onDataRecv(recv_info, data, len);
}

/* ==================== MAIN SKETCH ==================== */
SerialMonitorPrint monitor;
GPSModule gpsModule(GPSSerial);
SDCardReader sdReader(SD_CS_PIN, COORDS_FILENAME, META_FILENAME);
CompareGpsSd *cmp = nullptr;
CalculateNHDistance *calcNH = nullptr;
TransmittingData *transmitter = nullptr;

unsigned long lastCheck = 0;

void setup() {
  monitor.begin();
  delay(100);

  // Initialize Display
  initDisplay();
  
  gpsModule.begin(9600, GPS_RX_PIN, GPS_TX_PIN);
  monitor.printGpsInit(true);
  delay(100);

  bool sdok = sdReader.begin();
  monitor.printSdInit(sdok);
  if (sdok) monitor.printSdReadSuccess();
  delay(100);

  cmp = new CompareGpsSd(sdReader, monitor);
  calcNH = new CalculateNHDistance(sdReader, monitor);
  calcNH->begin();
  transmitter = new TransmittingData(monitor, *calcNH);
  transmitter->begin(REMOTE_MAC);

  monitor.printLine("===== SYSTEM READY =====");
  lastCheck = millis();
}

void loop() {
  gpsModule.feed();

  unsigned long now = millis();
  if (now - lastCheck >= GPS_CHECK_INTERVAL_MS) {
    lastCheck = now;
    double lat, lon;
    
    if (gpsModule.getLocationIfAccurate(lat, lon)) {
      monitor.printGpsLocation(lat, lon, gps.hdop.hdop());
      
      bool stateChanged = cmp->evaluate(lat, lon);
      bool onNH = cmp->isOnNH();
      
      if (stateChanged) {
        calcNH->updateState(onNH);
      }
      
      calcNH->feedLocationForDistance(lat, lon);
      double nh_km = calcNH->getNHDistance() / 1000.0;
      double total_km = calcNH->getTotalDistance() / 1000.0;
      monitor.printDistances(calcNH->getNHDistance(), calcNH->getTotalDistance());
      
      // Update display
      displayUpdate(VEHICLE_ID, FASTAG_NUMBER, nh_km, total_km, onNH);
    }
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's' || c == 'S') {
      transmitter->sendNow();
    }
  }

  delay(10);
}
