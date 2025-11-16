# RW612 USB PHY Init - Reference Implementation Comparison

## Analysis of Similar NXP Boards in TinyUSB

I've examined all NXP boards using the **ChipIdea HS USB controller** to find the best reference for RW612.

---

## **Boards Using ChipIdea HS USB Controller**

| Board Family | File | USB Init Method | Complexity |
|--------------|------|-----------------|------------|
| **MCX (MCXN9)** | `hw/bsp/mcx/family.c` | Direct USBPHY register access | ✅ **BEST** |
| **iMXRT** | `hw/bsp/imxrt/family.c` | Direct USBPHY register access | Good |
| **LPC18/43** | `hw/bsp/lpc18/family.c` | Chip library (`Chip_USB0_Init()`) | Library-based |
| **RW612** | `hw/bsp/rw612/family.c` | ❌ **MISSING** | None |

---

## **Reference 1: MCX MCXN9 (BEST MATCH)**

**Why this is the best reference for RW612:**
- ✅ **Newest NXP architecture** (similar to RW612)
- ✅ **Direct USBPHY peripheral access** (no library needed)
- ✅ **Complete USB PHY initialization** in family.c
- ✅ **Clear, well-documented code**

### **MCX USB PHY Initialization Code**

**Location**: `hw/bsp/mcx/family.c:133-185`

```c
#if defined(BOARD_TUD_RHPORT) && BOARD_TUD_RHPORT == 1 && (CFG_TUSB_MCU == OPT_MCU_MCXN9)
  // Port1 is High Speed

  // === POWER CONFIGURATION ===
  SPC0->ACTIVE_VDELAY = 0x0500;
  /* Change the power DCDC to 1.8v (By default, DCDC is 1.8V), CORELDO to 1.1v (By default, CORELDO is 1.0V) */
  SPC0->ACTIVE_CFG &= ~SPC_ACTIVE_CFG_CORELDO_VDD_DS_MASK;
  SPC0->ACTIVE_CFG |= SPC_ACTIVE_CFG_DCDC_VDD_LVL(0x3) | SPC_ACTIVE_CFG_CORELDO_VDD_LVL(0x3) |
                      SPC_ACTIVE_CFG_SYSLDO_VDD_DS_MASK | SPC_ACTIVE_CFG_DCDC_VDD_DS(0x2u);
  /* Wait until it is done */
  while (SPC0->SC & SPC_SC_BUSY_MASK) {}
  if (0u == (SCG0->LDOCSR & SCG_LDOCSR_LDOEN_MASK)) {
    SCG0->TRIM_LOCK = 0x5a5a0001U;
    SCG0->LDOCSR |= SCG_LDOCSR_LDOEN_MASK;
    /* wait LDO ready */
    while (0U == (SCG0->LDOCSR & SCG_LDOCSR_VOUT_OK_MASK));
  }

  // === USB CLOCK ENABLE ===
  SYSCON->AHBCLKCTRLSET[2] |= SYSCON_AHBCLKCTRL2_USB_HS_MASK | SYSCON_AHBCLKCTRL2_USB_HS_PHY_MASK;
  SCG0->SOSCCFG &= ~(SCG_SOSCCFG_RANGE_MASK | SCG_SOSCCFG_EREFS_MASK);
  /* xtal = 20 ~ 30MHz */
  SCG0->SOSCCFG = (1U << SCG_SOSCCFG_RANGE_SHIFT) | (1U << SCG_SOSCCFG_EREFS_SHIFT);
  SCG0->SOSCCSR |= SCG_SOSCCSR_SOSCEN_MASK;
  while (1) {
    if (SCG0->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK) {
      break;
    }
  }

  SYSCON->CLOCK_CTRL |= SYSCON_CLOCK_CTRL_CLKIN_ENA_MASK | SYSCON_CLOCK_CTRL_CLKIN_ENA_FM_USBH_LPT_MASK;
  CLOCK_EnableClock(kCLOCK_UsbHs);
  CLOCK_EnableClock(kCLOCK_UsbHsPhy);
  CLOCK_EnableUsbhsPhyPllClock(kCLOCK_Usbphy480M, 24000000U);
  CLOCK_EnableUsbhsClock();

  // === USB PHY CONFIGURATION ===
#if ((!(defined FSL_FEATURE_SOC_CCM_ANALOG_COUNT)) && (!(defined FSL_FEATURE_SOC_ANATOP_COUNT)))
  USBPHY->TRIM_OVERRIDE_EN = 0x001fU; /* override IFR value */
#endif

  // Enable PHY support for Low speed device + LS via FS Hub
  USBPHY->CTRL |= USBPHY_CTRL_SET_ENUTMILEVEL2_MASK | USBPHY_CTRL_SET_ENUTMILEVEL3_MASK;

  // Enable all power for normal operation
  USBPHY->PWD = 0;

  // TX Timing
  uint32_t phytx = USBPHY->TX;
  phytx &= ~(USBPHY_TX_D_CAL_MASK | USBPHY_TX_TXCAL45DM_MASK | USBPHY_TX_TXCAL45DP_MASK);
  phytx |= USBPHY_TX_D_CAL(0x04) | USBPHY_TX_TXCAL45DP(0x07) | USBPHY_TX_TXCAL45DM(0x07);
  USBPHY->TX = phytx;
#endif
```

### **Key Steps in MCX USB PHY Init:**

1. **Power Domain Setup** (lines 136-149)
   - Configure DCDC/LDO voltages
   - Enable LDO for USB
   - Wait for power stability

2. **Clock Configuration** (lines 150-166)
   - Enable USB HS and PHY clocks
   - Configure oscillator (20-30 MHz XTAL)
   - Enable USB PHY PLL (480 MHz)

3. **USB PHY Register Setup** (lines 168-184)
   - Override trim values (if needed)
   - Enable low-speed support
   - **Power on PHY** (`USBPHY->PWD = 0`)
   - Configure TX timing/calibration

---

## **Reference 2: iMXRT (Previously Analyzed)**

**Location**: `hw/bsp/imxrt/family.c:76-107`

Similar to MCX but with different clock API:

```c
static void init_usb_phy(uint8_t usb_id) {
  USBPHY_Type *usb_phy = USBPHY1;

  // Clock init
  CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, BOARD_XTAL0_CLK_HZ);
  CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, BOARD_XTAL0_CLK_HZ);

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
```

**Difference from MCX:**
- Uses different clock API (`CLOCK_EnableUsbhs0PhyPllClock` vs `CLOCK_EnableUsbhsPhyPllClock`)
- Different TX calibration values (0x0C vs 0x04)
- No power domain setup (different MCU architecture)

---

## **Reference 3: LPC18/43 (Library-Based)**

**Location**: `hw/bsp/lpc18/family.c:106-107`, `hw/bsp/lpc43/family.c:129-177`

Uses NXP chip library:

```c
//------------- USB -------------//
Chip_USB0_Init();
Chip_USB1_Init();
```

**Inside `Chip_USB0_Init()` (from NXP lpcopen library):**
- Configures USB pins
- Enables USB clocks
- Initializes USB PHY registers
- Sets up VBUS detection

**Not suitable for RW612 because:**
- Requires NXP lpcopen library
- Library not available for RW612 in TinyUSB

---

## **What RW612 Needs (Analysis)**

### **RW612 USB Hardware:**
- **Controller**: ChipIdea HS USB OTG @ 0x40145000
- **Clock**: 40 MHz XTAL (refclk_phy)
- **PHY**: Integrated USB PHY (similar to MCX/iMXRT)

### **Current RW612 Code (BROKEN):**

**Location**: `hw/bsp/rw612/family.c:86-91`

```c
// USB Controller Initialization for RW612
// USB clock is configured by BOARD_BootClockRUN()
// Just enable the clock gate and reset the peripheral
CLOCK_EnableClock(kCLOCK_Usb);
RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);
```

**Missing:**
- ❌ USB PHY clock initialization
- ❌ USB PHY power-on
- ❌ USB PHY register configuration
- ❌ TX calibration

---

## **Proposed Fix for RW612**

Based on MCX and iMXRT reference implementations, here's what RW612 needs:

### **Option A: Direct USBPHY Access (Like MCX/iMXRT)**

Add to `hw/bsp/rw612/family.c`:

```c
void board_init(void) {
  BOARD_InitPins();
  BOARD_BootClockRUN();

  // ... existing init ...

  // === USB PHY INITIALIZATION ===

  // 1. Enable USB clocks (may already be done by BOARD_BootClockRUN)
  CLOCK_EnableClock(kCLOCK_Usb);
  RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);

  // 2. Check if RW612 has USBPHY peripheral
  #ifdef USBPHY
    // Enable USB PHY clock (if separate from controller clock)
    // RW612 may have: CLOCK_EnableClock(kCLOCK_UsbPhy);

    // Power on USB PHY
    USBPHY->PWD = 0;  // Clear all power-down bits

    // Enable LS/FS support
    USBPHY->CTRL |= USBPHY_CTRL_SET_ENUTMILEVEL2_MASK | USBPHY_CTRL_SET_ENUTMILEVEL3_MASK;

    // TX timing calibration (use MCX values as starting point)
    uint32_t phytx = USBPHY->TX;
    phytx &= ~(USBPHY_TX_D_CAL_MASK | USBPHY_TX_TXCAL45DM_MASK | USBPHY_TX_TXCAL45DP_MASK);
    phytx |= USBPHY_TX_D_CAL(0x04) | USBPHY_TX_TXCAL45DP(0x07) | USBPHY_TX_TXCAL45DM(0x07);
    USBPHY->TX = phytx;
  #endif
}
```

### **Option B: Use NXP SDK USB Init Function**

If RW612 SDK provides USB init functions:

```c
#include "fsl_usb_device_config.h"  // From NXP SDK

void board_init(void) {
  // ... existing init ...

  // Use NXP SDK USB initialization
  USB_DeviceClockInit();    // Configure USB clocks
  USB_DevicePhyInit();      // Initialize USB PHY
}
```

---

## **Critical Questions to Answer**

1. **Does RW612 have a `USBPHY` peripheral?**
   - Check: `fsl_device_registers.h` for `USBPHY` or `USB_ANALOG` defines
   - If yes: Use MCX/iMXRT approach
   - If no: USB PHY might be integrated in USBOTG controller

2. **What USB PHY clock source does RW612 use?**
   - From clock_config.h: `refclk_phy = 40 MHz`
   - Might need: `CLOCK_EnableUsbhsPhyPllClock()` or similar

3. **Are there RW612 SDK USB examples?**
   - Download from: https://mcuxpresso.nxp.com/
   - Look for: `usb_device_cdc_vcom` example
   - Extract USB init code

---

## **Next Steps**

### **Step 1: Check RW612 Peripheral Availability**

Search RW612 SDK or device headers for:
```bash
grep -r "USBPHY\|USB_ANALOG" <rw612_sdk_path>/devices/RW612/
```

### **Step 2: Try Minimal USB PHY Init**

Add to `hw/bsp/rw612/family.c` (after existing USB init):

```c
// Experimental: Try USBPHY initialization
#ifdef USBPHY
  printf("Found USBPHY peripheral at 0x%08lX\n", (uint32_t)USBPHY);
  USBPHY->PWD = 0;  // Power on
  USBPHY->CTRL |= 0x0C000000;  // Enable UTM levels
  printf("USBPHY initialized\n");
#else
  printf("USBPHY peripheral not found - may be integrated\n");
#endif
```

### **Step 3: Test USB Enumeration**

Build, flash, and check if Windows detects the device.

---

## **Summary: Best Reference for RW612**

| Aspect | MCX MCXN9 | iMXRT | LPC18/43 |
|--------|-----------|-------|----------|
| **Architecture** | Modern (like RW612) | Older but similar | Old, library-based |
| **USB PHY Access** | Direct USBPHY registers | Direct USBPHY registers | Via Chip library |
| **Code Clarity** | ✅ Excellent | ✅ Good | ⚠️ Library hidden |
| **Applicability to RW612** | ✅✅✅ **BEST** | ✅✅ Good | ❌ Needs library |

**Recommendation**: Use **MCX MCXN9** as primary reference, with iMXRT as secondary.

---

## **Files to Examine**

### **In TinyUSB:**
- `hw/bsp/mcx/family.c` (lines 133-185) - **PRIMARY REFERENCE**
- `hw/bsp/imxrt/family.c` (lines 76-107) - Secondary reference
- `hw/bsp/lpc43/family.c` (lines 129-177) - Alternative approach

### **In NXP RW612 SDK:**
- `boards/frdmrw612/usb_examples/usb_device_cdc_vcom/board.c`
- `devices/RW612/drivers/fsl_clock.h` - Clock APIs
- `devices/RW612/RW612.h` - Peripheral definitions

---

**Last Updated**: 2025-11-16
**Analysis**: Comparison of USB PHY init across NXP boards
**Next**: Implement MCX-style USB PHY init for RW612
