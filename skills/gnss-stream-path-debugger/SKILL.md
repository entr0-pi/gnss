---
name: gnss-stream-path-debugger
description: Troubleshoot GNSS data flow across UART, BLE, TCP, and NTRIP correction paths with a deterministic triage sequence.
---

# GNSS Stream Path Debugger

Use this skill when data is missing, delayed, or one transport path fails.

## Workflow

1. Determine failing path:
   - UART->BLE
   - UART->TCP
   - BLE/TCP->UART
   - NTRIP->UART
2. Confirm compile-time prerequisites (feature flags enabled).
3. Validate source signal first (UART activity / GNSS output).
4. Validate one hop at a time and isolate first broken edge.
5. Recommend minimal fix and retest order.

## Triage Output Format

- Symptom
- Suspected layer
- Checks performed
- First failing edge
- Fix applied / next step
