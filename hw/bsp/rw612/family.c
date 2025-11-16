/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2018, hathach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

/* metadata:
   manufacturer: NXP
*/

#include "bsp/board_api.h"
#include "fsl_device_registers.h"
#include "fsl_gpio.h"
#include "fsl_usart.h"
#include "board.h"

#include "pin_mux.h"
#include "clock_config.h"

//--------------------------------------------------------------------+
// Forward USB interrupt events to TinyUSB IRQ Handler
//--------------------------------------------------------------------+

void USB_IRQHandler(void) {
  tusb_int_handler(0, true);
}

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

  //------------- USB Controller and PHY Initialization (based on MCX MCXN9) -------------//

  // Step 1: Enable USB controller clock
  CLOCK_EnableClock(kCLOCK_Usb);

  // Step 2: Reset USB controller
  RESET_PeripheralReset(kUSB_RST_SHIFT_RSTn);

  // Step 3: Initialize USB PHY (based on hw/bsp/mcx/family.c:168-184)
  #ifdef USBPHY
    // Enable USB PHY clock if separate from controller clock
    #ifdef kCLOCK_UsbPhy
      CLOCK_EnableClock(kCLOCK_UsbPhy);
    #endif

    // Override trim values (if needed - similar to MCX)
    #if !defined(FSL_FEATURE_SOC_CCM_ANALOG_COUNT) && !defined(FSL_FEATURE_SOC_ANATOP_COUNT)
      USBPHY->TRIM_OVERRIDE_EN = 0x001fU;  /* override IFR value */
    #endif

    // Enable PHY support for Low-speed device + LS via FS Hub
    USBPHY->CTRL |= USBPHY_CTRL_SET_ENUTMILEVEL2_MASK | USBPHY_CTRL_SET_ENUTMILEVEL3_MASK;

    // Enable all power for normal operation - CRITICAL!
    USBPHY->PWD = 0;

    // TX Timing calibration (using MCX values as reference)
    uint32_t phytx = USBPHY->TX;
    phytx &= ~(USBPHY_TX_D_CAL_MASK | USBPHY_TX_TXCAL45DM_MASK | USBPHY_TX_TXCAL45DP_MASK);
    phytx |= USBPHY_TX_D_CAL(0x04) | USBPHY_TX_TXCAL45DP(0x07) | USBPHY_TX_TXCAL45DM(0x07);
    USBPHY->TX = phytx;
  #else
    // USBPHY peripheral not found in device headers
    // This is expected if USB PHY is integrated in USBOTG controller or not exposed
    // USB may still work if PHY is auto-initialized by hardware
    #warning "USBPHY peripheral not found - USB PHY initialization skipped"
  #endif
}

//--------------------------------------------------------------------+
// Board porting API
//--------------------------------------------------------------------+

void board_led_write(bool state) {
  GPIO_PinWrite(LED_GPIO, 0, LED_PIN, state ? LED_STATE_ON : (1 - LED_STATE_ON));
}

uint32_t board_button_read(void) {
#ifdef BUTTON_GPIO
  return BUTTON_STATE_ACTIVE == GPIO_PinRead(BUTTON_GPIO, 0, BUTTON_PIN);
#else
  return 0;
#endif
}

int board_uart_read(uint8_t* buf, int len) {
  (void) buf;
  (void) len;
  return 0;
}

int board_uart_write(void const* buf, int len) {
#ifdef UART_DEV
  USART_WriteBlocking(UART_DEV, (uint8_t const*) buf, len);
  return len;
#else
  (void) buf; (void) len;
  return 0;
#endif
}

#if CFG_TUSB_OS == OPT_OS_NONE
volatile uint32_t system_ticks = 0;

void SysTick_Handler(void) {
  system_ticks++;
}

uint32_t board_millis(void) {
  return system_ticks;
}
#endif
