# C5/C6 shared-SPI TFT + NFC bench checklist

Status: C6 firmware compiles with the shared-SPI capability enabled, but every
hardware item below is unverified. C5 TFT is intentionally disabled until the
pinned LovyanGFX 1.2.21 supports ESP32-C5; run the same checklist before later
enabling `BOARD_SHARED_SPI` there.

## Wiring and instruments

Use a write-only SPI TFT. Leave TFT MISO disconnected (or prove it is
high-impedance while CS is HIGH). The shared bus reads over the **NFC MISO**
line (C6 GPIO2) — LovyanGFX brings the bus up with that pin as its MISO even
though the TFT never reads, so the NFC MISO must be wired for tag reads to work.
Install external pull-ups, typically 10 kOhm to 3.3 V, on both NFC CS and TFT
CS; firmware-driven HIGH is not a substitute during reset.

| Signal | C6 GPIO | C5 GPIO (deferred) |
|---|---:|---:|
| Shared SCK | 6 | 6 |
| Shared MOSI | 7 | 8 |
| NFC MISO | 2 | 9 |
| NFC CS (NSS/SS) | 10 | 10 |
| PN5180 BUSY | 1 | 1 |
| PN5180 RST / PN532 RST | 0 | 0 |
| TFT CS | 3 | 4 |
| TFT DC | 11 | 5 |
| TFT RST | 18 | 12 |
| TFT MISO / backlight | disconnected / tied as required | disconnected / tied as required |
| PN5180 IRQ / GPIO / AUX | not connected | not connected |

Capture serial logs at 115200 and, where available, use a logic analyzer on
SCK/MOSI/MISO and both CS lines. Record `/api/diagnostics` before and after each
long run, including free heap, minimum-ever free heap, largest free block,
task stack high-water marks, and reset reason.

## Gate checklist

1. **Cold boot and fail-safe selection**

   - Power-cycle at least 25 times, including five rapid off/on cycles, with
     both CS pull-ups fitted.
   - Confirm both CS lines remain HIGH through reset and are never LOW
     together. Confirm TFT and the selected reader initialize on every boot.
   - Repeat the entire item once with PN5180 selected and once with PN532.
   - Pass: 50/50 boots, no spurious commands, corrupt first frame, or reader
     initialization failure.

2. **Full-frame TFT plus PN5180 data integrity**

   - Continuously alternate dashboard/status screens so a 240x240 `pushSprite`
     occurs during scanning.
   - For every supported PN5180 tag format, repeat detect, full read, write,
     readback verification, remove, and re-present for at least 25 cycles.
     Include writable ISO14443A formats and supported ISO15693 formats; include
     read-only formats in detect/read coverage.
   - Pass: zero corrupt TFT frames, UID/payload corruption, failed verified
     writes, SPI errors, deadlocks, or watchdog resets.

3. **PN532 path parity**

   - Select PN532, cold boot, and repeat item 2 for every ISO14443A format the
     PN532 path supports, including OpenPrintTag HAL reads/writes and MIFARE
     Classic reads where applicable.
   - Pass: the same zero-error criteria as item 2.

4. **Wedged PN5180 BUSY isolation**

   - With PN5180 selected, force BUSY HIGH before a command and request TFT
     updates concurrently. Repeat for a failure during send/receive if the
     fixture can control BUSY safely.
   - Confirm the NFC command exits through its one-second bounded wait, marks
     the reader wedged, releases the recursive guard, raises both CS lines, and
     lets later TFT traffic proceed. A TFT caller may wait up to the 3.5-second
     guard bound or drop one frame; it must not wait indefinitely.
   - Pass: bounded failure, no CS overlap, display corruption, deadlock, task
     watchdog reset, or loss of watchdog diagnostics.

5. **Heavy-display scan latency**

   - Run maximum-rate full-frame changes while presenting/removing tags at a
     fixed cadence. Capture detect and complete-read latency for at least 500
     presentations, then repeat with the TFT disabled as the baseline.
   - Proposed budget pending bench measurement: p95 detect latency no more than
     150 ms above baseline and p99 complete-read latency no more than 500 ms
     above baseline, with zero corrupted tags or writes. Record actual values;
     revise the budget only with an explicit product decision.

6. **No-PSRAM memory review**

   - Confirm the C6 creates the expected 8-bit 240x240 sprite (about 57.6 kB)
     and does not log sprite allocation failure. Exercise OTA `freeForOTA()`
     and its direct progress/error drawing while NFC is active.
   - Compare diagnostics before TFT init, after init, after one hour, and after
     OTA sprite release. Pass: stable heap/largest-block floors, no allocation
     failure, and no task stack high-water mark below the project safety floor.
   - Repeat on C5 only after TFT capability is enabled there.

7. **Duration soaks**

   - Run two hours of active animation plus alternating reads and verified
     writes with PN5180, then two hours with PN532. Follow with an overnight run
     that continues animation/scanning and performs periodic verified writes.
   - Repeat the complete sequence on C5 after its TFT compile blocker is
     resolved.
   - Pass: zero SPI errors, watchdog resets, corrupt frames, corrupt tag data,
     or failed verified writes. Archive serial logs and start/end diagnostics.

Do not mark C6 shared TFT validated—or enable C5 shared TFT—until all applicable
items pass on physical hardware. A failure keeps TFT deferred without blocking
the NFC-capable Arduino-3.x target.
