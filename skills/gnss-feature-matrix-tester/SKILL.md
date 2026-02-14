---
name: gnss-feature-matrix-tester
description: Run and interpret the GNSS build-tester matrix (tiers A/B/C), summarize PASS/XFAIL outcomes, and recommend targeted new tests in flags_to_tests.json.
---

# GNSS Feature Matrix Tester

Use this skill for compile-matrix validation requests, CI hardening, or when changing feature flags and constraints.

## Workflow

1. List tests first:
   - `python utils/build-tester/build_Tester.py --list`
2. Execute requested scope:
   - smoke: `--tier A`
   - confidence: `--tier A,B`
   - constraints: `--tier C`
3. Classify outcomes by labels: `PASS`, `FAIL`, `XFAIL`, `UNEX.PASS`.
4. If failures occur:
   - identify likely flag interaction,
   - propose minimal reproduction command,
   - suggest next matrix additions.
5. Provide concise report:
   - command run
   - totals by label
   - actionable recommendations

## Guardrails

- Keep original `platformio.ini` intact after tests.
- Do not add broad new tests unless they cover a new interaction or regression.
