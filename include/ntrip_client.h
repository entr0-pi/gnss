#pragma once

#include "app.h"
#include "freertos/stream_buffer.h"

#if NTRIP_CLIENT_ENABLE
void ntrip_client_setup(StreamBufferHandle_t sb_ntrip2uart);
void ntrip_client_loop();
#else
inline void ntrip_client_setup(StreamBufferHandle_t) {}
inline void ntrip_client_loop() {}
#endif
