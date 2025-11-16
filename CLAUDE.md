# TinyUSB Development Guide for RW612

This is a TinyUSB fork customized for the **NXP RW612** board with integrated lwIP networking stack.

## RW612-Specific Information

### Board Details
- **Board**: NXP FRDM-RW612
- **MCU**: RW612 (Cortex-M33)
- **USB Driver**: ChipIdea High-Speed (ci_hs)
- **Network Stack**: lwIP integration for network examples
- **Network Class**: NCM (Network Control Model) - Windows 10+ compatible

### Key Differences from Upstream TinyUSB
- RW612 BSP (Board Support Package) in `hw/bsp/rw612/`
- lwIP networking stack integration
- NCM driver enabled instead of RNDIS for better Windows 10+ compatibility
- FreeRTOS configuration for RW612

### SDK Requirements
This repository expects the NXP RW612 SDK to be available in the parent directory structure:
- SDK drivers should be in `../../drivers/`
- SDK device files in `../../device/`
- SDK CMSIS in `../../CMSIS/`
- SDK startup files in `../../startup/`

The build system will automatically locate these based on the project structure.

## Build Commands

### CMake Build System (Preferred)
CMake with Ninja is the preferred build method for TinyUSB development.

- Build example for RW612 with Ninja:
  ```bash
  cd examples/device/cdc_msc
  mkdir build && cd build
  cmake -G Ninja -DBOARD=frdm_rw612 ..
  ninja
  ```
- Debug build: `cmake -G Ninja -DBOARD=frdm_rw612 -DCMAKE_BUILD_TYPE=Debug ..`
- With logging: `cmake -G Ninja -DBOARD=frdm_rw612 -DLOG=2 ..`
- With RTT logger: `cmake -G Ninja -DBOARD=frdm_rw612 -DLOG=2 -DLOGGER=rtt ..`
- Flash with JLink: `ninja cdc_msc-jlink`
- Flash with OpenOCD: `ninja cdc_msc-openocd`
- List all targets: `ninja -t targets`

### Make Build System (Alternative)
- Build example: `cd examples/device/cdc_msc && make BOARD=frdm_rw612 all`
- For specific example: `cd examples/{device|host|dual}/{example_name} && make BOARD=frdm_rw612 all`
- Flash with JLink: `make BOARD=frdm_rw612 flash-jlink`
- Flash with OpenOCD: `make BOARD=frdm_rw612 flash-openocd`
- Debug build: `make BOARD=frdm_rw612 DEBUG=1 all`
- With logging: `make BOARD=frdm_rw612 LOG=2 all`
- With RTT logger: `make BOARD=frdm_rw612 LOG=2 LOGGER=rtt all`

### Additional Options
- Select RootHub port: `RHPORT_DEVICE=1` (make) or `-DRHPORT_DEVICE=1` (cmake)
- Set port speed: `RHPORT_DEVICE_SPEED=OPT_MODE_FULL_SPEED` (make) or `-DRHPORT_DEVICE_SPEED=OPT_MODE_FULL_SPEED` (cmake)

### Dependencies
- Get dependencies: `python tools/get_deps.py rw612`
- Or from example: `cd examples/device/cdc_msc && make BOARD=frdm_rw612 get-deps`

### RW612-Specific Examples
- **net_lwip_webserver**: Network example with lwIP web server (recommended for RW612)
- **cdc_msc**: Combined CDC and Mass Storage device
- **cdc_msc_freertos**: CDC/MSC with FreeRTOS support
- All standard TinyUSB examples are available for RW612

### Testing
- Run unit tests: `cd test/unit-test && ceedling test:all`
- Run specific test: `cd test/unit-test && ceedling test:test_fifo`

### Pre-commit Hooks
Before building, it's recommended to run pre-commit to ensure code quality:
- Run pre-commit on all files: `pre-commit run --all-files`
- Run pre-commit on staged files: `pre-commit run`
- Install pre-commit hook: `pre-commit install`

## Code Style Guidelines
- Use C99 standard
- Memory-safe: no dynamic allocation
- Thread-safe: defer all interrupt events to non-ISR task functions
- 2-space indentation, no tabs
- Use snake_case for variables/functions
- Use UPPER_CASE for macros and constants
- Follow existing variable naming patterns in files you're modifying
- Include proper header comments with MIT license
- Add descriptive comments for non-obvious functions
- When including headers, group in order: C stdlib, tusb common, drivers, classes
- Always check return values from functions that can fail
- Use TU_ASSERT() for error checking with return statements

## Project Structure
- src/: Core TinyUSB stack code
- hw/: Board support packages and MCU drivers
- examples/: Reference examples for device/host/dual
- test/: Unit tests and hardware integration tests

## Release Process
To prepare a new release:
1. Update the `version` variable in `tools/make_release.py` to the new version number
2. Run the release script: `python tools/make_release.py`
   - This will update version numbers in `src/tusb_option.h`, `repository.yml`, and `library.json`
   - It will also regenerate documentation
3. Update `docs/info/changelog.rst` with release notes
4. Commit changes and create release tag
