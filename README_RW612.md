# TinyUSB Fork for RW612 (chipsoft/frdmrw612_freertos_hello)

**Base upstream version:** TinyUSB `0.21.0`
**Branch:** `rw612-0.21.0`

This is the RW612 firmware project's fork of [TinyUSB](https://github.com/hathach/tinyusb),
consumed as a git submodule at `app_libs/tinyusb-rw612` in
[frdmrw612_freertos_hello](https://github.com/chipsoft/frdmrw612_freertos_hello).

## Why a fork at all?

Upstream 0.21.0 added **native RW61x support** (`hw/bsp/rw61x/`, `OPT_MCU_RW61X`,
`ci_hs_rw61x.h`) — the board family this fork targets. As of the `rw612-0.21.0` sync,
the fork is intentionally thin: it carries forward only the handful of real
hardware-bug fixes the firmware team found necessary on real FRDM-RW612 boards, on top
of an otherwise-vanilla upstream tree.

## What the firmware actually builds

Only 7 files from this submodule are compiled into the firmware
(see `TINYUSB_SOURCES` in the parent repo's `Makefile`):

- `src/tusb.c`
- `src/common/tusb_fifo.c`
- `src/device/usbd.c`
- `src/class/net/ncm_device.c`
- `src/portable/chipidea/ci_hs/dcd_ci_hs.c`
- `lib/networking/dhserver.c`

Everything else in this tree (the example apps, `hw/bsp/`, the vendored `lib/lwip/`,
the doc tooling) is upstream TinyUSB infrastructure, unused by the firmware build.

## Local patches on top of upstream 0.21.0

Only 4 files carry a real patch:

| File | Patch |
|---|---|
| `src/common/tusb_types.h` | Byte-wise `wMaxPacketSize` read — works around an unaligned-load fault under ARMv8-M/TrustZone. |
| `src/class/net/net_device.h` | 4 accessor declarations (`tud_network_ncm_data_interface_active/host_configured/host_strictly_configured/tx_stalled`) consumed by the firmware's `bsp_usb_ncm.c` watchdog. |
| `src/portable/chipidea/ci_hs/dcd_ci_hs.c` | Active-qTD-overwrite guard in `dcd_edpt_xfer`. |
| `src/class/net/ncm_device.c` | NULL-deref HardFault guard + alt-flap flush, a race/lock around the glue NTB pointer, a notification-endpoint-busy deadlock fix, host-config TX gating with grace-fallback, and macOS NCM control-request interop completeness. |

Full rationale, per-fix hardware-bug references, and what was deliberately dropped as
superseded by upstream's native RW61x support live in the parent firmware repo:
`knowledge/tinyusb-patches.md`.

## Upstream sync

```bash
git fetch upstream --tags
git log --oneline <old-base>..<new-tag>   # scope the diff before touching anything
```

Given how thin this fork now is, the recommended approach for the *next* sync is not a
literal `git rebase -i` of the fork's commit history, but a fresh branch off the new
upstream tag with the 4 patches above re-applied and re-verified against whatever
upstream changed in the same files (upstream has substantially rewritten
`ncm_device.c` between releases before — read the new file, don't blindly replay old
diffs).

After any sync: build (`make clean && make trustzone` in the parent repo), then hardware
verify — USB NCM enumeration, a bench soak (`scripts/bench_usb_ncm.lua`), and
`test_device.lua` over the NCM transport. See the parent repo's `/verify` and `/test-cli`
skills.

## License

Same as TinyUSB: MIT License.
