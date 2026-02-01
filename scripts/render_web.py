from fastapi import FastAPI, Response, Request
from fastapi.responses import JSONResponse, FileResponse, HTMLResponse
from fastapi.staticfiles import StaticFiles
import uvicorn
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB_DIR = ROOT / "web"
STATUS_FILE = WEB_DIR / "status.json"
CONFIG_FILE = WEB_DIR / "config.json"

app = FastAPI(title="GNSS-BLE-STATUS (dev server)")

# API endpoints (define before static mount so routes take precedence)
@app.get("/api/status")
async def api_status():
    try:
        data = json.loads(STATUS_FILE.read_text(encoding="utf-8"))
        return JSONResponse(content=data)
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": "failed to read status.json", "detail": str(e)})

@app.post("/api/restart")
async def api_restart():
    # Dummy endpoint for the UI restart button
    return JSONResponse(content={"result": "ok"})

# Dummy config endpoints for the UI save button (dev only).
@app.get("/api/config")
async def api_config_get():
    try:
        if CONFIG_FILE.exists():
            data = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
        else:
            data = {"rx_pin": 10, "tx_pin": 11, "baud": 21000}
        return JSONResponse(content=data)
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": "failed to read config.json", "detail": str(e)})

@app.post("/api/config")
async def api_config_post(req: Request):
    try:
        data = await req.json()
    except Exception:
        return JSONResponse(status_code=400, content={"ok": False, "error": "Invalid JSON"})

    try:
        rx = int(data.get("rx_pin"))
        tx = int(data.get("tx_pin"))
        baud = int(data.get("baud"))
    except Exception:
        return JSONResponse(status_code=400, content={"ok": False, "error": "rx_pin, tx_pin, and baud are required"})

    config = {"rx_pin": rx, "tx_pin": tx, "baud": baud}
    try:
        CONFIG_FILE.write_text(json.dumps(config), encoding="utf-8")
    except Exception as e:
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write config.json", "detail": str(e)})

    return JSONResponse(content={"ok": True, "message": "Config saved", "config": config})

# Serve static files (index.html, app.js, style.css, etc.)
app.mount("/", StaticFiles(directory=str(WEB_DIR), html=True), name="static")

if __name__ == "__main__":
    uvicorn.run("render_web:app", host="127.0.0.1", port=8000, log_level="info", reload=True)
