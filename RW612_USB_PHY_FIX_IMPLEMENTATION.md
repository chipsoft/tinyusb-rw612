# RW612 USB PHY Fix - Concrete Implementation

## Based on MCX MCXN9 Reference Code

After analyzing similar NXP boards (MCX, iMXRT, LPC), I've created a concrete fix for RW612 USB PHY initialization.

---

## **Problem Summary**

**Current code** (`hw/bsp/rw612/family.c:86-91`):
```c
// USB Controller Initialization for RW612
CLOCK_EnableClock(kCLOCK_Usb);
RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);
```

This **only** enables the clock and resets the controller. It **doesn't initialize the USB PHY**.

---

## **Solution: Implement USB PHY Initialization**

Based on **MCX MCXN9** (`hw/bsp/mcx/family.c:133-185`), here's the fix for RW612:

### **Implementation Options**

Since we don't have access to RW612 device headers in the current codebase, I'm providing **two approaches**:

---

## **Approach 1: Direct USBPHY Register Access (Preferred)**

### **Prerequisites:**
1. Verify RW612 has `USBPHY` peripheral (check NXP SDK device headers)
2. Ensure `fsl_device_registers.h` includes USBPHY definitions

### **Code Changes:**

**File**: `hw/bsp/rw612/family.c`

**Replace lines 86-91 with:**

```c
  //------------- USB PHY Initialization (based on MCX MCXN9) -------------//

  // Step 1: Enable USB controller clock
  CLOCK_EnableClock(kCLOCK_Usb);

  // Step 2: Reset USB controller
  RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);

  // Step 3: Enable USB PHY clock (if separate from controller)
  // Note: RW612 may have kCLOCK_UsbPhy or similar
  // Check clock_config.h for USB PHY clock enable function
  #ifdef kCLOCK_UsbPhy
    CLOCK_EnableClock(kCLOCK_UsbPhy);
  #endif

  // Step 4: Initialize USB PHY registers (if USBPHY peripheral exists)
  #ifdef USBPHY
    // 4a. Override trim values (if needed for RW612)
    #if !defined(FSL_FEATURE_SOC_CCM_ANALOG_COUNT) && !defined(FSL_FEATURE_SOC_ANATOP_COUNT)
      USBPHY->TRIM_OVERRIDE_EN = 0x001fU;  // Override IFR value
    #endif

    // 4b. Enable PHY support for Low-speed devices + LS via FS Hub
    USBPHY->CTRL |= USBPHY_CTRL_SET_ENUTMILEVEL2_MASK | USBPHY_CTRL_SET_ENUTMILEVEL3_MASK;

    // 4c. Power on USB PHY (critical!)
    USBPHY->PWD = 0;  // Clear all power-down bits

    // 4d. Configure TX timing/calibration
    uint32_t phytx = USBPHY->TX;
    phytx &= ~(USBPHY_TX_D_CAL_MASK | USBPHY_TX_TXCAL45DM_MASK | USBPHY_TX_TXCAL45DP_MASK);
    // Use MCX calibration values as starting point
    phytx |= USBPHY_TX_D_CAL(0x04) | USBPHY_TX_TXCAL45DP(0x07) | USBPHY_TX_TXCAL45DM(0x07);
    USBPHY->TX = phytx;
  #else
    #warning "USBPHY peripheral not found - USB may not work without PHY initialization"
  #endif
```

---

## **Approach 2: Use NXP SDK USB Init Functions**

### **Prerequisites:**
1. Download NXP RW612 SDK from https://mcuxpresso.nxp.com/
2. Extract USB device example (e.g., `usb_device_cdc_vcom`)
3. Copy USB init files to TinyUSB BSP

### **Steps:**

#### **1. Copy NXP SDK USB Init Files**

From NXP SDK, copy these files to `hw/bsp/rw612/`:

```
<nxp_sdk>/boards/frdmrw612/usb_examples/.../usb_device_config.c
<nxp_sdk>/boards/frdmrw612/usb_examples/.../usb_device_config.h
```

Or create wrapper functions based on SDK code.

#### **2. Modify `hw/bsp/rw612/family.c`**

Add include:
```c
#include "usb_device_config.h"  // From NXP SDK
```

Replace USB init section:
```c
  //------------- USB -------------//

  // Use NXP SDK USB initialization functions
  USB_DeviceClockInit();   // Configure USB clocks
  USB_DevicePhyInit();     // Initialize USB PHY
```

#### **3. Update Build System**

**For CMake** (`hw/bsp/rw612/family.cmake`):
```cmake
target_sources(${BOARD_TARGET} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/usb_device_config.c
)
```

**For Make** (`hw/bsp/rw612/family.mk`):
```make
SRC_C += $(BSP_PATH)/usb_device_config.c
```

---

## **Approach 3: Minimal Experimental Init**

If you want to test quickly without NXP SDK:

### **Minimal Code Addition**

**File**: `hw/bsp/rw612/family.c`

**After line 91, add:**

```c
  //------------- Experimental USB PHY Init -------------//

  // Attempt to initialize USB PHY if peripheral exists
  #ifdef USBPHY
    // Disable all power-down modes
    USBPHY->PWD = 0x00000000;

    // Enable basic PHY features
    // Bit 26: ENUTMILEVEL2 - Enable UTMI level2
    // Bit 27: ENUTMILEVEL3 - Enable UTMI level3
    USBPHY->CTRL |= (1 << 26) | (1 << 27);

    // Note: TX calibration omitted for minimal test
    // Add if needed after testing basic enumeration
  #elif defined(USB_ANALOG)
    // Some NXP MCUs use USB_ANALOG instead of USBPHY
    USB_ANALOG->PWD = 0x00000000;
    USB_ANALOG->CTRL |= (1 << 26) | (1 << 27);
  #else
    // Fallback: USB PHY may be integrated in USBOTG controller
    // Try accessing USB PHY through USBOTG base + offset
    volatile uint32_t *usb_phy_pwd = (volatile uint32_t *)(USBOTG_BASE + 0x800);  // Example offset
    volatile uint32_t *usb_phy_ctrl = (volatile uint32_t *)(USBOTG_BASE + 0x804);
    *usb_phy_pwd = 0x00000000;
    *usb_phy_ctrl |= (1 << 26) | (1 << 27);
  #endif
```

**Warning**: The fallback with hardcoded offsets is **experimental**. Use only for quick testing.

---

## **Complete Proposed Code for family.c**

Here's the complete `board_init()` function with USB PHY initialization:

```c
void board_init(void) {
  BOARD_InitPins();
  BOARD_BootClockRUN();

#if CFG_TUSB_OS == OPT_OS_NONE
  // 1ms tick timer
  SysTick_Config(SystemCoreClock / 1000);
#elif CFG_TUSB_OS == OPT_OS_FREERTOS
  // If freeRTOS is used, IRQ priority is limit by max syscall (smaller is higher)
  NVIC_SetPriority(USB_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
#endif

  // LED
  CLOCK_EnableClock(LED_CLK);
  gpio_pin_config_t led_config = {kGPIO_DigitalOutput, 0};
  GPIO_PinInit(LED_GPIO, 0, LED_PIN, &led_config);
  board_led_write(0);

  // Button
#ifdef BUTTON_GPIO
  CLOCK_EnableClock(BUTTON_CLK);
  gpio_pin_config_t const button_config = {kGPIO_DigitalInput, 0};
  GPIO_PinInit(BUTTON_GPIO, 0, BUTTON_PIN, &button_config);
#endif

#ifdef UART_DEV
  // Enable UART when debug log is on
  board_uart_init_clock();

  usart_config_t uart_config;
  USART_GetDefaultConfig(&uart_config);
  uart_config.baudRate_Bps = CFG_BOARD_UART_BAUDRATE;
  uart_config.enableTx = true;
  uart_config.enableRx = true;

  USART_Init(UART_DEV, &uart_config, CLOCK_GetFlexCommClkFreq(UART_FLEXCOMM_INST));
#endif

  //------------- USB Controller and PHY Initialization -------------//

  // Enable USB controller clock
  CLOCK_EnableClock(kCLOCK_Usb);

  // Reset USB controller
  RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);

  // Initialize USB PHY (if peripheral exists)
  #ifdef USBPHY
    // Enable USB PHY clock (if separate from controller)
    #ifdef kCLOCK_UsbPhy
      CLOCK_EnableClock(kCLOCK_UsbPhy);
    #endif

    // Override trim values (if needed)
    #if !defined(FSL_FEATURE_SOC_CCM_ANALOG_COUNT) && !defined(FSL_FEATURE_SOC_ANATOP_COUNT)
      USBPHY->TRIM_OVERRIDE_EN = 0x001fU;
    #endif

    // Enable PHY support for Low-speed devices
    USBPHY->CTRL |= USBPHY_CTRL_SET_ENUTMILEVEL2_MASK | USBPHY_CTRL_SET_ENUTMILEVEL3_MASK;

    // Power on USB PHY - CRITICAL!
    USBPHY->PWD = 0;

    // TX timing calibration
    uint32_t phytx = USBPHY->TX;
    phytx &= ~(USBPHY_TX_D_CAL_MASK | USBPHY_TX_TXCAL45DM_MASK | USBPHY_TX_TXCAL45DP_MASK);
    phytx |= USBPHY_TX_D_CAL(0x04) | USBPHY_TX_TXCAL45DP(0x07) | USBPHY_TX_TXCAL45DM(0x07);
    USBPHY->TX = phytx;
  #else
    // USB PHY initialization placeholder
    // TODO: Add RW612-specific USB PHY init based on NXP SDK
    #warning "USB PHY initialization not implemented - USB will not work"
  #endif
}
```

---

## **Testing the Fix**

### **Step 1: Build**
```bash
cd examples/device/net_lwip_webserver
make BOARD=frdm_rw612 clean
make BOARD=frdm_rw612 -j8
```

### **Step 2: Check for Warnings**
Look for:
- ❌ `warning: USBPHY peripheral not found` → USBPHY not defined
- ✅ No warnings → USBPHY initialization included

### **Step 3: Flash and Test**
```bash
make BOARD=frdm_rw612 flash-jlink
```

### **Step 4: Connect to Windows 10**

**Expected results:**

| Scenario | Observation | Meaning |
|----------|-------------|---------|
| ✅ **Success** | Device Manager shows "USB Ethernet/RNDIS Gadget" | USB PHY working! |
| ⚠️ **Partial** | "Unknown USB Device (Device Descriptor Request Failed)" | USB PHY works, descriptor issue |
| ❌ **Fail** | Nothing in Device Manager | USB PHY still not initialized |

### **Step 5: Debug Output**

Add debug prints to verify initialization:

```c
#ifdef USBPHY
  printf("USB: USBPHY found at 0x%08lX\n", (uint32_t)USBPHY);
  USBPHY->PWD = 0;
  printf("USB: PHY powered on (PWD=0x%08lX)\n", USBPHY->PWD);
  printf("USB: PHY CTRL=0x%08lX\n", USBPHY->CTRL);
#else
  printf("USB: WARNING - USBPHY peripheral not found!\n");
#endif
```

---

## **Troubleshooting**

### **Issue 1: USBPHY Not Defined**

**Error**:
```
warning: USBPHY peripheral not found - USB will not work
```

**Solution:**
1. Check if `fsl_device_registers.h` has USBPHY definition
2. Try NXP SDK Approach 2 (use SDK init functions)
3. Check RW612 reference manual for USB PHY base address

### **Issue 2: Build Errors with USBPHY Registers**

**Error**:
```
error: 'USBPHY' undeclared
error: 'USBPHY_CTRL_SET_ENUTMILEVEL2_MASK' undeclared
```

**Solution:**
- USBPHY peripheral may not be exposed in TinyUSB's RW612 headers
- Download NXP RW612 SDK for complete device definitions
- Use Approach 2 (NXP SDK functions)

### **Issue 3: Device Still Not Detected**

**Possible causes:**
1. **USB PHY clock not enabled** → Add `CLOCK_EnableClock(kCLOCK_UsbPhy);`
2. **Incorrect USB PHY peripheral** → Check if RW612 uses `USB_ANALOG` instead
3. **Missing pin mux** → Add USB pin configuration to `pin_mux.c`
4. **USB VBUS not detected** → Configure VBUS sensing

---

## **Verification Checklist**

Before claiming success, verify:

- [ ] Code compiles without warnings
- [ ] `USBPHY` peripheral is defined and initialized
- [ ] Windows detects USB device (any name)
- [ ] Can ping 192.168.7.1 from Windows
- [ ] Can access http://192.168.7.1

---

## **Next Steps**

1. **Try Approach 1** (Direct USBPHY) if `USBPHY` is defined
2. **Fall back to Approach 2** (NXP SDK) if USBPHY not available
3. **Use Approach 3** (Experimental) for quick testing
4. **Report results** and refine based on what works

---

## **Reference Code Files**

- **MCX MCXN9**: `hw/bsp/mcx/family.c:133-185` (PRIMARY)
- **iMXRT**: `hw/bsp/imxrt/family.c:76-107` (Secondary)
- **Current RW612**: `hw/bsp/rw612/family.c:86-91` (BROKEN)

---

**Author**: Claude
**Date**: 2025-11-16
**Status**: Implementation ready - needs testing
**Next**: Apply fix and test on hardware
