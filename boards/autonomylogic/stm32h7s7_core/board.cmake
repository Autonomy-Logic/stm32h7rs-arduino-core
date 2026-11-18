# SPDX-License-Identifier: Apache-2.0

board_runner_args(probe_rs "--chip=STM32H7S7A8Ix")
board_runner_args(probe_rs "--connect-under-reset")
board_runner_args(pyocd "--target=stm32h7s7a8ixh")
board_runner_args(pyocd "--flash-opt=-O reset_type=hw")
board_runner_args(pyocd "--flash-opt=-O connect_mode=under-reset")

include(${ZEPHYR_BASE}/boards/common/probe-rs.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
