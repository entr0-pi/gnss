from fastapi import FastAPI, Response
from fastapi.responses import JSONResponse, FileResponse, HTMLResponse
from fastapi.staticfiles import StaticFiles
import uvicorn
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB_DIR = ROOT / "web"
STATUS_FILE = WEB_DIR / "status.json"

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

# Serve static files (index.html, app.js, style.css, etc.)
app.mount("/", StaticFiles(directory=str(WEB_DIR), html=True), name="static")

if __name__ == "__main__":
    uvicorn.run("render_web:app", host="127.0.0.1", port=8000, log_level="info", reload=False)
