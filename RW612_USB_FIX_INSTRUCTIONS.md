# RW612 USB PHY Fix - Implementation Guide

## **Problem Identified**

The RW612 TinyUSB implementation is **missing USB PHY initialization**, causing Windows 10 to not detect the device at all when plugged in.

**Root Cause**: `hw/bsp/rw612/family.c` only enables the USB controller clock but **does not initialize the USB PHY** registers needed for USB communication.

---

## **Solution: Add USB PHY Initialization**

### **Step 1: Get NXP SDK USB Example**

The RW612 USB PHY requires NXP-specific initialization. Download the reference code:

1. **Go to**: https://mcuxpresso.nxp.com/
2. **Search for**: "RW612 SDK"
3. **Download**: SDK for RW612
4. **Find example**: `boards/frdmrw612/usb_examples/usb_device_cdc_vcom`
5. **Locate**: USB PHY init code in `board.c` or `usb_device_config.c`

Look for code that:
- Initializes USB clocks
- Configures USB PHY registers
- Enables USB VBUS detection
- Sets up USB D+/D- pins

---

## **Step 2: Identify Required Initialization**

From the NXP SDK example, you should find something similar to:

```c
// Example pattern from NXP SDK (adapt to actual RW612 API)
void USB_DeviceClockInit(void)
{
    // Enable USB clock from reference clock
    CLOCK_AttachClk(kXTAL_to_USB_CLK);      // Attach 40MHz XTAL to USB
    CLOCK_SetClkDiv(kCLOCK_DivUsbClk, 1U);  // USB clock divider

    // Enable USB controller clock
    CLOCK_EnableClock(kCLOCK_Usb);

    // Reset USB controller
    RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);
}

void USB_DevicePhyInit(void)
{
    // These register names are EXAMPLES - check actual RW612 definitions
    // RW612 may use USBPHY or USB_ANALOG peripheral

    // Power on USB PHY
    USB_ANALOG->PWD &= ~USB_ANALOG_PWD_RXPWDRX_MASK;
    USB_ANALOG->PWD &= ~USB_ANALOG_PWD_TXPWDFS_MASK;

    // Enable USB PHY
    USB_ANALOG->CTRL &= ~USB_ANALOG_CTRL_SFTRST_MASK;
    USB_ANALOG->CTRL &= ~USB_ANALOG_CTRL_CLKGATE_MASK;

    // Configure USB transceiver
    // ... additional PHY configuration ...
}
```

**Key APIs to look for**:
- `USB_DeviceClockInit()`
- `USB_DevicePhyInit()`
- `BOARD_USB0_Init()` or similar
- Register accesses to `USBPHY` or `USB_ANALOG`

---

## **Step 3: Modify TinyUSB Board Files**

### **File 1: `hw/bsp/rw612/boards/frdm_rw612/board.h`**

Add USB clock initialization function:

```c
// Add after board_uart_init_clock() function (around line 55)

// USB PHY clock initialization
static inline void board_usb_phy_init_clock(void) {
  // TODO: Add USB clock configuration from NXP SDK
  // Example (adapt to actual RW612 API):
  // CLOCK_AttachClk(kXTAL_to_USB_CLK);
  // CLOCK_SetClkDiv(kCLOCK_DivUsbClk, 1U);
}
```

### **File 2: `hw/bsp/rw612/family.c`**

Replace the USB initialization in `board_init()`:

**Current code** (lines 86-91):
```c
  // USB Controller Initialization for RW612
  // USB clock is configured by BOARD_BootClockRUN()
  // Just enable the clock gate and reset the peripheral
  CLOCK_EnableClock(kCLOCK_Usb);
  RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);
```

**Replace with**:
```c
  // USB Controller and PHY Initialization for RW612
  board_usb_phy_init_clock();  // Initialize USB clocks
  CLOCK_EnableClock(kCLOCK_Usb);
  RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);

  // TODO: Add USB PHY initialization from NXP SDK
  // Example (adapt to actual RW612 registers):
  // USB_ANALOG->PWD = 0;  // Power on USB PHY
  // USB_ANALOG->CTRL |= USB_ANALOG_CTRL_ENAUTOCLR_MASK;  // Enable auto-clear
  // Additional PHY configuration as per NXP SDK...
```

---

## **Step 4: Alternative - Use NXP SDK USB Init Function Directly**

If NXP SDK provides a ready-to-use USB init function:

### **Option A: Link NXP SDK USB Driver**

1. Copy NXP SDK USB initialization files to `hw/bsp/rw612/`
2. Add to `family.cmake` or `family.mk`:
   ```cmake
   target_sources(${BOARD_TARGET} PRIVATE
     ${CMAKE_CURRENT_LIST_DIR}/usb_device_dci.c  # NXP USB driver
     ${CMAKE_CURRENT_LIST_DIR}/usb_phy_init.c    # USB PHY init
   )
   ```

3. Call NXP init function in `family.c`:
   ```c
   #include "usb_device_config.h"  // From NXP SDK

   void board_init(void) {
     // ... existing init ...

     // USB initialization from NXP SDK
     USB_DeviceClockInit();
     USB_DevicePhyInit();
   }
   ```

---

## **Step 5: Minimal Manual Fix (If SDK Not Available)**

If you can't access NXP SDK, here's a minimal template based on common NXP USB PHY patterns:

**Add to `hw/bsp/rw612/family.c`:**

```c
#include "fsl_device_registers.h"

// USB PHY initialization (adapt register names for RW612)
static void init_usb_phy_rw612(void) {
  // Note: These are EXAMPLE register names - verify against RW612 reference manual!

  // Option 1: If RW612 has USB_ANALOG peripheral (like iMXRT)
  #ifdef USB_ANALOG
    // Power on USB PHY
    USB_ANALOG->PWD = 0;  // Clear all power-down bits

    // Enable USB PHY
    USB_ANALOG->CTRL |= (1 << 30);  // ENAUTOCLR_PHY_PWD
    USB_ANALOG->CTRL &= ~((1 << 31) | (1 << 30));  // Clear SFTRST and CLKGATE
  #endif

  // Option 2: If RW612 has USBPHY peripheral (like some Kinetis)
  #ifdef USBPHY
    // Power on USB PHY
    USBPHY->PWD = 0;

    // Enable LS/FS support
    USBPHY->CTRL_SET = USBPHY_CTRL_ENUTMILEVEL2_MASK | USBPHY_CTRL_ENUTMILEVEL3_MASK;
  #endif

  // Option 3: If USB PHY is integrated and needs minimal setup
  // May only need clock enable (already done above)
}

void board_init(void) {
  BOARD_InitPins();
  BOARD_BootClockRUN();

#if CFG_TUSB_OS == OPT_OS_NONE
  SysTick_Config(SystemCoreClock / 1000);
#elif CFG_TUSB_OS == OPT_OS_FREERTOS
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
  board_uart_init_clock();
  usart_config_t uart_config;
  USART_GetDefaultConfig(&uart_config);
  uart_config.baudRate_Bps = CFG_BOARD_UART_BAUDRATE;
  uart_config.enableTx = true;
  uart_config.enableRx = true;
  USART_Init(UART_DEV, &uart_config, CLOCK_GetFlexCommClkFreq(UART_FLEXCOMM_INST));
#endif

  // USB Controller and PHY Initialization
  CLOCK_EnableClock(kCLOCK_Usb);
  RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);

  // Initialize USB PHY
  init_usb_phy_rw612();
}
```

---

## **Step 6: Check USB Pin Mux (If Needed)**

RW612 likely has **internal USB D+/D- routing**, but verify:

1. Check RW612 schematic for USB pins
2. If USB pins are exposed on package, add to `pin_mux.c`:

```c
void BOARD_InitPins(void) {
  // ... existing pins ...

  // USB pins (if external)
  // IO_MUX_SetPinMux(IO_MUX_USB_DP);  // USB D+
  // IO_MUX_SetPinMux(IO_MUX_USB_DM);  // USB D-
}
```

Most likely, USB is internal and doesn't need pin mux.

---

## **Step 7: Build and Test**

### **Build**:
```bash
cd examples/device/net_lwip_webserver
make BOARD=frdm_rw612 clean
make BOARD=frdm_rw612 -j8
```

### **Flash**:
```bash
make BOARD=frdm_rw612 flash-jlink
```

### **Test**:
1. Plug USB cable into Windows 10 PC
2. Check **Device Manager** → "Ports & Devices"
3. Should see:
   - **Best case**: "USB Ethernet/RNDIS Gadget" under Network Adapters
   - **Progress**: "Unknown USB Device" (descriptor issue, but PHY works!)
   - **Fail**: Nothing appears (USB PHY still not working)

### **Debug with Serial Console**:
```bash
# Monitor debug output
picocom /dev/ttyACM0 -b 115200
```

Look for:
```
USB NCM network interface initialized
```

---

## **Step 8: Further Debugging**

If device still not detected:

### **Check USB with Oscilloscope/Logic Analyzer**:
- **USB D+ should have 1.5kΩ pull-up to 3.3V** (Full-Speed device)
- **D+ voltage**: Should be ~3.3V when connected
- **D- voltage**: Should be ~0V when connected
- **On reset**: Host drives both D+/D- low for 10ms

### **Check USB Registers**:
Add debug output in `family.c`:

```c
void board_init(void) {
  // ... USB init ...

  // Debug: Check USB controller registers
  printf("USB USBCMD: 0x%08lX\n", USBOTG->USBCMD);
  printf("USB PORTSC1: 0x%08lX\n", USBOTG->PORTSC1);
  printf("USB USBSTS: 0x%08lX\n", USBOTG->USBSTS);
}
```

Expected values:
- `PORTSC1` should show PHY enabled and connected
- `USBSTS` should show USB reset after connecting

---

## **Common USB PHY Registers (Reference)**

### **Power-Down Register (PWD)**
```c
USB_ANALOG->PWD = 0;  // Power on everything
// or selectively:
USB_ANALOG->PWD &= ~(USB_ANALOG_PWD_RXPWDRX_MASK |
                      USB_ANALOG_PWD_RXPWDFS_MASK |
                      USB_ANALOG_PWD_TXPWDFS_MASK |
                      USB_ANALOG_PWD_TXPWDV2I_MASK);
```

### **Control Register (CTRL)**
```c
// Enable auto-clear of PWD bits
USB_ANALOG->CTRL |= USB_ANALOG_CTRL_ENAUTOCLR_MASK;

// Take PHY out of reset
USB_ANALOG->CTRL &= ~USB_ANALOG_CTRL_SFTRST_MASK;

// Enable PHY clock
USB_ANALOG->CTRL &= ~USB_ANALOG_CTRL_CLKGATE_MASK;
```

### **TX Register** (Optional - for signal quality)
```c
// D+/D- calibration (typical values)
USB_ANALOG->TX = (USB_ANALOG->TX & ~USB_ANALOG_TX_D_CAL_MASK) |
                  USB_ANALOG_TX_D_CAL(0x0C);
```

---

## **Quick Reference: Where to Find NXP Examples**

### **MCUXpresso SDK**:
1. **Download from**: https://mcuxpresso.nxp.com/
2. **Board**: FRDM-RW612
3. **Examples to check**:
   - `usb_examples/usb_device_cdc_vcom`
   - `usb_examples/usb_device_hid_mouse`
   - `demo_apps/hello_world` (may have USB init)

### **Files to examine**:
- `boards/frdmrw612/usb_examples/*/board.c`
- `boards/frdmrw612/usb_examples/*/usb_device_config.c`
- `middleware/usb/device/usb_device_dci.c`
- `devices/RW612/drivers/fsl_clock.h` (for clock APIs)

### **Search for**:
- `USB_DeviceClockInit`
- `USB_DevicePhyInit`
- `USBPHY->` or `USB_ANALOG->`
- `kCLOCK_Usb` or `kUSB_`

---

## **Expected Result After Fix**

✅ **Windows 10 should**:
- Detect "USB Ethernet/RNDIS Gadget" automatically
- Auto-install WINNCM driver (for NCM)
- Show network adapter in Device Manager

✅ **You should be able to**:
```cmd
ping 192.168.7.1
curl http://192.168.7.1
```

---

## **Summary Checklist**

- [ ] Download NXP RW612 SDK
- [ ] Find USB PHY init code in SDK examples
- [ ] Add `board_usb_phy_init_clock()` to `board.h`
- [ ] Add USB PHY initialization to `family.c board_init()`
- [ ] Build and test
- [ ] Verify USB enumeration on Windows 10
- [ ] Test network connectivity (ping/curl)

---

**Need Help?**
- **RW612 Reference Manual**: Check USB PHY chapter for register details
- **NXP Community**: https://community.nxp.com/
- **TinyUSB Discord**: https://discord.gg/tinyusb

**Analysis Document**: See `RW612_USB_PHY_ISSUE_ANALYSIS.md` for detailed root cause analysis.

---

**Last Updated**: 2025-11-16
**Issue**: RW612 USB not detected on Windows 10
**Status**: Fix implementation required (USB PHY init missing)
