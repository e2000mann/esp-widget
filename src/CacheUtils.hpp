#pragma once
#include <Arduino.h>
#include <vector>
#include <SD.h>
#include <M5CoreS3.h>

struct CachedImage {
  String path;
  String data;
  String mime;
};

struct CachedSound {
  String path;
  std::vector<uint8_t> data;
};

// Storage (declared here, defined in .cpp)
extern std::vector<CachedImage> g_images;
extern std::vector<CachedSound> g_sounds;

// API
void preloadImagesFromFolder(String folder);
CachedImage* findCachedImage(String path);

void preloadSoundsFromFolder(String folder);
CachedSound* findCachedSound(String path);