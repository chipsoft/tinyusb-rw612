UF2_FAMILY_ID = 0x2abc77ec
JLINK_DEVICE = RW612
JLINK_IF = swd

# Use SDK from parent project (frdmrw612_freertos_hello root)
# TOP points to app_libs/tinyusb, so $(TOP)/../.. is the repo root
PROJECT_ROOT = $(TOP)/../..
SDK_DRIVERS = $(PROJECT_ROOT)/drivers
SDK_DEVICE = $(PROJECT_ROOT)/device
SDK_CMSIS = $(PROJECT_ROOT)/CMSIS
SDK_STARTUP = $(PROJECT_ROOT)/startup
SDK_UTILITIES = $(PROJECT_ROOT)/utilities/debug_console_lite
SDK_UTILITIES_STR = $(PROJECT_ROOT)/utilities/str
SDK_COMPONENT = $(PROJECT_ROOT)/component/uart
SDK_COMPONENT_ELS = $(PROJECT_ROOT)/component/els_pkc

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
	$(SDK_DRIVERS)/fsl_clock.c \
	$(SDK_DRIVERS)/fsl_reset.c \
	$(SDK_DRIVERS)/fsl_gpio.c \
	$(SDK_DRIVERS)/fsl_usart.c \
	$(SDK_DRIVERS)/fsl_common.c \
	$(SDK_DRIVERS)/fsl_common_arm.c \
	$(SDK_DRIVERS)/fsl_power.c \
	$(SDK_DRIVERS)/fsl_flexcomm.c \
	$(SDK_DRIVERS)/fsl_ocotp.c \
	$(PROJECT_ROOT)/flash_config/flash_config.c

# ELS/PKC crypto library (for GDET and voltage calibration)
SRC_C += \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxClEls/src/mcuxClEls_Common.c \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxClEls/src/mcuxClEls_GlitchDetector.c

INC += \
	$(TOP)/$(BOARD_PATH) \
	$(SDK_CMSIS) \
	$(TOP)/hw/bsp/rw612 \
	$(PROJECT_ROOT) \
	$(SDK_DEVICE) \
	$(SDK_DEVICE)/periph \
	$(SDK_DRIVERS) \
	$(SDK_UTILITIES) \
	$(SDK_UTILITIES_STR) \
	$(SDK_COMPONENT) \
	$(PROJECT_ROOT)/flash_config \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxClBuffer/inc \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxClBuffer/inc/internal \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxClCore/inc \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxClEls/inc \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxClEls/inc/internal \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxClMemory/inc \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxClMemory/inc/internal \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslMemory/inc \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslMemory/inc/internal \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslCPreProcessor/inc \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslDataIntegrity/inc \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslFlowProtection/inc \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslParamIntegrity/inc \
	$(SDK_COMPONENT_ELS)/src/comps/mcuxCsslSecureCounter/inc \
	$(SDK_COMPONENT_ELS)/src/platforms/rw61x \
	$(SDK_COMPONENT_ELS)/src/platforms/rw61x/inc \
	$(SDK_COMPONENT_ELS)/includes/platform/rw61x

# Startup file (C-based startup for RW612)
SRC_C += $(SDK_STARTUP)/startup_rw612.c
