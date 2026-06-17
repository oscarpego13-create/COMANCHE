#pragma once

#define PLUG_NAME "COMANCHE"
#define PLUG_MFR  "AudioDev"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "1.0.0"
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
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI           1
#define PLUG_WIDTH            900
#define PLUG_HEIGHT           550
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
