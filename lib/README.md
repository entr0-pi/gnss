# minnmea

minnmea is a small C library for parsing NMEA 0183 sentences from GNSS receivers.  
It provides simple APIs to decode common NMEA messages into C structs for easy access to time, position, fix status, satellites and speed/course data.

## Table of contents
- [Supported sentences](#supported-sentences)
- [Common parsed fields](#common-parsed-fields)
- [Usage notes](#usage-notes)
- [Example](#example)
- [Reference](#reference)

## Supported sentences

### GGA — Global Positioning System Fix Data
- Fields: UTC time, latitude, longitude, fix quality, number of satellites, HDOP, altitude (with units).

### RMC — Recommended Minimum Specific GNSS Data
- Fields: UTC time, date, latitude, longitude, speed over ground (knots), course, fix status.

### GSA — GNSS DOP and Active Satellites
- Fields: Mode, fix type (2D/3D), PRNs of satellites used, PDOP / HDOP / VDOP.

### GSV — GNSS Satellites in View
- Fields: Number of messages, satellites in view; per-satellite PRN / elevation / azimuth / SNR.

### VTG — Course Over Ground and Ground Speed
- Fields: Course (true/magnetic), speed (knots / km/h).

### GLL — Geographic Position — Latitude/Longitude
- Fields: Latitude / Longitude and UTC time (optional; read-only position/time).

## Common parsed fields
- Timestamp (UTC)
- Latitude / Longitude (with hemisphere)
- Fix quality / status and number of satellites
- Horizontal dilution of precision (HDOP)
- Altitude
- Speed (knots and converted units) and course
- Satellite PRNs, elevation, azimuth, SNR

## Usage notes
- minnmea focuses on parsing; it does not handle serial I/O — feed it complete NMEA sentences.
- Always validate sentence checksum before parsing.
- Receivers may output different sentence combinations; handle missing sentences gracefully.

## Example
```c
// Feed complete NMEA sentence strings to the parser and read structs.
// ...existing code...
```

## Reference
- Project: https://github.com/kosma/minnmea

## Integration highlights in this project
- Parsed satellite azimuth/elevation values are consumed by the Web UI skyplot, which now uses a stereographic projection for clearer horizon distribution.
- Parsed fix/satellite quality data is also used alongside NTRIP status in the dashboard to monitor correction-assisted operation.
