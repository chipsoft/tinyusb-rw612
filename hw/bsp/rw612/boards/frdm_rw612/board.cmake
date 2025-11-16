set(MCU_VARIANT RW612)
set(MCU_CORE RW612_cm33)

set(JLINK_DEVICE RW612_M33)
set(PYOCD_TARGET RW612)
set(NXPLINK_DEVICE RW612:RW612)

set(PORT 0)

function(update_board TARGET)
  target_compile_definitions(${TARGET} PUBLIC
    CPU_RW612ETA2I
    BOARD_TUD_RHPORT=${PORT}
    # RW612 has USB High Speed controller
    BOARD_TUD_MAX_SPEED=OPT_MODE_HIGH_SPEED
    )
  target_sources(${TARGET} PUBLIC
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/clock_config.c
    ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/pin_mux.c
    )
endfunction()
