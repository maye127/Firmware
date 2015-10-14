ROMFS_ROOT ?= $(PX4_BASE)/ROMFS/factory/0/

#
# ADC
#
MODULES += drivers/stm32/adc
MODULES += modules/sensors_switch

#
# Bluetooth21 BT740
#
MODULES += drivers/bluetooth21

#
# FRAM
#
MODULES += systemcmds/mtd

#
# GPS
#
MODULES += drivers/gps

#
# Sensors
#
MODULES += modules/sensors_probe
MODULES += modules/sensor_validation

#
# OTP write modules
#
MODULES += lib/airdog/hwinfo
MODULES += lib/stm32f4
MODULES += systemcmds/flash

#
# Debug / Security dangerous modules
#
MODULES += systemcmds/mem

#
# Generic tools
#
MODULES += modules/i2c_exchange
MODULES += modules/gpio_tool
MODULES += modules/airdog/calibrator

#
# Minimal required module set
#
LIBRARIES += lib/mathlib/CMSIS
MODULES += lib/mathlib
MODULES += lib/conversion
MODULES += drivers/calibration
MODULES += drivers/device
MODULES += drivers/led
MODULES += drivers/stm32
MODULES += drivers/stm32/adc
MODULES += drivers/stm32/tone_alarm
MODULES += modules/systemlib
MODULES += modules/uORB
MODULES += modules/eparam
MODULES += systemcmds/boardinfo
MODULES += systemcmds/nshterm
MODULES += systemcmds/param
MODULES += systemcmds/reboot
MODULES += systemcmds/ver

#
# Logging
#

MODULES	+= modules/quick_log
#
# Transitional support - add commands from the NuttX export archive.
#
# In general, these should move to modules over time.
#
# Each entry here is <command>.<priority>.<stacksize>.<entrypoint> but we use a helper macro
# to make the table a bit more readable.
#
define _B
	$(strip $1).$(or $(strip $2),SCHED_PRIORITY_DEFAULT).$(or $(strip $3),CONFIG_PTHREAD_STACK_DEFAULT).$(strip $4)
endef

#                  command                 priority                   stack  entrypoint
BUILTIN_COMMANDS := \
	$(call _B, sercon,                 ,                          2048,  sercon_main                ) \
	$(call _B, serdis,                 ,                          2048,  serdis_main                )
