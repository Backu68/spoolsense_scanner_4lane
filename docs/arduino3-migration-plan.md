# Arduino-ESP32 3.x Migration Plan (Epic #254)

Handoff plan for migrating the firmware from Arduino-ESP32 **2.0.17** (ESP-IDF 4.4)
to Arduino-ESP32 **3.3.x** (ESP-IDF 5.5), unlocking **ESP32-C6** and **ESP32-C5**
targets. Written from a full audit of this codebase, not generic migration notes —
every "affected" claim below was grepped/verified against the tree as of dev @ v1.8.2.

---

## 1. Why, and why it's an epic

- The project pins `platform = espressif32@6.10.0`, which ships
  `framework-arduinoespressif32 @ 3.20017` = **Arduino core 2.0.17**. That core
  predates the C6/C5 chips entirely — they cannot be compiled for, period.
- The pin is deliberate: unpinned upgrades broke the build in the past
  (see the platformio.ini history and the pinning note). Migration means fixing
  those breaks properly and revalidating the entire board matrix on hardware.
- **C6** (WiFi 6, 802.15.4 Thread/Zigbee, single-core RISC-V) needs Arduino core
  **3.0+**. **C5** (dual-band 2.4/5 GHz WiFi 6, single-core RISC-V, 400KB-class
  SRAM like the C3) needs the **3.3.x** line. Memory **appears low-risk**: the C3
  (same SRAM class) runs this firmware at ~19% static RAM with ~190KB free heap —
  but runtime internal heap, largest-block, TLS/OTA behavior, and task
  high-water marks on the new core still need hardware evidence (the
  `/api/diagnostics` + self-test telemetry makes this a one-click check).

## 2. The platform path — critical fact

**The official PlatformIO `espressif32` platform's Arduino framework is frozen
at 2.0.17** (the platform itself still receives ESP-IDF-only updates, but the
Arduino core will never advance there — Espressif ended the PlatformIO
partnership). Arduino 3.x under PlatformIO requires the community
**pioarduino** fork: <https://github.com/pioarduino/platform-espressif32>.

- Current stable as of 2026-07-11: **pioarduino release `55.03.39`** =
  Arduino core **3.3.9** on **ESP-IDF 5.5.4**. (Upstream Arduino 3.3.10 exists
  but is not yet packaged by pioarduino — re-check the releases page at kickoff.)
- A `4.0.0-alpha` core exists — **do not** target it; stay on latest stable 3.3.x.
- Pin the **exact release** (zip-URL form from the pioarduino releases page):

  ```ini
  [env]
  platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
  ```

  Same discipline as today's `espressif32@6.10.0` pin — never a moving branch.
- Toolchain jump: **GCC 8.4 → GCC 14.x**. Expect a wave of new warnings
  (`-Wmaybe-uninitialized`, stricter narrowing). We don't build with `-Werror`,
  so these are cleanup, not blockers. Native tests (host g++) are unaffected.

## 3. Audited breakage inventory

### 3.1 MUST FIX — task watchdog (a direct API break, and a policy decision)

`src/NFCManager.cpp:656` (+ `esp_task_wdt_add/reset` at 657/666/678 and
`HardwareNFCConnection.cpp:14`). In 3.x, `esp_task_wdt_init` takes a config
struct **and** the Arduino core has already initialized the TWDT, so calling
init again fails with `ESP_ERR_INVALID_STATE` — use **`esp_task_wdt_reconfigure`**
(which requires an already-initialized TWDT; both facts per the
[IDF watchdog docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html)).

Two things the port must do deliberately — not assume:

1. **Choose the idle-task policy explicitly.** `reconfigure` replaces the
   *system-wide* TWDT config shared with Arduino/IDF tasks, including
   `idle_core_mask`. Do NOT claim any value "matches current behavior" without
   proof — at kickoff, check the pinned release's sdkconfig
   (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPUx`) and set `idle_core_mask` to
   preserve the core's choice (Arduino's default leaves idle tasks unwatched so
   a blocking `loop()` doesn't trip it — verify, then document the choice in
   the code comment).
2. **Check the return values** of both `esp_task_wdt_reconfigure` and
   `esp_task_wdt_add` (log loudly on failure — a silently unarmed watchdog is
   worse than none).

```cpp
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = NFC_WDT_TIMEOUT_S * 1000,
        .idle_core_mask = /* match core sdkconfig — decide at kickoff, see above */ 0,
        .trigger_panic  = true,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&wdtCfg);
    if (err != ESP_OK) Serial.printf("NFCManager: TWDT reconfigure failed: %d\n", err);
#else
    esp_task_wdt_init(NFC_WDT_TIMEOUT_S, true);
#endif
    if (esp_task_wdt_add(NULL) != ESP_OK) Serial.println("NFCManager: TWDT add failed");
```

⚠️ This is the **crash-hardening path** (the task_wdt work from v1.8.0). Port it
first and bench-verify **both failure modes**: (a) a deliberately hung scan loop
fires `task_wdt` with the RTC phase forensics intact, and (b) a starved-but-not-
hung task (busy higher-priority work) behaves as intended.
`esp_task_wdt_add(NULL)` / `esp_task_wdt_reset()` are unchanged.

### 3.2 MUST FIX — single-core task affinity (C3 today, C6/C5 tomorrow)

**Six tasks are explicitly pinned to core 1**: NFC scan (`NFCManager.cpp:203`),
SpoolmanSync (`SpoolmanManager.cpp:1295`), HATask (`HomeAssistantManager.cpp:258`),
LEDTask (`LEDManager.cpp:56`), LCDTask (`LCDManager.cpp:98`), and DiagTask
(`DiagnosticsManager.cpp`, lands with #253). Two more pin to core 0 (TFTTask,
OTATask). On the single-core **C3 this ships today under Arduino 2.x**, which
tolerated an out-of-range core ID — but **IDF 5.x tightened core-ID validation**,
so `xTaskCreatePinnedToCore(..., 1)` on a unicore target may assert or fail
outright. This is therefore a **legacy-env regression risk (C3) the moment the
platform swaps**, not just a new-chip concern.

Fix pattern — portable affinity per Espressif's own guidance
([FreeRTOS additions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/freertos_additions.html)):
use `tskNO_AFFINITY` / plain `xTaskCreate()` on unicore targets and keep the
intentional pinnings only on dual-core chips. Suggest one small helper instead
of eight scattered `#if`s:

```cpp
static BaseType_t createPinnedTask(TaskFunction_t fn, const char* name,
                                   uint32_t stack, void* arg,
                                   UBaseType_t prio, TaskHandle_t* handle,
                                   BaseType_t core) {
#if CONFIG_FREERTOS_UNICORE
    (void)core;
    return xTaskCreate(fn, name, stack, arg, prio, handle);
#else
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, handle, core);
#endif
}
```

Gate: **C3 must boot and scan in Phase 2/3**, before any C6/C5 work — it is the
in-fleet canary for this exact issue.

### 3.3 Verify-on-hardware — SPI bus mapping (the historic S3 collision fix)

`lib/PN5180/PN5180.cpp:33-40`: `PN5180_SPI_BUS` defaults to `FSPI` on S3 (bus 0
= SPI2) vs `HSPI` elsewhere; `esp32s3zero` overrides to `HSPI` in build flags
(platformio.ini:46) because its TFT owns SPI2. `src/TFTConfig.h` claims
`SPI3_HOST` (S3-DevKitC) / `SPI2_HOST` (Zero) / `VSPI_HOST` (WROOM).

The 3.x HAL kept `FSPI/HSPI/VSPI` defines, but **the numeric mapping is exactly
what caused the original TFT↔NFC bus collision** — re-verify per target on the
new core: build, then soak with TFT ON (the collision's failure mode was
task_wdt reboots every 8–18 min; a 2h clean TFT-on soak is the regression gate).
For C6/C5 (single GP-SPI, like the C3): they take the `#else HSPI` branch —
verify it maps to SPI2 on those targets or extend the `#if` chain explicitly.

### 3.4 Compiles-but-verify — everything else (audited clean)

| Surface | Finding |
|---|---|
| LEDC/PWM (`ledcSetup/ledcAttachPin`) | **Not used** anywhere (LED is NeoPixel/RMT; TFT backlight handled inside LovyanGFX) |
| HW timers (`timerBegin` signature change) | **Not used** |
| WiFi events (`WiFi.onEvent`, `SYSTEM_EVENT_*`) | **Not used** |
| Direct `esp_wifi_*`/`esp_netif` | **Not used** |
| `WiFi/HTTPClient/WiFiClientSecure/WebServer/ESPmDNS/Update/DNSServer` includes | 3.x ships compatibility headers (NetworkClient rename underneath); expect deprecation warnings only |
| mbedTLS (IDF5 ships mbedTLS 3.x) | Only `lib/bambutag/BambuKeyDeriver.cpp` — classic `mbedtls_md_*` HMAC API, which survived into mbedTLS 3.x. Verify compile; no struct-member poking |
| TLS certs | Only `setInsecure()` (WebServerManager.cpp:1123). The 3.x `setCACertBundle` signature change **doesn't affect us** |
| `esp_reset_reason`, `heap_caps_*`, `Preferences`/NVS, FreeRTOS APIs | Unchanged |
| ArduinoJson v7, PubSubClient 2.8, Keypad, LiquidCrystal_I2C, base64 | Hardware-agnostic, fine |
| Adafruit NeoPixel (resolves 1.15.4) | C6 RMT backend supported; **C5 needs latest lib — verify** |
| Adafruit PN532 1.3.4 | Plain SPI API; our conn does `SPI.begin(custom pins)` — fine |
| LovyanGFX 1.2.21 | IDF5/3.x and C6 are compile-compatible. The pinned release lacks the ESP32-C5 GPIO-register branch, so C5 TFT remains deferred while the capability-ready shared transport stays generic (§4.3). |
| htcw_json/htcw_io 0.2.5 | Pure C++/streams; main risk is GCC-14 strictness — compile check |
| In-repo C libs (openprinttag, opentag3d, tigertag) | Portable C; native tests already build them with modern GCC |

Per the exact-pins policy (#216), any lib bump forced by 3.x gets its own
deliberate commit with all envs built.

### 3.5 Flash budget — and why gzip (#250) must merge first

App slot is `0x1E0000` (1,966,080 B), dual-OTA on 4MB. Current usage:
esp32dev **87.3%**, c3 85.7%, s3zero 84.8%, s3devkitc 85.0%. The 3.x core
typically adds ~100–200KB. **Mitigation already in flight: PR #250 (pre-gzipped
web assets) reclaims ~174KB (~9%)** — roughly canceling the growth.
**Sequence #250 before the migration.** Partition table itself is unchanged
(NVS at 0x9000 keeps its format — configs survive).

### 3.6 OTA continuity — the biggest field risk

Field units run a 2.0.17/IDF-4.4-era **bootloader**; an OTA update replaces only
the app. Espressif supports **older bootloaders booting newer apps — but NOT
newer bootloaders booting older apps**
([bootloader compatibility](https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32/api-guides/bootloader.html)),
so downgrade behavior matters as much as upgrade continuity. Bench the full
matrix on the S3, don't assume any cell:

| # | Bootloader | Start app | Action | Expected / decides |
|---|---|---|---|---|
| 1 | old (IDF 4.4) | v1.8.x | OTA → 3.x app | The upgrade path for every field unit — must boot, scan, sync |
| 2 | old (IDF 4.4) | 3.x | OTA → another 3.x app | Ongoing updates post-migration (both slots) |
| 3 | old (IDF 4.4) | 3.x | OTA → v1.8.x app (downgrade) | The emergency escape hatch for OTA-only field units — verify it works |
| 4 | new (IDF 5.5, via full web-flash) | 3.x | OTA → v1.8.x app | **Expected unsupported** — document the recovery (full web-flash of the old release) and say so in release notes |

If row 1 fails, the 3.x release must be flagged "web-flasher full flash
required" — a breaking-change communication, not a fix. Row 4's asymmetry means
release notes must warn that after a *full* 3.x web-flash, downgrading requires
another full web-flash (OTA-only units keep their old bootloader and retain the
row-3 escape hatch).

### 3.7 Task stacks

IDF 5 tasks tend to need slightly more stack. We have live HWM telemetry
(`MemoryDiagnostics`, `/api/diagnostics`) — after the port, watch the floors;
LEDTask (2048) and HATask (historically tightest) are the ones to bump if <512
free. The self-test wizard (#253) makes this a one-click check.

## 4. New chip targets

### 4.1 ESP32-C6 (first new target — core support mature since 3.0)

- Env: `esp32c6` / board `esp32-c6-devkitc-1`, flags mirroring the C3 env
  (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1` — USB-Serial/JTAG console).
- `include/BoardPins.h`: new `#elif defined(BOARD_ESP32_C6)` section. Don't
  invent the map from memory — derive from the C6-DevKitC-1 datasheet at
  bring-up, avoiding straps (GPIO9/15), USB-JTAG (12/13), and flash pins.
- **Default status-LED pin = GPIO8**: the DevKitC-1 has an onboard WS2812 —
  exactly what LEDManager already drives — so the status LED needs zero
  external wiring. GPIO8 is also a strapping pin, so the C6 blocklist must
  treat it as allowed-with-care (Espressif themselves wired the LED there;
  driving WS2812 data post-boot doesn't disturb strapping) rather than
  blanket-rejecting straps like the S3/C3 lists do.
- `src/ConfigurationManager.cpp`: add a C6 blocklist alongside the existing
  C3/S3/WROOM ones. On dev today the validator is `sanitizeLedPin()` (~line 72);
  **after #247 merges** it becomes the shared `boardPinUsable()` — this epic
  runs after #247 (Phase 0), so target `boardPinUsable()`.
- **Synergy: #247 (runtime NFC pins) merges before this** — new-chip pin
  defaults only need to be *sane*; users can remap from the config page without
  reflashing. That materially de-risks bring-up.
- Feature stance at launch: NFC (PN5180 + PN532), LCD/keypad/LED, full web UI;
  TFT unsupported (single SPI bus, same as C3).

### 4.2 ESP32-C5 (second target — newest silicon)

- Same shape as C6 (env `esp32c5` / `esp32-c5-devkitc-1`, BoardPins +
  blocklist sections). Requires the newest pioarduino/3.3.x; treat C5 core
  support as **newer/less soaked** than C6 — ship it with a call-for-testers.
- SRAM (~400KB class) is a non-issue — the C3 proves the footprint.
- 5 GHz WiFi needs zero app changes (band-agnostic WiFi API), but bench-test
  association on a 5 GHz-only SSID as a checklist item.
- Check lib support (NeoPixel RMT on C5, etc.) at bring-up — C5 lags C6 in
  third-party libs.

### 4.3 Shared SPI for TFT + NFC on C5/C6 (last engineering phase)

This is **not required to prove the Arduino 3.x migration or ship NFC-capable
C5/C6 builds**. Keep TFT disabled on the single-GP-SPI targets through initial
bring-up, then tackle sharing after both chips are stable. Electrically, SPI is
designed for this: TFT and the selected NFC reader share SCK/MOSI/MISO and each
gets its own CS. The work is software ownership and failure isolation.

Current architecture cannot safely do that by changing pins alone:

- `lib/PN5180/PN5180.cpp` owns a file-static Arduino `SPIClass` and wraps its
  commands in `beginTransaction` / `endTransaction`.
- PN532 uses the global Arduino `SPI` object through Adafruit_PN532.
- LovyanGFX creates and initializes its own IDF-backed `lgfx::Bus_SPI`, with
  `bus_shared = false` today.
- NFC and TFT run in different FreeRTOS tasks. This still matters on a
  single-core chip because a task can be preempted while a transaction waits.

Implementation sequence:

1. **Spike bus coexistence first.** On C6, put PN5180 and a write-only TFT on
   the same host/pins with distinct CS lines. Confirm whether LovyanGFX and
   Arduino `SPIClass` can coexist when the host is initialized once. If the two
   drivers still compete for initialization/ownership, stop the spike and use
   one of the clean fallbacks below rather than layering locks over two owners.
2. **Centralize bus ownership.** Initialize the host exactly once and expose a
   shared bus/transaction abstraction. Preferred first attempt: inject the
   shared Arduino bus into the in-repo PN5180 transport and configure LovyanGFX
   for a shared bus. Clean fallback: port the small PN5180 transport layer to
   ESP-IDF `spi_device` so NFC and LovyanGFX use the same underlying driver.
   Changing the TFT library to one that accepts the shared `SPIClass` is the
   other fallback. Do not repeatedly tear down/reinitialize the host at runtime.
3. **Serialize transactions.** Add one recursive bus mutex/RAII guard used by
   PN5180, PN532, and every TFT hardware transfer (including init, brightness,
   and `pushSprite`). Apply settings per device: PN5180 mode 0 at <=7 MHz; TFT
   mode 0 at its validated rate (currently 40 MHz). Assert only the active
   device's CS. A safe first version may hold the lock for one complete logical
   NFC command; all BUSY waits must remain bounded so a wedged reader cannot
   freeze display access forever.
4. **Make the wiring fail-safe.** Both CS lines default HIGH with pull-ups.
   Prefer a write-only TFT with MISO disconnected; otherwise prove its MISO
   output is high-impedance whenever TFT CS is HIGH. TFT reset/backlight may be
   tied or assigned separate GPIOs according to the board pin budget. Unused
   PN5180 IRQ/GPIO/AUX pins need explicit `-1`/optional handling rather than
   dummy GPIO assignments.
5. **Enable by capability, not chip name alone.** Keep the existing separate-bus
   path for WROOM/S3. Add a board capability flag for shared SPI and only expose
   TFT in config when that board has a validated pin map. Target C6 first, then
   repeat on C5; C3 can opt in later if its tighter exposed-pin budget permits.
   Cover the runtime-selected PN5180 and PN532 paths, not only the default reader.

Shared-SPI regression gate:

- Cold boot repeatedly with both CS pull-ups installed; neither peripheral may
  be spuriously selected and both must initialize reliably.
- Run TFT animations/full-frame `pushSprite` updates while continuously
  detecting, reading, writing, and verifying every supported tag format.
- Repeat with PN532 selected and its supported ISO14443A formats.
- Deliberately wedge PN5180 BUSY and force TFT traffic; bounded NFC failure must
  not corrupt the display, deadlock the bus, or prevent the watchdog forensics.
- Force heavy TFT updates while scanning; no missed-session regression beyond a
  documented latency budget and no tag corruption are acceptable.
- Review internal heap/largest-block and TFT sprite allocation on the no-PSRAM
  C5/C6 boards (8-bit 240x240 sprite fallback is expected).
- Pass a **2-hour active soak minimum, then an overnight soak** on both C6 and
  C5 with zero SPI errors, watchdog resets, corrupt frames, or failed tag writes.

#### Phase 7 engineering status — 2026-07-12

- **C6 code complete; hardware validation pending.** `BOARD_SHARED_SPI` enables
  the TFT and injects Arduino's global `SPI` handle into both NFC transports.
  LovyanGFX initializes the host once at boot and is configured with
  `bus_shared = true`; NFC initialization adopts that handle and never ends or
  reinitializes it.
- **MISO routing is load-bearing and non-obvious.** On the single-GP-SPI boards
  LovyanGFX's `spi::init` performs the one and only Arduino `SPI.begin()` for the
  whole bus (its host equals the Arduino default host), and Arduino 3.3.9
  `SPIClass::begin()` returns immediately on a second call — it never re-attaches
  MISO. The TFT is write-only (`PIN_TFT_MISO = -1`), so the LovyanGFX bus config
  is deliberately given `pin_miso = PIN_PN5180_MISO`; otherwise the shared bus
  comes up with no MISO routed and **NFC cannot read any tag response**. Do not
  reorder init so NFC begins the bus first (LovyanGFX would tear it down with
  `SPI.end()`), and do not set the TFT bus MISO back to `-1`. `ConfigurationManager`
  rejects a runtime NFC SCK/MOSI/MISO remap that would diverge from this bus while
  TFT is enabled. `TFTManager::begin()` sets a `_began` flag on success; a failed
  shared-bus init skips `startTask()` so no task renders to an uninitialized panel.
- A single recursive, bounded RAII guard covers complete PN5180 commands
  (including bounded BUSY waits), PN532 calls and OpenPrintTag HAL callbacks,
  and all TFT hardware traffic. Both CS outputs are driven HIGH before bus init
  and after every guarded transaction. PN5180 remains MODE0 at 7 MHz and the
  write-only TFT remains MODE0 at 40 MHz.
- C6 pin map: shared SCK GPIO6, shared MOSI GPIO7, NFC MISO GPIO2/NSS GPIO10,
  TFT CS GPIO3/DC GPIO11/RST GPIO18, TFT MISO disconnected. Unused PN5180
  IRQ/GPIO/AUX are `-1` and optional in the connection setup.
- **C5 TFT cleanly deferred.** Its capability-ready map is SCK GPIO6, MOSI
  GPIO8, NFC MISO GPIO9/NSS GPIO10, TFT CS GPIO4/DC GPIO5/RST GPIO12. LovyanGFX
  1.2.21 does not compile its ESP32 GPIO backend for C5; the C5 environment
  therefore retains `BOARD_NO_TFT`, the library/source exclusions, and the
  NFC-only bus owner. No dependency pin or generated library source was
  patched to force an unsafe result.
  - **Why not patch 1.2.21 for C5 now:** LovyanGFX issue
    [#700](https://github.com/lovyan03/LovyanGFX/issues/700) documents the only
    sanctioned path — a collaborator directs users to the **`develop`** branch
    plus a C5 device profile and extending every `CONFIG_IDF_TARGET_ESP32C3/C6`
    conditional to include C5. The pinned 1.2.21 release has ~26 such
    register-level guards in `esp32/common.cpp` alone; aliasing C5 onto the C6
    branches there is an unverified assumption about C5 silicon register layout,
    it contradicts our pin-to-1.2.21 constraint (upstream says use `develop`),
    and there is no C5 hardware to validate the resulting binary. A fail-closed
    build-time patch is therefore deferred rather than shipped blind. Revisit
    when LovyanGFX cuts a release with first-class C5 support.
- Bench execution is the hardware owner's task. Every gate above is mapped to
  reproducible steps and acceptance criteria in
  `docs/shared-spi-bench-checklist.md`; no physical coexistence or soak claim
  is made by this code-only phase.

If the gate does not pass inside the timebox, defer shared TFT without blocking
the Arduino 3.x/C5/C6 release. The shipped capability remains NFC + LCD/keypad/
LED/web, and the docs continue to label TFT unsupported on single-SPI targets.

### 4.4 CI / release / web flasher (cross-repo)

- `.github/workflows/ci.yml:24` matrix: add `esp32c6` (later `esp32c5`).
- `.github/workflows/release.yml:31-56`: add build + `firmware/bootloader/
  partitions` copies (+ sha256 sidecars from #227) for each new env.
- **spoolsense.org web flasher**: add manifests for the new boards. esp-web-tools
  supports `chipFamily: ESP32-C6`; **verify C5 support in esp-web-tools before
  promising a C5 web-flash path** — if absent, C5 ships "CLI flash only" initially.

## 5. Execution phases

| Phase | Work | Gate |
|---|---|---|
| **0** | Flush the merge queue first: #246, #247, #250, #253 → release. Migration branches off that dev. | Queue empty; release shipped |
| **1** | Spike branch: swap platform to pinned pioarduino release; `pio run -e esp32s3devkitc`; catalogue every error/warning (timeboxed ~half day) | Error inventory written |
| **2** | Port WDT (§3.1) + single-core affinity helper (§3.2); fix stragglers; all 4 legacy envs compile; native tests green; warning triage | 4 envs + tests green |
| **3** | **Bench validation on S3** — the regression suite: TFT-on 2h soak (SPI collision), deliberate-hang → task_wdt panic fires, wedge fail-fast + RF_STATUS behavior, scan/write/verify all tag formats, Spoolman sync, **OTA matrix (§3.6)**, NVS config survival, mDNS, task-stack HWM review; **C3 boot+scan sanity** (unicore-affinity canary, §3.2) | Clean soak + OTA matrix + C3 boots |
| **4** | C6 bring-up: env + BoardPins + blocklist; **add env to ci.yml at bring-up** (release packaging stays Phase 6); buy ESP32-C6-DevKitC-1; bench NFC+WiFi+web; self-test wizard run on C6 | C6 scans a tag, syncs Spoolman, CI green |
| **5** | C5 bring-up (same shape; newest pioarduino; 5 GHz association test; ci.yml env at bring-up) | C5 functional or explicitly deferred |
| **6** | release.yml packaging + spoolsense.org manifests (+ esp-web-tools C5 check) — CI envs already added in Phases 4/5 | Release pipeline builds 5–6 targets |
| **7** | **Code complete for C6; bench validation pending. C5 capability-ready but TFT deferred on LovyanGFX 1.2.21.** Shared owner + recursive guard cover both readers and TFT (§4.3). | C6 must still pass both-reader active/overnight bench gates; enable C5 only after library support and the same gates |
| **8** | Ship as a **major/mid version bump** with call-for-testers notes (OTA up/downgrade notes front and center; shared-TFT support stated per actual Phase 7 result) | Release + field feedback window |

## 6. Estimates & shopping list

- Phase 1–2: **1–2 bench days** (the API surface is genuinely small — WDT + warnings).
- Phase 3: ~1 elapsed week (soaks dominate).
- Phase 4: 1–2 days once hardware arrives.
- Phase 7 shared SPI: **2–4 engineering days** plus overnight soaks; timebox the
  initial two-driver coexistence spike to half a day before choosing a fallback.
- Hardware: **ESP32-C6-DevKitC-1** (~$8), **ESP32-C5-DevKitC-1** (availability
  varies), plus the existing bench PN5180.

## 7. Open questions (answer at kickoff)

1. Confirm the pioarduino pin at kickoff (current stable 2026-07-11: `55.03.39` = core 3.3.9/IDF 5.5.4; newer may exist by then).
2. OTA matrix results (§3.6) — decides the release messaging.
3. esp-web-tools C5 chipFamily support — decides C5 web-flash vs CLI-only.
4. Do we add H2/P4 envs while we're here? (Zero user demand today — suggest no.)
5. Flash headroom on esp32dev after the port — if <5% even with gzip, consider
   trimming (e.g. `CORE_DEBUG_LEVEL=0` is already default; last resort is a
   larger-flash board recommendation, not partition surgery on field units).
6. Shared-SPI ownership result (§4.3): can pinned LovyanGFX coexist cleanly with
   an injected Arduino `SPIClass`, or should PN5180 move to IDF `spi_device`?

## 8. Explicit non-goals

- Arduino core 4.x (alpha) — revisit in a year.
- Thread/Zigbee/Matter features on C6 — the migration only *unlocks* the radio;
  building on it is separate product work.
- Shared TFT on C3 — Phase 7 targets C6/C5; C3 only opts in later if its board
  pin budget and the same regression gates pass.
