#!/usr/bin/env python3
"""
XL multi-tool print test.

Simulates a 3-tool XL print with per-tool filament data.
Verifies the scanner logs per-tool usage and publishes to HA.

Usage:
    python3 test_xl_multitool.py --mock-host localhost --mock-port 8080
"""

import argparse
import time
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
    print("SpoolSense PrusaLink Test: XL Multi-Tool Print")
    print("=" * 60)

    requests.post(f"{mock_url}/mock/reset")

    # Start 3-tool print with per-tool filament data
    print("\n[1/3] Starting 3-tool print...")
    set_mock_state(mock_url, {
        "state": "printing",
        "job_id": 200,
        "filament_g": 45.0,  # total
        "filament_type": "PLA",
        "nozzle_temp": 215,
        "bed_temp": 60,
        "filament_per_tool": [15.0, 20.0, 10.0],
        "filament_type_per_tool": ["PLA", "PETG", "PLA"],
    })
    wait(15, "Scanner detects multi-tool print")

    print("  Expected serial output:")
    print("    PrusaLink: Multi-tool job detected (3 tools)")
    print("      Tool 0: 15.00g PLA")
    print("      Tool 1: 20.00g PETG")
    print("      Tool 2: 10.00g PLA")

    # Finish print
    print("\n[2/3] Finishing print...")
    set_mock_state(mock_url, {"state": "progress", "progress": 100.0})
    wait(5, "Progress to 100%")
    set_mock_state(mock_url, {"state": "finished"})
    wait(15, "Scanner deducts total 45g")

    print("\n[3/3] Verification")
    print("  Expected serial output:")
    print("    EVENT: PrintEnded - job_id=200, filament=45.00g, canceled=false, tools=3")
    print("      Tool 0: 15.00g")
    print("      Tool 1: 20.00g")
    print("      Tool 2: 10.00g")
    print("")
    print("  Expected HA MQTT payload (printer/state):")
    print('    {"state":"idle","last_job_id":200,"filament_used_g":45.0,')
    print('     "tool_count":3,"per_tool":[15.0,20.0,10.0]}')

    requests.post(f"{mock_url}/mock/reset")
    print("\nDone.")


if __name__ == "__main__":
    main()
