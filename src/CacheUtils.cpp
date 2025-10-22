#include <SD.h>
#include <M5CoreS3.h>

#include "CacheUtils.hpp"

// Define storage exactly once
std::vector<CachedImage> g_images;
std::vector<CachedSound> g_sounds;

void preloadImagesFromFolder(String folder) {
  File dir = SD.open(folder);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("❌ %s is not a directory\n", folder);
    return;
  }

  while (true) {
    File file = dir.openNextFile();
    if (!file) {
      break;
    }
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    String name = file.name();
    if (!name.endsWith(".webp") && !name.endsWith(".png")) {
      file.close();
      continue;
    }

    CoreS3.Display.printf("\n Caching file %s\n", name.c_str());

    size_t size = file.size();
    CachedImage img;
    img.mime = name.endsWith(".webp") ? "image/webp" :
           name.endsWith(".png")  ? "image/png"  :
           "application/octet-stream";
    img.path = folder + "/" + name;
    img.data.reserve(size);
    while (file.available()) {
      img.data += (char)file.read();
    }
    file.close();

    Serial.printf("Cached %s (%u bytes)\n", img.path.c_str(), img.data.length());
    g_images.push_back(std::move(img));
  }
  dir.close();
}

CachedImage* findCachedImage(String path) {
  Serial.printf("Using cached image %s\n", path.c_str());
  for (auto &img : g_images)
    if (img.path.equals(path))
      return &img;
  return nullptr;
}

void preloadSoundsFromFolder(String folder) {
  File dir = SD.open(folder);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("❌ %s is not a directory\n", folder);
    dir.close();
    return;
  }

  while (true) {
    File file = dir.openNextFile();
    if (!file) break;
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    String name = file.name();
    if (!name.endsWith(".wav")) {
      file.close();
      continue;
    }

    CoreS3.Display.printf("\n Caching file %s\n", name.c_str());

    CachedSound sound;
    sound.path = folder + "/" + name;
    sound.data.resize(file.size());

    size_t bytesRead = file.read(sound.data.data(), file.size());
    file.close();

    Serial.printf("Cached %s (%u bytes)\n", sound.path.c_str(), sound.data.size());
    g_sounds.push_back(std::move(sound));
  }
  dir.close();
}

CachedSound* findCachedSound(String path) {
  Serial.printf("Using cached sound %s\n", path.c_str());
  for (auto &sound : g_sounds)
    if (sound.path.equals(path))
      return &sound;
  return nullptr;
}