UF2_FAMILY_ID = 0x2abc77ec
JLINK_DEVICE = RW612
JLINK_IF = swd

# Use SDK from hw/mcu/nxp/mcux-sdk and lib/CMSIS_5
SDK_DIR = $(TOP)/hw/mcu/nxp/mcux-sdk
MCU_DIR = $(SDK_DIR)/devices/RW612
SDK_DRIVERS = $(SDK_DIR)/drivers
SDK_DEVICE = $(MCU_DIR)
SDK_CMSIS = $(TOP)/lib/CMSIS_5/CMSIS/Core/Include
SDK_STARTUP = $(MCU_DIR)
SDK_UTILITIES = $(SDK_DIR)/components/uart
SDK_UTILITIES_STR = $(SDK_DIR)/utilities/str
SDK_COMPONENT = $(SDK_DIR)/components/uart
SDK_COMPONENT_ELS = $(SDK_DIR)/components/els_pkc

include $(TOP)/$(BOARD_PATH)/board.mk

# RW612 only has USB High Speed (port 0)
PORT ?= 0

CFLAGS += \
  -flto \
  -DBOARD_TUD_RHPORT=$(PORT) \
  -DBOARD_TUD_MAX_SPEED=OPT_MODE_HIGH_SPEED \
  -DBOOT_HEADER_ENABLE=1 \

# mcu driver and startup cause following warnings
CFLAGS += -Wno-error=unused-parameter -Wno-error=old-style-declaration -Wno-error=strict-prototypes -Wno-error=cast-qual

LDFLAGS_GCC += -specs=nosys.specs -specs=nano.specs -L$(TOP)/hw/bsp/rw612/boards/frdm_rw612

# Linker script from project (in BSP board directory)
LD_FILE ?= hw/bsp/rw612/boards/frdm_rw612/build_main.ld

# TinyUSB: RW612 uses ChipIdea HS controller
SRC_C += src/portable/chipidea/ci_hs/dcd_ci_hs.c

# RW612 SDK drivers  
SRC_C += \
	$(SDK_DEVICE)/system_RW612.c \
	$(SDK_DEVICE)/drivers/fsl_clock.c \
	$(SDK_DEVICE)/drivers/fsl_power.c \
	$(SDK_DEVICE)/drivers/fsl_reset.c \
	$(SDK_DEVICE)/drivers/fsl_ocotp.c \
	$(SDK_DRIVERS)/lpc_gpio/fsl_gpio.c \
	$(SDK_DRIVERS)/flexcomm/fsl_flexcomm.c \
	$(SDK_DRIVERS)/flexcomm/usart/fsl_usart.c \
	$(SDK_DRIVERS)/common/fsl_common.c \
	$(SDK_DRIVERS)/common/fsl_common_arm.c

# Note: ELS/PKC crypto library disabled - missing platform headers
# SRC_C += \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxClEls/src/mcuxClEls_Common.c \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxClEls/src/mcuxClEls_GlitchDetector.c

INC += \
	$(TOP)/$(BOARD_PATH) \
	$(SDK_CMSIS) \
	$(TOP)/hw/bsp/rw612 \
	$(SDK_DEVICE) \
	$(SDK_DEVICE)/drivers \
	$(SDK_DRIVERS)/lpc_gpio \
	$(SDK_DRIVERS)/flexcomm \
	$(SDK_DRIVERS)/flexcomm/usart \
	$(SDK_DRIVERS)/common \
	$(SDK_DRIVERS)/flexspi \
	$(SDK_DRIVERS)/cache/cache64 \
	$(SDK_UTILITIES) \
	$(SDK_UTILITIES_STR) \
	$(SDK_COMPONENT)

# Note: ELS/PKC include paths disabled - missing platform headers
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxClBuffer/inc \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxClBuffer/inc/internal \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxClCore/inc \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxClEls/inc \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxClEls/inc/internal \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxClMemory/inc \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxClMemory/inc/internal \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslMemory/inc \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslMemory/inc/internal \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslCPreProcessor/inc \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslDataIntegrity/inc \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslFlowProtection/inc \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslParamIntegrity/inc \
#	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslSecureCounter/inc \
#	$(SDK_COMPONENT_ELS)/src/platforms/rw61x \
#	$(SDK_COMPONENT_ELS)/src/platforms/rw61x/inc \
#	$(SDK_COMPONENT_ELS)/includes/platform/rw61x

# Startup file (C-based startup for RW612)
SRC_C += $(SDK_STARTUP)/mcuxpresso/startup_rw612.c
