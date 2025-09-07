// downloaded libraries
#include <Arduino.h>
#include <M5CoreS3.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// custom libraries

// pins
#define SD_SPI_SCK_PIN  36
#define SD_SPI_MISO_PIN 35
#define SD_SPI_MOSI_PIN 37
#define SD_SPI_CS_PIN   4

// secrets
#ifndef AP_SSID
  #error "AP_SSID not defined"
#endif
#ifndef AP_PASS
  #error "AP_PASS not defined"
#endif

// web server variables
AsyncWebServer server(80);
IPAddress apIP(10, 0, 0, 1);
IPAddress gw  (10, 0, 0, 1);
IPAddress mask(255, 255, 255, 0);

// function declarations
void displayPngImage(const char *filename);

void setup() {
  // Initialise serial connection
  Serial.begin(115200);
  delay(200);
  Serial.println("Alive");

  // Initialise CoreS3 & Display
  auto cfg = M5.config();
  CoreS3.begin(cfg);
  CoreS3.Display.setSwapBytes(true);

  int textsize = CoreS3.Display.height() / 60;
  if (textsize == 0) {
      textsize = 1;
  }
  CoreS3.Display.setTextSize(textsize);
  CoreS3.Display.setRotation(1);
  CoreS3.Display.setBrightness(255);

  // Initialise SD card
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    // Print a message if SD card initialization failed or if the SD card does not exist.
    M5.Display.print("\n SD card not detected\n");
    while (1)
      ;
  } else {
    M5.Display.print("\n SD card detected\n");
  }
  delay(1000);

  // Initialise WiFi Connection
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, gw, mask);
  bool apOK = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println(WiFi.softAPIP());

  // Initialise web server
  server.serveStatic("/", SD, "/www/").setDefaultFile("index.html");
  server.begin();

  // show initial picture
  displayPngImage("/mood_matrix/pleased.png");
}

void loop() {
  // put your main code here, to run repeatedly:
}

// functions
void displayPngImage(const char *filename) {
  Serial.println("Drawing PNG...");
  Serial.println(filename);

  fs::File imgFile = SD.open(filename, FILE_READ);
  if (!imgFile) {
    Serial.println("Failed to open PNG!");
    return;
  }

  bool displaySet = CoreS3.Display.drawPng(&imgFile, 0, 0);

  imgFile.close();
  
  Serial.println(displaySet ? "PNG Loaded" : "Failed to load PNG");
}
