#!/usr/bin/env python3
"""
Filament mismatch warning test.

Starts a print that expects ABS (240C nozzle) while the NFC tag has PLA loaded.
Verifies the scanner logs a FILAMENT MISMATCH warning.

Usage:
    python3 test_filament_mismatch.py --mock-host localhost --mock-port 8080
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
    print("SpoolSense PrusaLink Test: Filament Mismatch Warning")
    print("=" * 60)

    requests.post(f"{mock_url}/mock/reset")

    # Start print expecting ABS at 240C — but tag should have PLA loaded
    print("\n[1/3] Starting print expecting ABS (240C nozzle, 100C bed)...")
    set_mock_state(mock_url, {
        "state": "printing",
        "job_id": 77,
        "filament_g": 15.0,
        "filament_type": "ABS",
        "nozzle_temp": 240,
        "bed_temp": 100,
    })
    wait(15, "Scanner should detect mismatch if tag has PLA")

    print("\n[2/3] Check serial output for:")
    print("  PrinterManager: FILAMENT MISMATCH — gcode expects 'ABS', tag has 'PLA'")
    print("  EVENT: PrinterWarning type=filament_mismatch expected=ABS actual=PLA")
    print("  (LCD should show 'WRONG FILAMENT!' / 'ABS!=PLA WRONG!')")
    print("  (LED should flash yellow 3x)")

    # Also test temp warning — if tag has max_print_temp < 240
    print("\n[3/3] Temperature check:")
    print("  If tag has max_print_temp=220 and gcode needs 240C:")
    print("  PrinterManager: TEMP WARNING — gcode 240C exceeds tag max 220C")

    # Let it finish
    set_mock_state(mock_url, {"state": "finished"})
    wait(5, "Cleanup")
    requests.post(f"{mock_url}/mock/reset")

    print("\nDone.")


if __name__ == "__main__":
    main()
