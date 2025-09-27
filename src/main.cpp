// downloaded libraries
#include <Arduino.h>
#include <M5CoreS3.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <espeak.h>
#include <espeak-ng-data.h>

// custom libraries
#include "WavUtils.h"
#include "MemoryBufferStream.h"

// pins & things
#define SD_SPI_SCK_PIN  36
#define SD_SPI_MISO_PIN 35
#define SD_SPI_MOSI_PIN 37
#define SD_SPI_CS_PIN   4
#define AUDIO_SAMPLE_RATE 22050

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
bool displayPngImage();
void changeMoodMatrix(const String& chosenFace);
void changeTTS(const String& text);
void changeSettings(const String& brightness, const String& volume);
bool speakOutLoud();
void initDisplay();
void initSpeaker();
void initTTS();
void initSDCard();
void initWiFiAndWebServer();

MemoryBufferStream memoryStream;
ESpeak espeak(memoryStream);

// global variables
volatile bool g_drawPending = true;
String g_nextPath = "/mood_matrix/default.png"; // initial face
volatile bool g_busyDrawing = false;

volatile bool g_speakPending = true;
String g_ttsSentence = "This is a test";
volatile bool g_busySpeaking = false;

volatile int g_currentBrightness = 255;
volatile int g_currentVolume = 128;

static constexpr size_t kPcmCapacitySamples = AUDIO_SAMPLE_RATE * 60; // 60 sec max length for audio

void initDisplay() {
  CoreS3.Display.setSwapBytes(true);
  int textsize = CoreS3.Display.height() / 60;
  if (textsize == 0) {
      textsize = 1;
  }
  CoreS3.Display.setTextSize(textsize);
  CoreS3.Display.setRotation(1);
  CoreS3.Display.setBrightness(g_currentBrightness);
}

void initSpeaker() {
  CoreS3.Speaker.begin();
  CoreS3.Speaker.setVolume(g_currentVolume);
}

void initTTS() {
  if (!memoryStream.begin(kPcmCapacitySamples)) {
    CoreS3.Display.print("\nPSRAM alloc failed for memoryStream\n");
    while (1)
      ;
  }

  // female voice (default en voice too male for widget imo :()
  espeak.add("/mem/data/voices/!v/f4", 
               espeak_ng_data_voices__v_f4, 
               espeak_ng_data_voices__v_f4_len);

  if (!espeak.begin()) {
    Serial.println("Did not load espeak");
  }

  espeak.setVoice("en+f4");
  espeak.setRate(140);
}

void initSDCard() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    // Print a message if SD card initialization failed or if the SD card does not exist.
    CoreS3.Display.print("\n SD card not detected\n");
    while (1)
      ;
  } else {
    CoreS3.Display.print("\n SD card detected\n");
  }
  delay(1000);
}

void initWiFiAndWebServer() {
  // Initialise WiFi Connection
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, gw, mask);
  bool apOK = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println(WiFi.softAPIP());

  // Initialise web server
  auto *moodMatrixHandler = new AsyncCallbackJsonWebHandler(
    "/mood-matrix",
    [](AsyncWebServerRequest* request, JsonVariant &json) {
      // Body is already parsed here
      String chosenFace = json["chosenFace"] | "";
      if (chosenFace.isEmpty()) {
        request->send(400, "application/json", R"({"error":"Incorrect JSON. Expecting {chosenFace: 'faceName'}"})");
        return;
      }

      Serial.println("mood matrix req recieved");

      changeMoodMatrix(chosenFace);

      request->send(200, "application/json", R"({"ok":true})");
    }
  );
  moodMatrixHandler->setMaxContentLength(256);
  server.addHandler(moodMatrixHandler);
  
  auto *ttsHandler = new AsyncCallbackJsonWebHandler(
    "/tts",
    [](AsyncWebServerRequest* request, JsonVariant &json) {
      // Body is already parsed here
      String text = json["text"] | "";
      if (text.isEmpty()) {
        request->send(400, "application/json", R"({"error":"Incorrect JSON. Expecting {text: 'text goes here'}"})");
        return;
      }

      Serial.println("tts req recieved");

      changeTTS(text);

      request->send(200, "application/json", R"({"ok":true})");
    }
  );
  ttsHandler->setMaxContentLength(256);
  server.addHandler(ttsHandler);

  auto *settingsHandler = new AsyncCallbackJsonWebHandler(
    "/settings",
    [](AsyncWebServerRequest* request, JsonVariant &json) {
      // Body is already parsed here
      String brightness = json["brightness"] | "";
      String volume = json["volume"] | "";
    
      bool validBrightness = !brightness.isEmpty() && brightness.length() > 0;
      bool validVolume = !volume.isEmpty() && volume.length() > 0;

      if (!validBrightness || !validVolume) {
        request->send(400, "application/json", R"({"error":"Incorrect JSON. Expecting {text: 'text goes here'}"})");
        return;
      }

      Serial.println("settings req recieved");

      changeSettings(brightness, volume);

      request->send(200, "application/json", R"({"ok":true})");
    }
  );
  settingsHandler->setMaxContentLength(256);
  server.addHandler(settingsHandler);

  server.serveStatic("/", SD, "/www/").setDefaultFile("index.html");

  server.begin();
}

void setup() {
  // Initialise serial connection
  Serial.begin(115200);
  delay(200);
  Serial.println("Alive");

  // Initialise CoreS3
  auto cfg = M5.config();
  CoreS3.begin(cfg);

  initDisplay();
  initSpeaker();
  initTTS();
  initSDCard();
  initWiFiAndWebServer();
}

void loop() {
  if (g_drawPending && !g_busyDrawing) {
    g_drawPending = false;
    g_busyDrawing = true;

    bool ok = displayPngImage();

    g_busyDrawing = false;

    yield();
  }

  if (g_speakPending && !g_busySpeaking) {
    g_speakPending = false;
    g_busySpeaking = true;

    bool ok = speakOutLoud();

    g_busySpeaking = false;

    yield();
  }
}

// functions
bool displayPngImage() {
  const char* filename = g_nextPath.c_str();

  Serial.println("Drawing PNG...");
  Serial.println(filename);

  fs::File imgFile = SD.open(filename, FILE_READ);
  if (!imgFile) {
    Serial.println("Failed to open PNG!");
    return false;
  }

  bool displaySet = CoreS3.Display.drawPng(&imgFile, 0, 0);

  imgFile.close();
  
  Serial.println(displaySet ? "PNG Loaded" : "Failed to load PNG");
  return displaySet;
}

void changeMoodMatrix(const String& chosenFace) {
  String path = "/mood_matrix/" + chosenFace + ".png";

  g_nextPath = path;
  g_drawPending = true;
}

void changeTTS(const String& text) {
  g_ttsSentence = text;
  g_speakPending = true;
}

void changeSettings(const String& brightness, const String& volume) {
  int brightnessNum = brightness.toInt();
  int volumeNum = volume.toInt();

  if (brightnessNum != g_currentBrightness) {
    g_currentBrightness = brightnessNum;
    CoreS3.Display.setBrightness(g_currentBrightness);
  }

  if (volumeNum != g_currentVolume) {
    g_currentVolume = volumeNum;
    CoreS3.Speaker.setVolume(g_currentVolume);
  }
}

bool speakOutLoud() {
  memoryStream.clear();
  const char* sentence = g_ttsSentence.c_str();

  Serial.println(sentence);

  // Kick off synthesis into MemoryBufferStream
  bool ttsRunning = espeak.say(sentence);

  if (!ttsRunning) {
    Serial.println("TTS failed");
    return false;
  }
  
  // Wait for the buffer to START filling (up to 300 ms)
  uint32_t t0 = millis();
  while (memoryStream.size() == 0 && millis() - t0 < 300) {
    delay(5);
  }

  static std::vector<uint8_t> wav;
  make_wav_from_pcm16(memoryStream.data(), memoryStream.size(), 22050, wav);

  CoreS3.Speaker.stop();
  delay(10);
  bool audioOutput = CoreS3.Speaker.playWav(wav.data(), wav.size());

  // Let DMA drain
  const uint32_t bytes_per_sec = AUDIO_SAMPLE_RATE * 2;
  delay((wav.size() * 1000) / bytes_per_sec + 50);

  return audioOutput;
};
