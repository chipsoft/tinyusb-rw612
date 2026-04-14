# TinyUSB RW612 Instructions

These instructions apply within `app_libs/tinyusb-rw612/`.

## Project Overview
- This is a TinyUSB fork customized for the NXP FRDM-RW612 board.
- MCU: RW612 (`Cortex-M33`)
- USB controller: ChipIdea High-Speed
- Networking focus: lwIP integration with NCM rather than RNDIS for Windows compatibility

## RW612-Specific Notes
- RW612 BSP support lives in `hw/bsp/rw612/`.
- The build expects the parent repo layout to provide `drivers/`, `device/`, `CMSIS/`, and `startup/`.
- Prefer RW612-focused examples such as `net_lwip_webserver`, `cdc_msc`, and `cdc_msc_freertos`.

## Build

### Preferred: CMake + Ninja
```bash
cd examples/device/cdc_msc
mkdir -p build && cd build
cmake -G Ninja -DBOARD=frdm_rw612 ..
ninja
```

### Make Alternative
```bash
cd examples/device/cdc_msc
make BOARD=frdm_rw612 all
```

### Useful Options
- Debug: `-DCMAKE_BUILD_TYPE=Debug` or `DEBUG=1`
- Logging: `-DLOG=2` or `LOG=2`
- RTT logging: `-DLOGGER=rtt` or `LOGGER=rtt`
- Root hub selection: `-DRHPORT_DEVICE=1` or `RHPORT_DEVICE=1`

## Dependencies and Validation
- Fetch dependencies with `python tools/get_deps.py rw612`.
- Run unit tests with `cd test/unit-test && ceedling test:all`.
- Run `pre-commit run --all-files` before finishing non-trivial changes when the environment supports it.
- Build at least one relevant RW612 example after stack changes.

## Code Style
- C99, 2-space indentation, no tabs.
- No dynamic allocation.
- Defer ISR-originated work to non-ISR task context.
- Use snake_case for functions/variables and UPPER_CASE for macros/constants.
- Follow existing include ordering and style in touched files.

## Release Notes
- If touching release automation, versioning, or generated docs, keep changes scoped and leave final release/commit steps for the maintainer unless explicitly asked.
