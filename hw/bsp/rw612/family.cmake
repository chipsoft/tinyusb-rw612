include_guard()

# Use SDK from parent project (frdmrw612_freertos_hello root)
# TOP points to app_libs/tinyusb, so ${TOP}/../.. is the repo root
set(PROJECT_ROOT ${TOP}/../..)
set(SDK_DRIVERS ${PROJECT_ROOT}/drivers)
set(SDK_DEVICE ${PROJECT_ROOT}/device)
set(SDK_CMSIS ${PROJECT_ROOT}/CMSIS)
set(SDK_STARTUP ${PROJECT_ROOT}/startup)

# include board specific
include(${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}/board.cmake)

# toolchain set up
set(CMAKE_SYSTEM_CPU cortex-m33 CACHE INTERNAL "System Processor")
set(FAMILY_MCUS RW612 CACHE INTERNAL "")

set(CMAKE_TOOLCHAIN_FILE ${TOP}/examples/build_system/cmake/toolchain/arm_${TOOLCHAIN}.cmake)

#------------------------------------
# Startup & Linker script
#------------------------------------
if (NOT DEFINED LD_FILE_GNU)
  set(LD_FILE_GNU ${TOP}/hw/bsp/rw612/boards/${BOARD}/build_main.ld)
endif ()
set(LD_FILE_Clang ${LD_FILE_GNU})
if (NOT DEFINED STARTUP_FILE_GNU)
  set(STARTUP_FILE_GNU ${SDK_STARTUP}/startup_rw612.c)
endif()
set(STARTUP_FILE_Clang ${STARTUP_FILE_GNU})

#------------------------------------
# Board Target
#------------------------------------
function(family_add_board BOARD_TARGET)
  add_library(${BOARD_TARGET} STATIC
    # driver
    ${SDK_DRIVERS}/fsl_gpio.c
    ${SDK_DRIVERS}/fsl_common.c
    ${SDK_DRIVERS}/fsl_common_arm.c
    ${SDK_DRIVERS}/fsl_usart.c
    ${SDK_DRIVERS}/fsl_clock.c
    ${SDK_DRIVERS}/fsl_reset.c
    # mcu
    ${SDK_DEVICE}/system_RW612.c
    )
  target_include_directories(${BOARD_TARGET} PUBLIC
    ${SDK_CMSIS}
    ${SDK_DRIVERS}
    ${SDK_DEVICE}
    ${SDK_DEVICE}/periph
    )

  update_board(${BOARD_TARGET})
endfunction()

#------------------------------------
# Functions
#------------------------------------
function(family_configure_example TARGET RTOS)
  family_configure_common(${TARGET} ${RTOS})
  family_add_tinyusb(${TARGET} OPT_MCU_RW612)

  target_sources(${TARGET} PUBLIC
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/family.c
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../board.c
    ${TOP}/src/portable/chipidea/ci_hs/dcd_ci_hs.c
    ${STARTUP_FILE_${CMAKE_C_COMPILER_ID}}
    )
  target_include_directories(${TARGET} PUBLIC
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/boards/${BOARD}
    )

  if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(${TARGET} PUBLIC
      "LINKER:--script=${LD_FILE_GNU}"
      --specs=nosys.specs --specs=nano.specs
      )
  elseif (CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_link_options(${TARGET} PUBLIC
      "LINKER:--script=${LD_FILE_Clang}"
      )
  endif ()

  # Flashing
  family_add_bin_hex(${TARGET})
  family_flash_jlink(${TARGET})
endfunction()
