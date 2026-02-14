# Web UI Render/Mock API Server

Development server for rendering the GNSS web UI (`data/web`) with local mock API endpoints.

## What this tool does

`render_web.py` starts a FastAPI app that:

- Serves the static web UI from `data/web`
- Exposes mock API routes used by the frontend (`/api/status`, `/api/config`, `/api/wifi_config`, `/api/ntrip_config`, etc.)
- Stores and persists mock state in `utils/render-web/mock_data.json`
- Migrates legacy config/status files into the unified mock data format when needed

This lets you iterate on frontend behavior without flashing hardware.

## How to use

From the repo root:

```bash
python utils/render-web/render_web.py
```

Then open:

- `http://127.0.0.1:8000`

### Requirements

Install dependencies first (for example in a virtual environment):

```bash
pip install fastapi uvicorn
```

## Key features

- **Static + API in one process**: frontend files and backend mock endpoints served together.
- **Persistent mock state**: writes updates to `mock_data.json` so refreshes keep state.
- **Validation and guardrails**: basic schema checks for UART/Wi-Fi/NTRIP payloads with clear HTTP errors.
- **Password masking support**: preserves existing Wi-Fi password when frontend sends masked value.
- **No-cache middleware**: disables HTTP caching to make frontend edits immediately visible.
- **Backward-compatible migration**: reads older `config.json`/`status.json`/`ntrip_config.json` layouts.
