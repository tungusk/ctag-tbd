#pragma once

#include <stdint.h>

static constexpr uint32_t BOOT_MANIFEST_MAGIC = 0x4d444254; // "TBDM"
static constexpr uint16_t BOOT_MANIFEST_VERSION = 1;

static constexpr uint8_t BOOT_MANIFEST_TRACKS = 19;
static constexpr uint8_t BOOT_MANIFEST_SOUND_TRACKS = 16;
static constexpr uint8_t BOOT_MANIFEST_PARAMS = 24;
static constexpr uint8_t BOOT_MANIFEST_PAGES = 6;
static constexpr uint8_t BOOT_MANIFEST_BANKS = 32;
static constexpr uint8_t BOOT_MANIFEST_KITS = 16;
static constexpr uint8_t BOOT_MANIFEST_SAMPLES = 128;

static constexpr uint8_t BOOT_MANIFEST_ID_LEN = 32;
static constexpr uint8_t BOOT_MANIFEST_KIT_ID_LEN = 16;
static constexpr uint8_t BOOT_MANIFEST_NAME_LEN = 32;
static constexpr uint8_t BOOT_MANIFEST_SHORT_NAME_LEN = 8;
static constexpr uint8_t BOOT_MANIFEST_PAGE_NAME_LEN = 16;
static constexpr uint8_t BOOT_MANIFEST_UI_LEN = 16;
static constexpr uint8_t BOOT_MANIFEST_TEMPLATE_NAME_LEN = 32;

struct BootManifestParam {
  uint8_t slot;
  uint8_t idx;
  uint8_t flags;
  uint8_t ctrlType;
  int16_t ctrl;
  uint16_t resolution;
  float minValue;
  float maxValue;
  float defaultValue;
  float value;
  char name[BOOT_MANIFEST_SHORT_NAME_LEN];
  char ui[BOOT_MANIFEST_UI_LEN];
};

struct BootManifestPage {
  char name[BOOT_MANIFEST_PAGE_NAME_LEN];
};

struct BootManifestTrack {
  uint8_t trackIndex;
  uint8_t valid;
  uint8_t isBusTrack;
  uint8_t isRompler;
  uint8_t romBank;
  int16_t sampleSlice;
  uint8_t pageCount;
  uint8_t paramCount;
  char presetId[BOOT_MANIFEST_ID_LEN];
  char presetName[BOOT_MANIFEST_NAME_LEN];
  char macroId[BOOT_MANIFEST_ID_LEN];
  char macroName[BOOT_MANIFEST_NAME_LEN];
  char machineId[BOOT_MANIFEST_ID_LEN];
  BootManifestPage pages[BOOT_MANIFEST_PAGES];
  BootManifestParam params[BOOT_MANIFEST_PARAMS];
};

struct BootManifestKit {
  char id[BOOT_MANIFEST_KIT_ID_LEN];
  char name[BOOT_MANIFEST_NAME_LEN];
};

struct BootManifestBank {
  uint16_t index;
  uint16_t startIndex;
  uint16_t sampleCount;
  char name[BOOT_MANIFEST_NAME_LEN];
};

struct BootManifestSample {
  char name[BOOT_MANIFEST_NAME_LEN];
};

struct BootManifest {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint32_t totalSize;
  uint32_t flags;
  char activeTemplate[BOOT_MANIFEST_TEMPLATE_NAME_LEN];
  char kitId[BOOT_MANIFEST_KIT_ID_LEN];
  char kitName[BOOT_MANIFEST_NAME_LEN];
  uint8_t kitIndex;
  uint8_t trackCount;
  uint8_t kitCount;
  uint8_t bankCount;
  uint8_t sampleCount;
  uint8_t reserved[3];
  BootManifestKit kits[BOOT_MANIFEST_KITS];
  BootManifestTrack tracks[BOOT_MANIFEST_TRACKS];
  BootManifestBank banks[BOOT_MANIFEST_BANKS];
  BootManifestSample samples[BOOT_MANIFEST_SAMPLES];
};

static_assert(sizeof(BootManifestParam) == 48, "BootManifestParam layout changed");
static_assert(sizeof(BootManifestPage) == 16, "BootManifestPage layout changed");
static_assert(sizeof(BootManifestKit) == 48, "BootManifestKit layout changed");
static_assert(sizeof(BootManifestBank) == 38, "BootManifestBank layout changed");
static_assert(sizeof(BootManifestSample) == 32, "BootManifestSample layout changed");
