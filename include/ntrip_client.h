#pragma once

#include "app.h"
#include "freertos/stream_buffer.h"

struct NtripClientSnapshot {
  bool connected;
  bool healthy;
  bool streaming;
  uint32_t bytesReceived;
  uint32_t totalFrames;
  uint16_t lastMessageType;
  uint32_t lastFrameAgeMs;
  uint8_t protocolVersion;  // 1 = Rev1, 2 = Rev2, 0 = not connected
};

#if NTRIP_CLIENT_ENABLE
void ntrip_client_setup(StreamBufferHandle_t sb_ntrip2uart);
void ntrip_client_loop();
bool ntrip_client_get_snapshot(NtripClientSnapshot& out);
#else
inline void ntrip_client_setup(StreamBufferHandle_t) {}
inline void ntrip_client_loop() {}
inline bool ntrip_client_get_snapshot(NtripClientSnapshot&) { return false; }
#endif
