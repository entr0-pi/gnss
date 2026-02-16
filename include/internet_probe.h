#pragma once

#include "app.h"

#if WIFI_ENABLE

// Returns cached internet reachability.
// Probe runs at most once per 10 seconds.
bool internet_probe_is_reachable();

#else

inline bool internet_probe_is_reachable() { return false; }

#endif
