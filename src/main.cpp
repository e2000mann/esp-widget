// downloaded libraries
#include <Arduino.h>
#include <M5CoreS3.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
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

// image caching into psram
struct CachedImage {
  String path;
  String data;
  String mime;
};
std::vector<CachedImage> images;

// function declarations
void preloadImagesFromFolder(String folder);
CachedImage* findCachedImage(String path);
void handlePlainTextPost(AsyncWebServerRequest* request,
                         uint8_t* data, size_t len, size_t index, size_t total,
                         void (*callback)(const String&)) ;
void addPlainTextEndpoint(const char* path, void (*callback)(const String&));
bool displayMoodMatrix();
bool displaySound();
bool displayPngImage(String filename);
void changeMoodMatrix(const String& chosenFace);
void changeTTS(const String& text);
void changeBrightness(const String& brightness);
void changeVolume(const String& volume);
bool speakOutLoud();
bool playSound();
void callOutSound(const String& sound);
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

volatile bool g_speakPending = false;
String g_ttsSentence = "";
volatile bool g_busySpeaking = false;

volatile int g_currentBrightness = 255;
volatile int g_currentVolume = 128;

volatile bool g_soundPending = false;
String g_soundName = "";
volatile bool g_busyWithSound = false;

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

  preloadImagesFromFolder("/mood_matrix");
  preloadImagesFromFolder("/sounds");
  preloadImagesFromFolder("/www/assets/mm");
  preloadImagesFromFolder("/www/assets/sounds");
}

void initWiFiAndWebServer() {
  // Initialise WiFi Connection
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, gw, mask);
  bool apOK = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println(WiFi.softAPIP());

  // Initialise web server
  addPlainTextEndpoint("/sound", callOutSound);
  addPlainTextEndpoint("/mood-matrix", changeMoodMatrix);
  addPlainTextEndpoint("/tts", changeTTS);
  addPlainTextEndpoint("/brightness", changeBrightness);
  addPlainTextEndpoint("/volume", changeVolume);

  server.serveStatic("/", SD, "/www/")
    .setDefaultFile("index.html")
    .setFilter([](AsyncWebServerRequest *req) {
      // reject /assets/ paths so cache/onNotFound can handle them
      return !req->url().startsWith("/assets/");
    });

  server.onNotFound([](AsyncWebServerRequest *req) {
    String url = "/www" + req->url();
    CachedImage* img = findCachedImage(url);

    if (img) {
      req->send_P(200, img->mime, (uint8_t*)img->data.c_str(), img->data.length());
      return;
    }

    req->send(404, "text/plain", "Not found");
  });

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

    bool ok = displayMoodMatrix();

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

  if (g_soundPending && !g_busyWithSound) {
    g_soundPending = false;
    g_busyWithSound = true;

    bool displayOk = displaySound();
    bool audioOK = playSound();

    delay(1000);

    g_busyWithSound = false;
    g_drawPending = true; // revert to mood matrix image
  }
}

// functions
void preloadImagesFromFolder(String folder) {
  File dir = SD.open(folder);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("❌ %s is not a directory\n", folder);
    return;
  }

  while (true) {
    File file = dir.openNextFile();
    if (!file) break;
    if (file.isDirectory()) continue;

    String name = file.name();
    if (!name.endsWith(".webp") && !name.endsWith(".png")) {
      continue;
    }

    size_t size = file.size();
    CachedImage img;
    img.mime = name.endsWith(".webp") ? "image/webp" :
           name.endsWith(".png")  ? "image/png"  :
           "application/octet-stream";
    img.path = folder + "/" + name;
    img.data.reserve(size);
    while (file.available()) img.data += (char)file.read();
    file.close();

    Serial.printf("Cached %s (%u bytes)\n", img.path.c_str(), img.data.length());
    images.push_back(std::move(img));
  }
  dir.close();
}

CachedImage* findCachedImage(String path) {
  Serial.printf("Using cached image %s\n", path.c_str());
  for (auto &img : images)
    if (img.path.equals(path))
      return &img;
  return nullptr;
}

void handlePlainTextPost(AsyncWebServerRequest* request,
                         uint8_t* data, size_t len, size_t index, size_t total,
                         void (*callback)(const String&)) {
  // Handle chunked requests (mostly for tts)
  if (index == 0) request->_tempObject = new String();
  String* body = static_cast<String*>(request->_tempObject);
  body->concat(reinterpret_cast<const char*>(data), len);

  // Final chunk?
  if (index + len == total) {
    body->trim();

    if (body->isEmpty()) {
      request->send(400, "application/json",
                    R"({"error":"Empty body. Send plain text."})");
    } else {
      Serial.printf("[%s] received: %s\n", request->url().c_str(), body->c_str());
      callback(*body);
      request->send(200, "application/json", R"({"ok":true})");
    }

    delete body;
    request->_tempObject = nullptr;
  }
}

void addPlainTextEndpoint(const char* path, void (*callback)(const String&)) {
  server.on(path, HTTP_POST,
    [](AsyncWebServerRequest* request) {},
    nullptr,
    [callback](AsyncWebServerRequest* request,
               uint8_t* data, size_t len, size_t index, size_t total) {
      handlePlainTextPost(request, data, len, index, total, callback);
    }
  );
}

bool displayMoodMatrix() {
  bool displaySet = displayPngImage(g_nextPath);
  return displaySet;
}

bool displaySound() {
  String combinedFileName = "/sounds/" + g_soundName + ".png";

  bool displaySet = displayPngImage(combinedFileName);
  return displaySet;
}

bool displayPngImage(String filename) {
  Serial.println("Drawing PNG...");
  Serial.println(filename);

  CachedImage* img = findCachedImage(filename);

  if (!img) {
    Serial.println("Failed to load PNG (cache)");
    return false;
  }

  bool displaySet = CoreS3.Display.drawPng((uint8_t*)img->data.c_str(), img->data.length(), 0, 0);
  
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

void changeBrightness(const String& brightness) {
  int brightnessNum = brightness.toInt();

  if (brightnessNum != g_currentBrightness) {
    g_currentBrightness = brightnessNum;
    CoreS3.Display.setBrightness(g_currentBrightness);
  }
}

void changeVolume(const String& volume) {
  int volumeNum = volume.toInt();

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

bool playSound() {
  String combinedFileName = "/sounds/" + g_soundName + ".wav";
  const char* filename = combinedFileName.c_str();

  fs::File soundFile = SD.open(filename, FILE_READ);
  if (!soundFile) {
    Serial.println("Failed to open WAV!");
    return false;
  }

  size_t fileSize = soundFile.size();
  uint8_t* wavData = (uint8_t*)malloc(fileSize);

  size_t bytesRead = soundFile.read(wavData, fileSize);
  soundFile.close();

  if (bytesRead != fileSize) {
      Serial.printf("Read error: %d/%d bytes\n", bytesRead, fileSize);
      free(wavData);
      return false;
  }

  bool audioOutput = CoreS3.Speaker.playWav(wavData, fileSize);
  
  free(wavData);
  return audioOutput;
}

void callOutSound(const String& sound) {
  g_soundName = sound;
  g_soundPending = true;
}