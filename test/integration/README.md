# Integration bench tests

Hardware-in-the-loop scenarios that drive a **real scanner** against a mock
PrusaLink server. They verify the print-lifecycle behavior end to end: job
polling, filament deduction on finish, cancel proration, filament-type
mismatch warnings, and XL multi-tool handling.

**These cannot run in CI** — every scenario needs a physical scanner on the
network with an NFC tag on the reader (tracked in #225; the suite was
originally slated for CI before that constraint was recognized).

## Prerequisites

1. A flashed scanner on the same LAN as this machine.
2. A writable NFC tag (OpenPrintTag or OpenTag3D) on the reader with a known
   remaining weight — deduction scenarios write to it.
3. Scanner config page → PrusaLink URL = `http://<this-machine-ip>:8080`
   (or the port you pass with `-p`), PrusaLink enabled.
4. `python3` with `requests` (`pip3 install requests`).

## Running

```bash
./run_bench.sh                # all scenarios, mock on :8080
./run_bench.sh -p 9090 e2e    # one scenario on a custom port
```

The runner starts `mock_prusalink.py`, waits for it to come up, executes the
selected scenarios, and tears the mock down. Each scenario prints PASS/FAIL;
the runner exits non-zero on any failure.

| Scenario   | Script                      | What it proves |
|------------|-----------------------------|----------------|
| `e2e`      | `test_print_e2e.py`         | Full print cycle deducts the right grams from the tag |
| `canceled` | `test_print_canceled.py`    | Canceled prints deduct prorated usage, not the full job |
| `mismatch` | `test_filament_mismatch.py` | Loaded-filament mismatch is detected and surfaced |
| `xl`       | `test_xl_multitool.py`      | Multi-tool jobs attribute usage to the correct tool |

## Driving the mock manually

```bash
python3 mock_prusalink.py --port 8080
curl -X POST http://localhost:8080/mock/state -d '{"state":"printing","job_id":42,"filament_g":9.18}'
curl -X POST http://localhost:8080/mock/state -d '{"state":"finished"}'
```
