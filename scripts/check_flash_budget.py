#!/usr/bin/env python3
"""Fail CI when a firmware image nears the OTA app-partition limit.

Usage: check_flash_budget.py <env> [max_percent]

Reads the app0 partition size from partitions.csv and compares it against
.pio/build/<env>/firmware.bin. An image over the threshold (default 97%)
fails the build — better a red PR than a target that can no longer fit an
OTA update. ESP32-C5 is the tightest board (~95% as of 1.9.0).
"""
import csv
import os
import sys


def partitions_csv_for(env):
    """The env's board_build.partitions override, else the default table."""
    import configparser
    cp = configparser.ConfigParser()
    cp.read("platformio.ini")
    sect = "env:%s" % env
    if cp.has_option(sect, "board_build.partitions"):
        return cp.get(sect, "board_build.partitions").strip()
    return "partitions.csv"


def app_partition_size(csv_path="partitions.csv"):
    with open(csv_path) as f:
        for row in csv.reader(f):
            if len(row) >= 5 and row[0].strip() == "app0":
                return int(row[4].strip(), 0)
    raise SystemExit("check_flash_budget: app0 not found in partitions.csv")


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: check_flash_budget.py <env> [max_percent]")
    env = sys.argv[1]
    max_pct = float(sys.argv[2]) if len(sys.argv) > 2 else 97.0

    binary = os.path.join(".pio", "build", env, "firmware.bin")
    if not os.path.isfile(binary):
        raise SystemExit("check_flash_budget: %s not found (build first)" % binary)

    used = os.path.getsize(binary)
    limit = app_partition_size(partitions_csv_for(env))
    pct = used * 100.0 / limit
    print("check_flash_budget: %s firmware.bin %d / %d bytes (%.1f%%, max %.1f%%)"
          % (env, used, limit, pct, max_pct))
    if pct > max_pct:
        raise SystemExit(
            "check_flash_budget: %s exceeds the flash budget — trim the image "
            "or consciously raise the threshold in ci.yml" % env)


if __name__ == "__main__":
    main()
