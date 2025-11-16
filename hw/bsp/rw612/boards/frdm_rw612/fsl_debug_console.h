/*
 * TinyUSB RW612 BSP - Debug Console Stub
 * Minimal stub to satisfy fsl_power.c dependency
 */

#ifndef FSL_DEBUG_CONSOLE_H_
#define FSL_DEBUG_CONSOLE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Stub functions - not implemented for TinyUSB examples */
#define PRINTF(...) ((void)0)
#define PUTCHAR(c) ((void)0)
#define SCANF(...) ((void)0)
#define GETCHAR() ((int)0)

static inline int DbgConsole_Init(void) { return 0; }
static inline int DbgConsole_Deinit(void) { return 0; }

#ifdef __cplusplus
}
#endif

#endif /* FSL_DEBUG_CONSOLE_H_ */
