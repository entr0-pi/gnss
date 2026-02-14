---
name: gnss-webui-dev-loop
description: Run a fast local Web UI development loop with mock API data via utils/render-web, including status/config/wifi/ntrip endpoint validation.
---

# GNSS WebUI Dev Loop

Use this skill for frontend iterations and API contract checks without hardware.

## Workflow

1. Start local server:
   - `python utils/render-web/render_web.py`
2. Validate key endpoints:
   - `GET /api/status`
   - `GET/POST /api/config`
   - `GET/POST /api/wifi_config`
   - `GET/POST /api/ntrip_config`
3. Update `utils/render-web/mock_data.json` with scenario variants:
   - no-fix vs fixed GNSS
   - NTRIP disconnected vs streaming
4. Verify UI behavior for masked secrets and lock state.
5. Report issues as:
   - API mismatch
   - UI rendering issue
   - state persistence issue

## Deliverables

- Run commands
- Scenario matrix
- Defect list with repro steps
