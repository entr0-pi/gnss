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
  GNSS["GNSS Module<br>(Serial1)"]
  PHONE["BLE Phone"]
  TCPC["TCP Client"]
  CASTER["NTRIP Caster"]

  URX["task_uart_rx<br>pri 3"]
  UTX["task_uart_tx<br>pri 3"]
  BTX["task_ble_tx<br>pri 2"]
  TIO["task_tcp_io<br>pri 2"]
  NMEA["nmea_gps<br>parser"]

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
    S1["Serial.begin()<br>gnss_config_begin()"]
    S2["Create StreamBuffers<br>uart↔ble, uart↔tcp, ntrip→uart"]
    S3["Setup UART (Serial1)<br>if pins/baud configured"]
    S4["Setup BLE (NUS service)<br>start advertising"]
    S5["Register WebUI routes<br>mount LittleFS"]
    S6["Connect WiFi (STA)<br>from NVS or compile-time"]
    S7["Start HTTP server :80"]
    S8["Init NMEA parser"]
    S9["ntrip_client_setup()<br>spawns NtripMonitor task"]
    S10["Create worker tasks<br>uart_rx, uart_tx, ble_tx, tcp_io"]

    S1 --> S2 --> S3 --> S4 --> S5 --> S6 --> S7 --> S8 --> S9 --> S10
  end

  subgraph loop["loop() — every 2 ms"]
    L1["maybeReconnectWiFi<br>(throttled 5 s)"]
    L2["server.handleClient()"]
    L3["ntrip_client_loop()<br>(status logging)"]
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
    R1["GET /<br>index.html.gz"]
    R2["GET /style.css<br>style.css.gz"]
    R3["GET /app.js<br>app.js.gz"]
    R4["GET /favicon.ico<br>favicon.ico.gz"]
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

  subgraph snapshots["Snapshot Providers<br>(defined in main.cpp)"]
    BLE_S["webui_get_ble_snapshot"]
    GPS_S["webui_get_gps_snapshot"]
    TCP_S["webui_get_tcp_snapshot"]
    NTRIP_S["webui_get_ntrip_snapshot"]
  end

  subgraph config["Config Modules"]
    GNSS_C["gnss_config<br>load / save"]
    WIFI_C["wifi_config<br>load / save"]
    NTRIP_C["ntrip_config<br>load / save"]
  end

  INET["Internet probe<br>(HTTP 204 check, cached 10 s)"]

  STATUS --> BLE_S & GPS_S & TCP_S & NTRIP_S & INET
  CFG_G & CFG_P --> GNSS_C
  WIFI_G & WIFI_P --> WIFI_C
  NTRIP_G & NTRIP_P --> NTRIP_C
```

---

## ntrip_client.cpp — NTRIP Connection Manager

```mermaid
flowchart TD
  START(["configMonitorTask<br>(FreeRTOS, core 1, 1 s loop)"])

  CHECK_NET{"Internet<br>reachable?"}
  NET_LOST["Stop NtripClient"]
  LOAD["ntrip_config_load()<br>→ NtripConfig + NtripLockout"]
  ENABLED{"enabled &<br>not locked out?"}
  HASH{"Config hash<br>changed?"}
  BEGIN["NtripClient.begin()<br>+ startTask(core 0)"]
  STOP["NtripClient.stop()"]
  SYNC["syncLockoutWithClientState<br>(clear on STREAMING, set on LOCKED_OUT)"]
  LOCKOUT{"State ==<br>LOCKED_OUT?"}
  HANDLE_LO["handleLockout<br>auto-reset after 2 min"]
  STATS["displayDetailedStats<br>every 30 s"]
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
    LIB["NtripClient library<br>(internal task, core 0)"]
    WRITER["NtripStreamWriter<br>(Print adapter)"]
    SB(("ntrip→uart<br>StreamBuffer"))
  end

  CASTER -->|TCP| LIB -->|RTCM frames| WRITER -->|xStreamBufferSend| SB
```

---

## nmea_gps.cpp — NMEA Sentence Parser

```mermaid
flowchart LR
  UART["Raw UART bytes<br>(from task_uart_rx)"]

  subgraph collector["Line Collector"]
    DOLLAR["Wait for $"]
    ACCUM["Accumulate chars"]
    NEWLINE["LF → null-terminate"]
  end

  CHECK["minmea_check()<br>verify checksum"]

  subgraph dispatch["Sentence Dispatch"]
    RMC["RMC<br>lat, lon, speed<br>time, date, validity"]
    GGA["GGA<br>fix quality, sats<br>HDOP, time"]
    GSA["GSA<br>fix type<br>PDOP/HDOP/VDOP"]
    GST["GST<br>σ_lat, σ_lon, σ_alt<br>→ accuracy (m)"]
    GSV["GSV<br>per-sat PRN, el, az, SNR<br>(multi-msg staged)"]
  end

  STATE["GpsState<br>(portMUX protected)"]
  SNAP["nmea_get_snapshot()<br>→ WebuiGpsSnapshot"]

  UART -->|nmea_feed_bytes| collector
  collector --> CHECK --> dispatch
  RMC & GGA & GSA & GST --> STATE
  GSV -->|"stage per-constellation<br>then commit"| STATE
  STATE --> SNAP
```

---

## wifi_config.cpp — WiFi Config Module

```mermaid
flowchart LR
  subgraph api["wifi_config API"]
    DEF["defaults()<br>→ empty WifiConfig"]
    VAL["validate(cfg)<br>ssid required, static IP if !dhcp"]
    LOAD["load(out, err)<br>→ WifiConfig"]
    SAVE["save(cfg, err)"]
  end

  NVS["NVS (wifi namespace)<br>ssid, pass, dhcp<br>ip, gw, subnet, dns"]

  LOAD -->|read| NVS
  SAVE -->|write| NVS
```

---

## gnss_config.cpp — GNSS Config Module

```mermaid
flowchart LR
  subgraph api["gnss_config API"]
    DEF["defaults()<br>→ PIN_GNSS_RX/TX, GNSS_BAUD"]
    VAL["validate(cfg)<br>pins, baud range"]
    LOAD["load(out, err)<br>→ GnssConfig"]
    SAVE["save(cfg, err)"]
    BEGIN["begin()<br>init NVS, write defaults if missing"]
  end

  NVS["NVS (gnss namespace)<br>rx_pin, tx_pin, baud"]

  BEGIN -->|init| NVS
  LOAD -->|read| NVS
  SAVE -->|write| NVS
```

---

## ntrip_config.cpp — NTRIP Config Module

```mermaid
flowchart LR
  subgraph api["ntrip_config API"]
    DEF["defaults()<br>→ disabled, port 2101, 5 retries"]
    VAL["validate(cfg)<br>host, mount, port required"]
    LOAD["load(out, lockout, err)<br>→ NtripConfig + NtripLockout"]
    SAVE["save(cfg, lockout, err)"]
  end

  NVS["NVS (ntrip namespace)<br>13 config keys<br>+ 3 lockout keys"]

  LOAD -->|read| NVS
  SAVE -->|write| NVS
```

---

## logger.cpp — Serial Logger

```mermaid
flowchart LR
  MACRO["LOG_I / LOG_W / LOG_E / LOG_D<br>(preprocessor macros)"]
  FN["logToSerial(color, tag, file, line, fmt, ...)"]
  USB["Serial (USB CDC)"]

  MACRO --> FN -->|"[timestamp][TAG] message (file:line)"| USB
```
