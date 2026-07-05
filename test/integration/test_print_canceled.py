#!/usr/bin/env python3
"""
Print cancellation test — verifies partial filament deduction.

Usage:
    python3 test_print_canceled.py --mock-host localhost --mock-port 8080
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


def wait(seconds: int, reason: str):
    print(f"  Waiting {seconds}s — {reason}")
    time.sleep(seconds)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mock-host", default="localhost")
    parser.add_argument("--mock-port", type=int, default=8080)
    args = parser.parse_args()

    mock_url = f"http://{args.mock_host}:{args.mock_port}"

    print("=" * 60)
    print("SpoolSense PrusaLink Test: Print Canceled at 30%")
    print("=" * 60)

    # Reset
    requests.post(f"{mock_url}/mock/reset")

    # Start print
    print("\n[1/4] Starting print (job_id=99, filament=20.0g)...")
    set_mock_state(mock_url, {
        "state": "printing",
        "job_id": 99,
        "filament_g": 20.0,
        "filament_type": "PETG",
        "nozzle_temp": 240,
        "bed_temp": 85,
    })
    wait(15, "Scanner detects print start")

    # Progress to 30%
    print("\n[2/4] Progress to 30%...")
    set_mock_state(mock_url, {"state": "progress", "progress": 30.0})
    wait(12, "Scanner updates progress")

    # Stop
    print("\n[3/4] Canceling print...")
    set_mock_state(mock_url, {"state": "stopped"})
    wait(15, "Scanner should deduct 30% of 20g = 6.0g")

    # Verify
    print("\n[4/4] Verification")
    print("  Expected: ~6.0g deducted (30% of 20g)")
    print("  Check serial output for:")
    print("    PrinterManager: Job 99 canceled — 6.00g used")

    requests.post(f"{mock_url}/mock/reset")
    print("\nDone.")


if __name__ == "__main__":
    main()
