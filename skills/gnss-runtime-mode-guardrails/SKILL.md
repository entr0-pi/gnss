---
name: gnss-runtime-mode-guardrails
description: Verify and document runtime behavior differences between mutable and locked firmware modes, including expected API write protections and configuration sources.
---

# GNSS Runtime Mode Guardrails

Use this skill when users need confidence about mutable vs production-locked behavior.

## Workflow

1. Identify mode inputs:
   - lock flags (`FORCE_WIFI_SECRETS`, `FORCE_HARDCODED_UART`)
   - enabled feature set
2. Produce endpoint behavior matrix for each mode:
   - `/api/config`
   - `/api/wifi_config`
   - `/api/ntrip_config`
3. Expected outcomes:
   - mutable mode: POST accepted + persisted
   - locked mode: POST rejected (`403`) where applicable
4. Provide verification steps using Web UI or HTTP requests.
5. Summarize operator guidance (what must be changed at build-time vs runtime).

## Output

- Mode matrix (endpoint × mode × expected status)
- Compliance checklist for release builds
