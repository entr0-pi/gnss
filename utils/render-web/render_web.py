from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
import uvicorn
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT_DIR = Path(__file__).resolve().parent
WEB_DIR = ROOT / "data" / "web"
LEGACY_WEB_DATA_DIR = WEB_DIR
MOCK_DATA_FILE = SCRIPT_DIR / "mock_data.json"

def _resolve_data_file(filename: str) -> Path:
    """Prefer utils/render-web/<file>, fallback to legacy data/web/<file>."""
    local = SCRIPT_DIR / filename
    legacy = LEGACY_WEB_DATA_DIR / filename
    if local.exists():
        return local
    if legacy.exists():
        return legacy
    return local

STATUS_FILE = _resolve_data_file("status.json")
CONFIG_FILE = _resolve_data_file("config.json")
NTRIP_CONFIG_FILE = _resolve_data_file("ntrip_config.json")

app = FastAPI(title="GNSS-BLE-STATUS (dev server)")

DEFAULT_NTRIP_STATUS = {
    "connected": False,
    "healthy": False,
    "streaming": False,
    "bytes_received": 0,
    "total_frames": 0,
    "last_msg_type": 0,
    "last_frame_age_ms": 0,
    "protocol_version": 0,
}

DEFAULT_MOCK_DATA = {
    "config": {
        "rx_pin": 16,
        "tx_pin": 17,
        "baud": 9600,
        "ssid": "your-ssid",
        "pass": "your-password",
        "dhcp": True,
        "accesspoint": True,
        "ip": "192.168.1.50",
        "gw": "192.168.1.1",
        "subnet": "255.255.255.0",
        "dns": "8.8.8.8",
        "locked": False,
    },
    "status": {},
    "ntrip_config": {
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
            "send_gga": False,
        },
        "lockout": {
            "failed_attempts": 0,
            "abandoned": False,
            "last_config_hash": "",
        },
    },
}


def load_mock_data():
    if MOCK_DATA_FILE.exists():
        return json.loads(MOCK_DATA_FILE.read_text(encoding="utf-8"))

    # Backward-compatible migration from legacy split files.
    migrated = json.loads(json.dumps(DEFAULT_MOCK_DATA))
    try:
        if CONFIG_FILE.exists():
            cfg = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
            migrated["config"] = {
                "rx_pin": cfg.get("rx_pin", 16),
                "tx_pin": cfg.get("tx_pin", 17),
                "baud": cfg.get("baud", 9600),
                "ssid": cfg.get("ssid", "your-ssid"),
                "pass": cfg.get("pass", "your-password"),
                "dhcp": cfg.get("dhcp", True),
                "accesspoint": cfg.get("accesspoint", True),
                "ip": cfg.get("ip", "192.168.1.50"),
                "gw": cfg.get("gw", "192.168.1.1"),
                "subnet": cfg.get("subnet", "255.255.255.0"),
                "dns": cfg.get("dns", "8.8.8.8"),
                "locked": cfg.get("locked", False),
            }
        if STATUS_FILE.exists():
            migrated["status"] = json.loads(STATUS_FILE.read_text(encoding="utf-8"))
        if NTRIP_CONFIG_FILE.exists():
            migrated["ntrip_config"] = json.loads(NTRIP_CONFIG_FILE.read_text(encoding="utf-8"))
        elif CONFIG_FILE.exists():
            cfg = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
            if isinstance(cfg.get("ntrip"), dict):
                migrated["ntrip_config"]["ntrip"] = cfg["ntrip"]
            if isinstance(cfg.get("lockout"), dict):
                migrated["ntrip_config"]["lockout"] = cfg["lockout"]
    except Exception:
        pass

    save_mock_data(migrated)
    return migrated


def save_mock_data(data):
    MOCK_DATA_FILE.parent.mkdir(parents=True, exist_ok=True)
    MOCK_DATA_FILE.write_text(json.dumps(data, indent=2), encoding="utf-8")

def load_config():
    data = load_mock_data()
    return data.get("config", {})

def save_config(config):
    data = load_mock_data()
    data["config"] = config
    save_mock_data(data)

def load_ntrip_config():
    data = load_mock_data()
    return data.get("ntrip_config", DEFAULT_MOCK_DATA["ntrip_config"])

def save_ntrip_config(config):
    data = load_mock_data()
    data["ntrip_config"] = config
    save_mock_data(data)

# API endpoints (define before static mount so routes take precedence)
@app.get("/api/status")
async def api_status():
    try:
        data = load_mock_data().get("status", {})
        ntrip = data.get("ntrip")
        if not isinstance(ntrip, dict):
            data["ntrip"] = DEFAULT_NTRIP_STATUS.copy()
        else:
            merged = DEFAULT_NTRIP_STATUS.copy()
            merged.update(ntrip)
            data["ntrip"] = merged
        return JSONResponse(content=data)
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": "failed to read mock status", "detail": str(e)})

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
        return JSONResponse(status_code=500, content={"error": "failed to read mock config", "detail": str(e)})

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
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write mock config", "detail": str(e)})

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
            "accesspoint": data.get("accesspoint", True),
            "ip": data["ip"],
            "gw": data["gw"],
            "subnet": data["subnet"],
            "dns": data["dns"],
            "locked": data["locked"],
        }
        return JSONResponse(content=payload)
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": "failed to read mock config", "detail": str(e)})

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
        accesspoint = bool(data.get("accesspoint", True))
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
    config["accesspoint"] = accesspoint
    config["ip"] = ip
    config["gw"] = gw
    config["subnet"] = subnet
    config["dns"] = dns

    try:
        save_config(config)
    except Exception as e:
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write mock config", "detail": str(e)})

    return JSONResponse(content={"ok": True, "message": "WiFi config saved", "config": config})

@app.get("/api/ntrip_config")
async def api_ntrip_config_get():
    try:
        data = load_ntrip_config()
        data["locked"] = False
        return JSONResponse(content=data)
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": "failed to read mock ntrip_config", "detail": str(e)})

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
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write mock ntrip_config", "detail": str(e)})

    return JSONResponse(content={"ok": True, "message": "NTRIP config saved", "config": ntrip})

# Serve static files (index.html, app.js, style.css, etc.)
app.mount("/", StaticFiles(directory=str(WEB_DIR), html=True), name="static")

if __name__ == "__main__":
    uvicorn.run("render_web:app", host="127.0.0.1", port=8000, log_level="info", reload=True)
