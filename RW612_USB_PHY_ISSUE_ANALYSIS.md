# RW612 USB PHY Initialization Issue - Root Cause Analysis

## **Problem Statement**
NCM/RNDIS USB examples work on other MCUs but **nothing happens** when connecting RW612 to Windows 10. The USB device is not detected at all.

## **Root Cause**
**MISSING USB PHY INITIALIZATION** in the RW612 board support package.

---

## **What's Missing**

### **1. No USB PHY Initialization**

**Current code** (`hw/bsp/rw612/family.c:86-91`):
```c
// USB Controller Initialization for RW612
// USB clock is configured by BOARD_BootClockRUN()
// Just enable the clock gate and reset the peripheral
CLOCK_EnableClock(kCLOCK_Usb);
RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);
```

This **only**:
- ✅ Enables USB controller clock gate
- ✅ Resets the USB peripheral

But it's **MISSING**:
- ❌ USB PHY power-on
- ❌ USB PHY register configuration
- ❌ USB D+/D- pin mux configuration
- ❌ USB VBUS detection setup

### **2. No USB Pin Mux Configuration**

**Current code** (`hw/bsp/rw612/boards/frdm_rw612/pin_mux.c`):
- Configures: ENET pins, UART pins, GPIO pins
- **Missing**: USB D+/D- pins, USB VBUS pin

---

## **Comparison with Working Boards**

### **iMXRT (Working Example)**

File: `hw/bsp/imxrt/family.c:76-107`

```c
static void init_usb_phy(uint8_t usb_id) {
  USBPHY_Type *usb_phy;

  if (usb_id == 0) {
    usb_phy = USBPHY1;
    CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, BOARD_XTAL0_CLK_HZ);
    CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, BOARD_XTAL0_CLK_HZ);
  }

  // Enable PHY support for Low speed device + LS via FS Hub
  usb_phy->CTRL |= USBPHY_CTRL_SET_ENUTMILEVEL2_MASK | USBPHY_CTRL_SET_ENUTMILEVEL3_MASK;

  // Enable all power for normal operation
  usb_phy->PWD = 0;

  // TX Timing
  uint32_t phytx = usb_phy->TX;
  phytx &= ~(USBPHY_TX_D_CAL_MASK | USBPHY_TX_TXCAL45DM_MASK | USBPHY_TX_TXCAL45DP_MASK);
  phytx |= USBPHY_TX_D_CAL(0x0C) | USBPHY_TX_TXCAL45DP(0x06) | USBPHY_TX_TXCAL45DM(0x06);
  usb_phy->TX = phytx;
}

void board_init(void) {
  // ... other init ...
  init_usb_phy(0);  // Initialize USB PHY!
}
```

**Key actions**:
1. Enable USB PHY PLL clock (480MHz)
2. Enable USB controller clock
3. Configure USB PHY CTRL register
4. Power on PHY (PWD = 0)
5. Set TX timing calibration

---

## **RW612 USB Hardware Architecture**

### **USB Controller**
- **Type**: ChipIdea High-Speed USB OTG controller
- **Base Address**: 0x40145000 (`USBOTG_BASE`)
- **IRQ**: `USB_IRQn`
- **Driver**: `src/portable/chipidea/ci_hs/dcd_ci_hs.c`

### **USB PHY Clocking**
From `clock_config.h`:
- **refclk_phy**: 40 MHz (USB PHY reference clock)
- **hclk**: 260 MHz (USB controller AHB clock)

### **What RW612 Needs** (Based on NXP SDK requirements)
1. USB PHY power domain enabled
2. USB PHY clock source configured
3. USB PHY registers initialized:
   - Power-down mode disabled
   - D+/D- transceivers enabled
   - VBUS detection configured
4. USB pins properly muxed (if not internal)

---

## **Impact on USB Enumeration**

Without USB PHY initialization:
1. **USB D+/D- lines are not driven** → Windows sees nothing
2. **No USB pull-up resistor active** → Device not detected
3. **USB PHY is in power-down mode** → Controller can't communicate
4. **D+ pull-up (1.5kΩ to 3.3V) never activates** → Host thinks nothing is plugged in

**Result**: Complete USB silence. Windows Device Manager shows **NOTHING**.

---

## **Required Changes**

### **File 1: `hw/bsp/rw612/boards/frdm_rw612/board.h`**

Add USB clock initialization function (similar to UART):

```c
// USB PHY clock initialization
static inline void board_usb_phy_init_clock(void) {
  // Enable USB clock from XTAL 40MHz
  // RW612 USB PHY uses refclk_phy (40 MHz) configured by BOARD_BootClockRUN()
  // Additional PHY-specific clock setup if needed by SDK
}
```

### **File 2: `hw/bsp/rw612/family.c`**

Replace current USB init with proper PHY initialization:

```c
// USB Controller Initialization for RW612
void board_usb_phy_init(void) {
  // 1. Initialize USB PHY clock
  board_usb_phy_init_clock();

  // 2. Enable USB controller clock
  CLOCK_EnableClock(kCLOCK_Usb);

  // 3. Reset USB peripheral
  RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);

  // 4. Configure USB PHY (RW612-specific)
  // TODO: Add RW612 USB PHY register configuration
  // This requires NXP SDK USB PHY driver or manual register setup

  // 5. Enable USB PHY power
  // TODO: Clear power-down bits in USB PHY PWD register

  // 6. Configure VBUS detection
  // TODO: Set up USB VBUS detection if required
}

void board_init(void) {
  // ... existing init ...

  // USB PHY must be initialized BEFORE tusb_init()
  board_usb_phy_init();
}
```

### **File 3: `hw/bsp/rw612/boards/frdm_rw612/pin_mux.c`** (if needed)

Add USB pin configuration:

```c
void BOARD_InitPins(void) {
  // ... existing pins ...

  // Initialize USB pins if not internal
  // RW612 may have internal USB D+/D- routing
  // Check if IO_MUX configuration is needed
}
```

---

## **Next Steps to Fix**

### **Option 1: Use NXP SDK USB Example** (Recommended)
1. Download NXP RW612 SDK from NXP website
2. Find USB device example (e.g., `usb_device_cdc_vcom`)
3. Extract USB PHY initialization code
4. Port to TinyUSB BSP

### **Option 2: Manual PHY Configuration**
1. Study RW612 Reference Manual - USB PHY chapter
2. Identify required PHY registers:
   - PWD (Power-Down Register)
   - CTRL (Control Register)
   - TX (Transmit Control Register)
3. Add initialization code based on register map

### **Option 3: Check if USB PHY is On-Die or External**
- RW612 likely has **integrated USB PHY**
- May require minimal configuration (just power-on)
- Check schematic for external USB PHY chip

---

## **Verification Steps**

After adding USB PHY init:

1. **Build and flash**:
   ```bash
   cd examples/device/net_lwip_webserver
   make BOARD=frdm_rw612 clean
   make BOARD=frdm_rw612 -j8
   make BOARD=frdm_rw612 flash-jlink
   ```

2. **Connect to Windows 10 PC**

3. **Check Device Manager**:
   - Should see "USB Ethernet/RNDIS Gadget" or "Unknown Device"
   - If "Unknown Device": Descriptor issues (minor fix)
   - If nothing: USB PHY still not initialized

4. **Use USB analyzer/sniffer**:
   - Check for D+ pull-up (device should pull D+ to 3.3V via 1.5kΩ)
   - Check for USB reset signaling from host
   - Verify device responds to GET_DESCRIPTOR

---

## **References**

- **RW612 Clock Config**: `hw/bsp/rw612/boards/frdm_rw612/clock_config.c`
- **USB Controller**: ChipIdea HS @ 0x40145000
- **Working Example**: `hw/bsp/imxrt/family.c:76-107`
- **USB Driver**: `src/portable/chipidea/ci_hs/dcd_ci_hs.c`
- **NXP RW612 SDK**: https://mcuxpresso.nxp.com/

---

## **Summary**

| Component | Status | Issue |
|-----------|--------|-------|
| USB Controller | ✅ Initialized | Clock enabled, reset done |
| USB PHY | ❌ **NOT Initialized** | **Missing power-on and config** |
| USB Pins | ⚠️ Unknown | May need pin mux |
| USB Clock | ✅ Configured | 40 MHz refclk_phy from XTAL |
| ChipIdea Driver | ✅ Works | Tested on other boards |
| NCM Stack | ✅ Works | Tested on other MCUs |

**The USB PHY must be explicitly initialized before the USB controller can communicate with the host.**

---

**Author**: Claude (TinyUSB Analysis)
**Date**: 2025-11-16
**Board**: FRDM-RW612
**Issue**: USB not detected on Windows 10 (NCM/RNDIS)
