## State Flow (Stateless Config Modules)
```mermaid
flowchart LR
  subgraph Browser[WebUI Browser]
    JS[app.js]
    UI[HTML/CSS]
    JS --> UI
    UI --> JS
  end

  subgraph Server[Embedded HTTP Server]
    WEB[web_ui.cpp JSON API boundary]
    WIFI[wifi_config stateless load/save]
    GNSS[gnss_config stateless load/save]
    NTRIP[ntrip_config stateless load/save]
    WEB --> WIFI
    WEB --> GNSS
    WEB --> NTRIP
  end

  subgraph Storage[NVS]
    NVS["(NVS namespaces and keys)"]
  end

  WIFI <--> NVS
  GNSS <--> NVS
  NTRIP <--> NVS

  JS -->|GET /api/status poll| WEB
  JS -->|GET /api/*_config| WEB
  JS -->|POST /api/*_config| WEB
  WEB -->|JSON responses| JS
```

```mermaid
sequenceDiagram
  autonumber
  participant JS as app.js
  participant WEB as web_ui.cpp
  participant WIFI as wifi_config
  participant GNSS as gnss_config
  participant NTRIP as ntrip_config
  participant NVS as NVS

  loop Every POLL_INTERVAL
    JS->>WEB: GET /api/status
    WEB-->>JS: 200 JSON(status/telemetry)
  end

  JS->>WEB: POST /api/wifi_config {json}
  WEB->>WIFI: wifi_config_save(struct)
  WIFI->>NVS: write keys
  NVS-->>WIFI: ok
  WIFI-->>WEB: ok
  WEB-->>JS: 200 {ok:true}

  JS->>WEB: GET /api/wifi_config
  WEB->>WIFI: wifi_config_load(out)
  WIFI->>NVS: read keys
  NVS-->>WIFI: values
  WIFI-->>WEB: out
  WEB-->>JS: 200 JSON

  JS->>WEB: POST /api/config {json}
  WEB->>GNSS: gnss_config_save(struct)
  GNSS->>NVS: write keys
  NVS-->>GNSS: ok
  GNSS-->>WEB: ok
  WEB-->>JS: 200 {ok:true}

  JS->>WEB: POST /api/ntrip_config {json}
  WEB->>NTRIP: ntrip_config_save(struct)
  NTRIP->>NVS: write keys
  NVS-->>NTRIP: ok
  NTRIP-->>WEB: ok
  WEB-->>JS: 200 {ok:true}
```

---

## main.cpp — Data Flow Architecture

```mermaid
flowchart LR
  GNSS["GNSS Module\n(Serial1)"]
  PHONE["BLE Phone"]
  TCPC["TCP Client"]
  CASTER["NTRIP Caster"]

  URX["task_uart_rx\npri 3"]
  UTX["task_uart_tx\npri 3"]
  BTX["task_ble_tx\npri 2"]
  TIO["task_tcp_io\npri 2"]
  NMEA["nmea_gps\nparser"]

  U2B(("uart→ble"))
  B2U(("ble→uart"))
  U2T(("uart→tcp"))
  T2U(("tcp→uart"))
  N2U(("ntrip→uart"))

  GNSS -->|RX| URX
  URX -->|feed| NMEA
  URX --> U2B --> BTX -->|notify| PHONE
  URX --> U2T --> TIO -->|send| TCPC
  PHONE -->|write| B2U --> UTX
  TCPC -->|recv| T2U --> UTX
  CASTER -->|RTCM| N2U --> UTX
  UTX -->|TX| GNSS
```

```mermaid
flowchart TD
  subgraph setup["setup()"]
    S1["Serial.begin()\ngnss_config_begin()"]
    S2["Create StreamBuffers\nuart↔ble, uart↔tcp, ntrip→uart"]
    S3["Setup UART (Serial1)\nif pins/baud configured"]
    S4["Setup BLE (NUS service)\nstart advertising"]
    S5["Register WebUI routes\nmount LittleFS"]
    S6["Connect WiFi (STA)\nfrom NVS or compile-time"]
    S7["Start HTTP server :80"]
    S8["Init NMEA parser"]
    S9["ntrip_client_setup()\nspawns NtripMonitor task"]
    S10["Create worker tasks\nuart_rx, uart_tx, ble_tx, tcp_io"]

    S1 --> S2 --> S3 --> S4 --> S5 --> S6 --> S7 --> S8 --> S9 --> S10
  end

  subgraph loop["loop() — every 2 ms"]
    L1["maybeReconnectWiFi\n(throttled 5 s)"]
    L2["server.handleClient()"]
    L3["ntrip_client_loop()\n(status logging)"]
    L4["delay(2)"]

    L1 --> L2 --> L3 --> L4
  end

  setup --> loop
```

---

## web_ui.cpp — HTTP JSON API Boundary

```mermaid
flowchart TD
  subgraph static["Static Assets (LittleFS)"]
    R1["GET /\nindex.html.gz"]
    R2["GET /style.css\nstyle.css.gz"]
    R3["GET /app.js\napp.js.gz"]
    R4["GET /favicon.ico\nfavicon.ico.gz"]
  end

  subgraph api["JSON API Endpoints"]
    STATUS["GET /api/status"]
    CFG_G["GET /api/config"]
    CFG_P["POST /api/config"]
    WIFI_G["GET /api/wifi_config"]
    WIFI_P["POST /api/wifi_config"]
    NTRIP_G["GET /api/ntrip_config"]
    NTRIP_P["POST /api/ntrip_config"]
    RESTART["POST /api/restart"]
  end

  subgraph snapshots["Snapshot Providers\n(defined in main.cpp)"]
    BLE_S["webui_get_ble_snapshot"]
    GPS_S["webui_get_gps_snapshot"]
    TCP_S["webui_get_tcp_snapshot"]
    NTRIP_S["webui_get_ntrip_snapshot"]
  end

  subgraph config["Config Modules"]
    GNSS_C["gnss_config\nload / save"]
    WIFI_C["wifi_config\nload / save"]
    NTRIP_C["ntrip_config\nload / save"]
  end

  INET["Internet probe\n(HTTP 204 check, cached 10 s)"]

  STATUS --> BLE_S & GPS_S & TCP_S & NTRIP_S & INET
  CFG_G & CFG_P --> GNSS_C
  WIFI_G & WIFI_P --> WIFI_C
  NTRIP_G & NTRIP_P --> NTRIP_C
```

---

## ntrip_client.cpp — NTRIP Connection Manager

```mermaid
flowchart TD
  START(["configMonitorTask\n(FreeRTOS, core 1, 1 s loop)"])

  CHECK_NET{"Internet\nreachable?"}
  NET_LOST["Stop NtripClient"]
  LOAD["ntrip_config_load()\n→ NtripConfig + NtripLockout"]
  ENABLED{"enabled &\nnot locked out?"}
  HASH{"Config hash\nchanged?"}
  BEGIN["NtripClient.begin()\n+ startTask(core 0)"]
  STOP["NtripClient.stop()"]
  SYNC["syncLockoutWithClientState\n(clear on STREAMING, set on LOCKED_OUT)"]
  LOCKOUT{"State ==\nLOCKED_OUT?"}
  HANDLE_LO["handleLockout\nauto-reset after 2 min"]
  STATS["displayDetailedStats\nevery 30 s"]
  SLEEP(["vTaskDelay 1 s"])

  START --> CHECK_NET
  CHECK_NET -->|lost| NET_LOST --> SLEEP
  CHECK_NET -->|ok, every 5 s| LOAD
  LOAD --> ENABLED
  ENABLED -->|no / disabled| STOP --> SLEEP
  ENABLED -->|yes| HASH
  HASH -->|changed or first run| BEGIN --> SYNC
  HASH -->|same| SYNC
  SYNC --> LOCKOUT
  LOCKOUT -->|yes| HANDLE_LO --> SLEEP
  LOCKOUT -->|no| STATS --> SLEEP
  SLEEP -->|loop| CHECK_NET
```

```mermaid
flowchart LR
  subgraph data["RTCM Data Path"]
    CASTER["NTRIP Caster"]
    LIB["NtripClient library\n(internal task, core 0)"]
    WRITER["NtripStreamWriter\n(Print adapter)"]
    SB(("ntrip→uart\nStreamBuffer"))
  end

  CASTER -->|TCP| LIB -->|RTCM frames| WRITER -->|xStreamBufferSend| SB
```

---

## nmea_gps.cpp — NMEA Sentence Parser

```mermaid
flowchart LR
  UART["Raw UART bytes\n(from task_uart_rx)"]

  subgraph collector["Line Collector"]
    DOLLAR["Wait for $"]
    ACCUM["Accumulate chars"]
    NEWLINE["\\n → null-terminate"]
  end

  CHECK["minmea_check()\nverify checksum"]

  subgraph dispatch["Sentence Dispatch"]
    RMC["RMC\nlat, lon, speed\ntime, date, validity"]
    GGA["GGA\nfix quality, sats\nHDOP, time"]
    GSA["GSA\nfix type\nPDOP/HDOP/VDOP"]
    GST["GST\nσ_lat, σ_lon, σ_alt\n→ accuracy (m)"]
    GSV["GSV\nper-sat PRN, el, az, SNR\n(multi-msg staged)"]
  end

  STATE["GpsState\n(portMUX protected)"]
  SNAP["nmea_get_snapshot()\n→ WebuiGpsSnapshot"]

  UART -->|nmea_feed_bytes| collector
  collector --> CHECK --> dispatch
  RMC & GGA & GSA & GST --> STATE
  GSV -->|"stage per-constellation\nthen commit"| STATE
  STATE --> SNAP
```

---

## wifi_config.cpp — WiFi Config Module

```mermaid
flowchart LR
  subgraph api["wifi_config API"]
    DEF["defaults()\n→ empty WifiConfig"]
    VAL["validate(cfg)\nssid required, static IP if !dhcp"]
    LOAD["load(out, err)\n→ WifiConfig"]
    SAVE["save(cfg, err)"]
  end

  NVS["NVS (wifi namespace)\nssid, pass, dhcp\nip, gw, subnet, dns"]

  LOAD -->|read| NVS
  SAVE -->|write| NVS
```

---

## gnss_config.cpp — GNSS Config Module

```mermaid
flowchart LR
  subgraph api["gnss_config API"]
    DEF["defaults()\n→ PIN_GNSS_RX/TX, GNSS_BAUD"]
    VAL["validate(cfg)\npins, baud range"]
    LOAD["load(out, err)\n→ GnssConfig"]
    SAVE["save(cfg, err)"]
    BEGIN["begin()\ninit NVS, write defaults if missing"]
  end

  NVS["NVS (gnss namespace)\nrx_pin, tx_pin, baud"]

  BEGIN -->|init| NVS
  LOAD -->|read| NVS
  SAVE -->|write| NVS
```

---

## ntrip_config.cpp — NTRIP Config Module

```mermaid
flowchart LR
  subgraph api["ntrip_config API"]
    DEF["defaults()\n→ disabled, port 2101, 5 retries"]
    VAL["validate(cfg)\nhost, mount, port required"]
    LOAD["load(out, lockout, err)\n→ NtripConfig + NtripLockout"]
    SAVE["save(cfg, lockout, err)"]
  end

  NVS["NVS (ntrip namespace)\n13 config keys\n+ 3 lockout keys"]

  LOAD -->|read| NVS
  SAVE -->|write| NVS
```

---

## logger.cpp — Serial Logger

```mermaid
flowchart LR
  MACRO["LOG_I / LOG_W / LOG_E / LOG_D\n(preprocessor macros)"]
  FN["logToSerial(color, tag, file, line, fmt, ...)"]
  USB["Serial (USB CDC)"]

  MACRO --> FN -->|"[timestamp][TAG] message (file:line)"| USB
```
