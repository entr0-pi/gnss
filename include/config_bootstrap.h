#pragma once

/// Initializes NVS and seeds defaults for all config subsystems.
/// Must be called exactly once in setup(), before any config load/save.
/// Seeds defaults only when NVS is empty and the subsystem is not immutable.
void config_bootstrap();
