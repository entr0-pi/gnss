from fastapi import FastAPI, Request
from fastapi import Response
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
import uvicorn
import json
from pathlib import Path
import ipaddress

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

@app.middleware("http")
async def disable_cache(request: Request, call_next):
    response = await call_next(request)
    response.headers["Cache-Control"] = "no-store, no-cache, must-revalidate, max-age=0"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response

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
PASS_MASK = "********"

DEFAULT_MOCK_DATA = {
    "uart_config": {
        "rx_pin": 16,
        "tx_pin": 17,
        "baud": 9600,
        "locked": False,
    },
    "wifi_config": {
        "ssid": "your-ssid",
        "pass": "your-password",
        "dhcp": True,
        "accesspoint": True,
        "dual_mode_supported": True,
        "ip": "192.168.1.50",
        "gw": "192.168.1.1",
        "subnet": "255.255.255.0",
        "dns": "8.8.8.8",
        "locked": False,
    },
    "status": {},
    "ntrip_config": {
        "locked": False,
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
        data = json.loads(MOCK_DATA_FILE.read_text(encoding="utf-8"))
        # In-file migration from legacy combined "config" to split configs.
        if "config" in data and ("uart_config" not in data or "wifi_config" not in data):
            cfg = data.get("config", {})
            data["uart_config"] = {
                "rx_pin": cfg.get("rx_pin", 16),
                "tx_pin": cfg.get("tx_pin", 17),
                "baud": cfg.get("baud", 9600),
                "locked": cfg.get("uart_locked", cfg.get("locked", False)),
            }
            data["wifi_config"] = {
                "ssid": cfg.get("ssid", "your-ssid"),
                "pass": cfg.get("pass", "your-password"),
                "dhcp": cfg.get("dhcp", True),
                "accesspoint": cfg.get("accesspoint", True),
                "dual_mode_supported": cfg.get("dual_mode_supported", True),
                "ip": cfg.get("ip", "192.168.1.50"),
                "gw": cfg.get("gw", "192.168.1.1"),
                "subnet": cfg.get("subnet", "255.255.255.0"),
                "dns": cfg.get("dns", "8.8.8.8"),
                "locked": cfg.get("wifi_locked", cfg.get("locked", False)),
            }
            data.pop("config", None)
            save_mock_data(data)
        return data

    # Backward-compatible migration from legacy split files.
    migrated = json.loads(json.dumps(DEFAULT_MOCK_DATA))
    try:
        if CONFIG_FILE.exists():
            cfg = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
            migrated["uart_config"] = {
                "rx_pin": cfg.get("rx_pin", 16),
                "tx_pin": cfg.get("tx_pin", 17),
                "baud": cfg.get("baud", 9600),
                "locked": cfg.get("uart_locked", cfg.get("locked", False)),
            }
            migrated["wifi_config"] = {
                "ssid": cfg.get("ssid", "your-ssid"),
                "pass": cfg.get("pass", "your-password"),
                "dhcp": cfg.get("dhcp", True),
                "accesspoint": cfg.get("accesspoint", True),
                "dual_mode_supported": cfg.get("dual_mode_supported", True),
                "ip": cfg.get("ip", "192.168.1.50"),
                "gw": cfg.get("gw", "192.168.1.1"),
                "subnet": cfg.get("subnet", "255.255.255.0"),
                "dns": cfg.get("dns", "8.8.8.8"),
                "locked": cfg.get("wifi_locked", cfg.get("locked", False)),
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

def load_uart_config():
    data = load_mock_data()
    return data.get("uart_config", DEFAULT_MOCK_DATA["uart_config"])

def save_uart_config(config):
    data = load_mock_data()
    data["uart_config"] = config
    save_mock_data(data)

def load_wifi_config():
    data = load_mock_data()
    return data.get("wifi_config", DEFAULT_MOCK_DATA["wifi_config"])

def save_wifi_config(config):
    data = load_mock_data()
    data["wifi_config"] = config
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
    return Response(status_code=204)

# Dummy config endpoints for the UI save button (dev only).
@app.get("/api/config")
async def api_config_get():
    try:
        data = load_uart_config()
        payload = {
            "rx_pin": data.get("rx_pin", 16),
            "tx_pin": data.get("tx_pin", 17),
            "baud": data.get("baud", 9600),
            "locked": bool(data.get("locked", False)),
            "defaults": {
                "rx_pin": 16,
                "tx_pin": 17,
                "baud": 9600,
            },
        }
        return JSONResponse(content=payload)
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": "failed to read mock uart_config", "detail": str(e)})

@app.post("/api/config")
async def api_config_post(req: Request):
    try:
        data = await req.json()
    except Exception:
        return JSONResponse(status_code=400, content={"ok": False, "error": "Invalid JSON"})

    if not isinstance(data.get("rx_pin"), int) or not isinstance(data.get("tx_pin"), int) or not isinstance(data.get("baud"), int):
        return JSONResponse(status_code=400, content={"ok": False, "error": "rx_pin, tx_pin, and baud are required"})
    rx = int(data.get("rx_pin"))
    tx = int(data.get("tx_pin"))
    baud = int(data.get("baud"))
    if baud < 0:
        return JSONResponse(status_code=400, content={"ok": False, "error": "rx_pin, tx_pin, and baud are required"})

    config = load_uart_config()
    if bool(config.get("locked", False)):
        return JSONResponse(status_code=403, content={"ok": False, "error": "UART config is locked via build flags"})
    config["rx_pin"] = rx
    config["tx_pin"] = tx
    config["baud"] = baud
    try:
        save_uart_config(config)
    except Exception as e:
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write mock uart_config", "detail": str(e)})

    return JSONResponse(content={"ok": True, "message": "Config saved", "config": {"rx_pin": rx, "tx_pin": tx, "baud": baud}})

# Dummy WiFi config endpoints for the UI save button (dev only).
@app.get("/api/wifi_config")
async def api_wifi_config_get():
    try:
        data = load_wifi_config()
        payload = {
            "loaded": True,
            "ssid": data["ssid"],
            "pass": PASS_MASK,
            "dhcp": data["dhcp"],
            "accesspoint": data.get("accesspoint", True),
            "dual_mode_supported": data.get("dual_mode_supported", True),
            "ip": data["ip"],
            "gw": data["gw"],
            "subnet": data["subnet"],
            "dns": data["dns"],
            "locked": data.get("locked", False),
        }
        return JSONResponse(content=payload)
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": "failed to read mock wifi_config", "detail": str(e)})

@app.post("/api/wifi_config")
async def api_wifi_config_post(req: Request):
    try:
        data = await req.json()
    except Exception:
        return JSONResponse(status_code=400, content={"ok": False, "error": "Invalid JSON"})

    if not isinstance(data.get("ssid"), str) or not isinstance(data.get("pass"), str) or type(data.get("dhcp")) is not bool or type(data.get("accesspoint")) is not bool:
        return JSONResponse(status_code=400, content={"ok": False, "error": "ssid, pass, dhcp, and accesspoint are required"})

    ssid = data["ssid"].strip()
    if not ssid:
        return JSONResponse(status_code=400, content={"ok": False, "error": "ssid is empty"})

    dhcp = data["dhcp"]
    accesspoint = data["accesspoint"]
    wifi_pass = data["pass"]
    ip = str(data.get("ip", "0.0.0.0"))
    gw = str(data.get("gw", "0.0.0.0"))
    subnet = str(data.get("subnet", "0.0.0.0"))
    dns = str(data.get("dns", "0.0.0.0"))

    config = load_wifi_config()
    if bool(config.get("locked", False)):
        return JSONResponse(status_code=403, content={"ok": False, "error": "WiFi config is locked via build flags"})

    if wifi_pass == PASS_MASK:
        wifi_pass = str(config.get("pass", PASS_MASK))

    if not dhcp:
        for field_name, value in (("ip", ip), ("gw", gw), ("subnet", subnet), ("dns", dns)):
            if not value:
                return JSONResponse(status_code=400, content={"ok": False, "error": "ip, gw, subnet, and dns are required when dhcp is false"})
            try:
                ipaddress.ip_address(value)
            except ValueError:
                return JSONResponse(status_code=400, content={"ok": False, "error": "Invalid IP fields"})
    else:
        ip = "0.0.0.0"
        gw = "0.0.0.0"
        subnet = "0.0.0.0"
        dns = "0.0.0.0"

    config["ssid"] = ssid
    config["pass"] = wifi_pass
    config["dhcp"] = dhcp
    config["accesspoint"] = accesspoint
    config["ip"] = ip
    config["gw"] = gw
    config["subnet"] = subnet
    config["dns"] = dns

    try:
        save_wifi_config(config)
    except Exception as e:
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write mock wifi_config", "detail": str(e)})

    return JSONResponse(content={"ok": True, "message": "WiFi config saved", "config": {
        "ssid": config["ssid"],
        "pass": PASS_MASK,
        "dhcp": config["dhcp"],
        "accesspoint": config.get("accesspoint", True),
        "ip": config["ip"],
        "gw": config["gw"],
        "subnet": config["subnet"],
        "dns": config["dns"],
    }})

@app.get("/api/ntrip_config")
async def api_ntrip_config_get():
    try:
        data = load_ntrip_config()
        data["locked"] = bool(data.get("locked", False))
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

    required_checks = [
        (type(ntrip.get("enabled")) is bool),
        isinstance(ntrip.get("host"), str),
        (type(ntrip.get("port")) is int),
        isinstance(ntrip.get("mount"), str),
        isinstance(ntrip.get("user"), str),
        isinstance(ntrip.get("pass"), str),
        (type(ntrip.get("max_tries")) is int),
        (type(ntrip.get("retry_delay_ms")) is int),
        (type(ntrip.get("health_timeout_ms")) is int),
        (type(ntrip.get("passive_sample_ms")) is int),
        (type(ntrip.get("required_valid_frames")) is int),
        (type(ntrip.get("buffer_size")) is int),
        (type(ntrip.get("connect_timeout_ms")) is int),
        (type(ntrip.get("send_gga")) is bool),
    ]
    if not all(required_checks):
        return JSONResponse(status_code=400, content={"ok": False, "error": "Invalid or missing NTRIP fields"})

    if not ntrip["host"]:
        return JSONResponse(status_code=400, content={"ok": False, "error": "host is required"})
    if not ntrip["mount"]:
        return JSONResponse(status_code=400, content={"ok": False, "error": "mount is required"})
    if ntrip["port"] <= 0 or ntrip["port"] > 65535:
        return JSONResponse(status_code=400, content={"ok": False, "error": "port must be non-zero"})
    if ntrip["max_tries"] < 1:
        return JSONResponse(status_code=400, content={"ok": False, "error": "max_tries must be >= 1"})
    if ntrip["buffer_size"] == 0:
        return JSONResponse(status_code=400, content={"ok": False, "error": "buffer_size must be non-zero"})
    if ntrip["connect_timeout_ms"] == 0:
        return JSONResponse(status_code=400, content={"ok": False, "error": "connect_timeout_ms must be non-zero"})

    try:
        current = load_ntrip_config()
        if bool(current.get("locked", False)):
            return JSONResponse(status_code=403, content={"ok": False, "error": "NTRIP config is locked"})
        current["ntrip"] = ntrip
        save_ntrip_config(current)
    except Exception as e:
        return JSONResponse(status_code=500, content={"ok": False, "error": "failed to write mock ntrip_config", "detail": str(e)})

    return JSONResponse(content={"ok": True, "message": "NTRIP config saved", "config": ntrip})

# Serve static files (index.html, app.js, style.css, etc.)
app.mount("/", StaticFiles(directory=str(WEB_DIR), html=True), name="static")

if __name__ == "__main__":
    uvicorn.run("render_web:app", host="127.0.0.1", port=8000, log_level="info", reload=True)
