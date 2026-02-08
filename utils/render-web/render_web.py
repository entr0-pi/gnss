from fastapi import FastAPI, Response, Request
from fastapi.responses import JSONResponse, FileResponse, HTMLResponse
from fastapi.staticfiles import StaticFiles
import uvicorn
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WEB_DIR = ROOT / "web"
STATUS_FILE = WEB_DIR / "status.json"
CONFIG_FILE = WEB_DIR / "config.json"
NTRIP_CONFIG_FILE = WEB_DIR / "ntrip_config.json"

app = FastAPI(title="GNSS-BLE-STATUS (dev server)")

DEFAULT_NTRIP_STATUS = {
    "connected": False,
    "healthy": False,
    "streaming": False,
    "bytes_received": 0,
    "total_frames": 0,
    "last_msg_type": 0,
    "last_frame_age_ms": 0,
}

def load_config():
    if not CONFIG_FILE.exists():
        raise FileNotFoundError("config.json not found")
    return json.loads(CONFIG_FILE.read_text(encoding="utf-8"))

def save_config(config):
    CONFIG_FILE.write_text(json.dumps(config, indent=2), encoding="utf-8")

def load_ntrip_config():
    if not NTRIP_CONFIG_FILE.exists():
        return {
            "ntrip": {
                "enabled": False,
                "host": "rtk2go.com",
                "port": 2101,
                "mount": "YOUR_MOUNT",
                "user": "user",
                "pass": "pass",
                "max_tries": 5,
                "retry_delay_ms": 30000,
                "health_timeout_ms": 60000,
                "passive_sample_ms": 5000,
                "required_valid_frames": 3,
                "buffer_size": 1024,
                "connect_timeout_ms": 5000,
            },
            "lockout": {
                "failed_attempts": 0,
                "abandoned": False,
                "last_config_hash": "",
            },
        }
    return json.loads(NTRIP_CONFIG_FILE.read_text(encoding="utf-8"))

def save_ntrip_config(config):
    NTRIP_CONFIG_FILE.write_text(json.dumps(config, indent=2), encoding="utf-8")

# API endpoints (define before static mount so routes take precedence)
@app.get("/api/status")
async def api_status():
    try:
        data = json.loads(STATUS_FILE.read_text(encoding="utf-8"))
        ntrip = data.get("ntrip")
        if not isinstance(ntrip, dict):
            data["ntrip"] = DEFAULT_NTRIP_STATUS.copy()
        else:
            merged = DEFAULT_NTRIP_STATUS.copy()
            merged.update(ntrip)
            data["ntrip"] = merged
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
        data = load_config()
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

    config = load_config()
    config["rx_pin"] = rx
    config["tx_pin"] = tx
    config["baud"] = baud
    try:
        save_config(config)
    except Exception as e:
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write config.json", "detail": str(e)})

    return JSONResponse(content={"ok": True, "message": "Config saved", "config": config})

# Dummy WiFi config endpoints for the UI save button (dev only).
@app.get("/api/wifi_config")
async def api_wifi_config_get():
    try:
        data = load_config()
        payload = {
            "ssid": data["ssid"],
            "pass": data["pass"],
            "dhcp": data["dhcp"],
            "ip": data["ip"],
            "gw": data["gw"],
            "subnet": data["subnet"],
            "dns": data["dns"],
            "locked": data["locked"],
        }
        return JSONResponse(content=payload)
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": "failed to read config.json", "detail": str(e)})

@app.post("/api/wifi_config")
async def api_wifi_config_post(req: Request):
    try:
        data = await req.json()
    except Exception:
        return JSONResponse(status_code=400, content={"ok": False, "error": "Invalid JSON"})

    ssid = str(data.get("ssid", "")).strip()
    if not ssid:
        return JSONResponse(status_code=400, content={"ok": False, "error": "ssid is required"})

    try:
        dhcp = bool(data["dhcp"])
        wifi_pass = data["pass"]
        ip = data["ip"]
        gw = data["gw"]
        subnet = data["subnet"]
        dns = data["dns"]
    except KeyError:
        return JSONResponse(status_code=400, content={"ok": False, "error": "pass, dhcp, ip, gw, subnet, and dns are required"})

    config = load_config()
    config["ssid"] = ssid
    config["pass"] = wifi_pass
    config["dhcp"] = dhcp
    config["ip"] = ip
    config["gw"] = gw
    config["subnet"] = subnet
    config["dns"] = dns

    try:
        save_config(config)
    except Exception as e:
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write config.json", "detail": str(e)})

    return JSONResponse(content={"ok": True, "message": "WiFi config saved", "config": config})

@app.get("/api/ntrip_config")
async def api_ntrip_config_get():
    try:
        data = load_ntrip_config()
        data["locked"] = False
        return JSONResponse(content=data)
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": "failed to read ntrip_config.json", "detail": str(e)})

@app.post("/api/ntrip_config")
async def api_ntrip_config_post(req: Request):
    try:
        data = await req.json()
    except Exception:
        return JSONResponse(status_code=400, content={"ok": False, "error": "Invalid JSON"})

    ntrip = data.get("ntrip")
    if not isinstance(ntrip, dict):
        return JSONResponse(status_code=400, content={"ok": False, "error": "ntrip object is required"})

    try:
        current = load_ntrip_config()
        current["ntrip"] = ntrip
        save_ntrip_config(current)
    except Exception as e:
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write ntrip_config.json", "detail": str(e)})

    return JSONResponse(content={"ok": True, "message": "NTRIP config saved", "config": ntrip})

# Serve static files (index.html, app.js, style.css, etc.)
app.mount("/", StaticFiles(directory=str(WEB_DIR), html=True), name="static")

if __name__ == "__main__":
    uvicorn.run("render_web:app", host="127.0.0.1", port=8000, log_level="info", reload=True)
