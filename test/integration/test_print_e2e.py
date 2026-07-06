#!/usr/bin/env python3
"""
End-to-end print lifecycle test.

Prerequisites:
  1. mock_prusalink.py running on port 8080
  2. Scanner configured with PrusaLink URL = http://<your-ip>:8080
  3. Scanner has an NFC tag on the reader with known remaining weight

This script drives the mock through a complete print cycle and verifies
the scanner deducts filament from the NFC tag.

Usage:
    python3 test_print_e2e.py --mock-host localhost --mock-port 8080
"""

import argparse
import json
import time
import sys
import requests


def set_mock_state(base_url: str, state: dict):
    r = requests.post(f"{base_url}/mock/state", json=state)
    r.raise_for_status()
    print(f"  Mock → {state.get('state', '?')}")


def get_mock_state(base_url: str) -> dict:
    r = requests.get(f"{base_url}/mock/state")
    r.raise_for_status()
    return r.json()


def wait(seconds: int, reason: str):
    print(f"  Waiting {seconds}s — {reason}")
    for i in range(seconds):
        time.sleep(1)
        print(f"    {i+1}/{seconds}s", end="\r")
    print()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mock-host", default="localhost")
    parser.add_argument("--mock-port", type=int, default=8080)
    parser.add_argument("--scanner-host", default=None,
                        help="Scanner IP for API checks (optional)")
    args = parser.parse_args()

    mock_url = f"http://{args.mock_host}:{args.mock_port}"

    print("=" * 60)
    print("SpoolSense PrusaLink Integration Test: Print E2E")
    print("=" * 60)

    # --- Phase 1: Verify mock is running ---
    print("\n[1/6] Verifying mock server...")
    try:
        state = get_mock_state(mock_url)
        print(f"  Mock state: {state['printer_state']}")
    except Exception as e:
        print(f"  FAIL: Cannot reach mock at {mock_url}: {e}")
        sys.exit(1)

    # --- Phase 2: Reset to idle ---
    print("\n[2/6] Resetting mock to idle...")
    requests.post(f"{mock_url}/mock/reset")

    if args.scanner_host:
        print(f"  Checking scanner at {args.scanner_host}...")
        try:
            r = requests.get(f"http://{args.scanner_host}/api/status", timeout=5)
            status = r.json()
            print(f"  Scanner status: tag={status.get('tag_present', '?')}")
        except Exception as e:
            print(f"  Warning: Cannot reach scanner: {e}")

    # --- Phase 3: Start a print ---
    print("\n[3/6] Starting print (job_id=42, filament=9.18g PLA)...")
    set_mock_state(mock_url, {
        "state": "printing",
        "job_id": 42,
        "filament_g": 9.18,
        "filament_type": "PLA",
        "nozzle_temp": 215,
        "bed_temp": 60,
    })

    wait(15, "Scanner should detect PRINTING state and send PRINT_STARTED")

    # --- Phase 4: Progress updates ---
    print("\n[4/6] Simulating print progress...")
    for pct in [25, 50, 75, 100]:
        set_mock_state(mock_url, {"state": "progress", "progress": float(pct)})
        wait(3, f"Progress → {pct}%")

    # --- Phase 5: Print finishes ---
    print("\n[5/6] Print finishing...")
    set_mock_state(mock_url, {"state": "finished"})

    wait(15, "Scanner should detect FINISHED and deduct 9.18g from tag")

    # --- Phase 6: Verify ---
    print("\n[6/6] Verification...")

    if args.scanner_host:
        try:
            r = requests.get(f"http://{args.scanner_host}/api/status", timeout=5)
            status = r.json()
            remaining = status.get("grams_remaining")
            if remaining is not None:
                print(f"  Tag remaining: {remaining}g")
                print("  (Check serial output for PRINT_ENDED + REMOVE_WEIGHT log)")
            else:
                print("  Could not read remaining weight from scanner API")
        except Exception as e:
            print(f"  Could not verify via scanner API: {e}")
    else:
        print("  No --scanner-host provided. Check scanner serial output for:")
        print("    PrinterManager: Tracking job 42 (filament: 9.18g)")
        print("    PrinterManager: Job 42 finished — 9.18g used")
        print("    EVENT: PrintEnded - job_id=42, filament=9.18g, canceled=false")

    # Cleanup
    requests.post(f"{mock_url}/mock/reset")

    print("\n" + "=" * 60)
    print("Test complete. Verify deduction in scanner serial output.")
    print("=" * 60)


if __name__ == "__main__":
    main()
