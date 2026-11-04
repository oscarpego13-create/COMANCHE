#pragma once

#define PLUG_NAME "COMANCHE"
#define PLUG_MFR  "AudioDev"
#define PLUG_VERSION_HEX 0x00000105
#define PLUG_VERSION_STR "0.1.5"
#define PLUG_UNIQUE_ID 'Cmch'
#define PLUG_MFR_ID    'AuDv'
#define PLUG_URL_STR   ""
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2024 AudioDev"
#define PLUG_CLASS_NAME COMANCHE

#define BUNDLE_NAME "COMANCHE"
#define BUNDLE_MFR  "AudioDev"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "COMANCHE"

// Instrument: no audio input, stereo output
#define PLUG_CHANNEL_IO "0-2"

#define PLUG_LATENCY          0
#define PLUG_TYPE             1   // instrument
#define PLUG_DOES_MIDI_IN     1
#define PLUG_DOES_MIDI_OUT    0
#define PLUG_DOES_MPE         0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI           1
#define PLUG_WIDTH            743
#define PLUG_HEIGHT           331
#define PLUG_FPS              30
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE      0

#define AUV2_ENTRY          COMANCHE_Entry
#define AUV2_ENTRY_STR      "COMANCHE_Entry"
#define AUV2_FACTORY        COMANCHE_Factory
#define AUV2_FACTORY_STR    "COMANCHE_Factory"
#define AUV2_VIEW_CLASS     COMANCHE_View
#define AUV2_VIEW_CLASS_STR "COMANCHE_View"

#define VST3_SUBCATEGORY "Instrument|Sampler"

#define CLAP_MANUAL_URL  ""
#define CLAP_SUPPORT_URL ""
#define CLAP_DESCRIPTION "OneShot Sampler with effects chain"
#define CLAP_FEATURES    "instrument"

#define APP_NUM_CHANNELS       2
#define APP_N_VECTOR_WAIT      0
#define APP_MULT               1
#define APP_COPY_AUV3          0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_MONO_FN "Roboto-Mono-Regular.ttf"

// Whole-UI scale from the UI Lab design ("Tamaño completo"): window = PLUG_WIDTH/HEIGHT x UI_SCALE
#define UI_SCALE 1.10f

// Horse sprite sheet: 12 frames x 3 gaits (walk, trot, gallop), each cell HORSE_FRAME_W x HORSE_FRAME_H
#define HORSE_FN "horse.png"
#define HORSE_FRAME_W 202.0f
#define HORSE_FRAME_H 135.0f
#define TITLE_FN "comanche-title.png"

// Diagnostic breadcrumbs to ~/Library/Application Support/COMANCHE/trace.log (Trace.h)
#define COMANCHE_TRACE 0
