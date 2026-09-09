/* Copyright (C) Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/uart.h>
#include <cmsis_core.h>
#include <soc_common.h>
#include <se_service.h>
#include <es0_power_manager.h>

#include "appl_shell.h"
#include "power_mgr.h"
#include "ble_handler.h"

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/pinctrl/balletto-pinctrl.h>

#if !defined(CONFIG_SOC_SERIES_B1)
#error "Application works only with B1 devices"
#endif

static volatile int run_profile_error;

/* Macros */
LOG_MODULE_REGISTER(main, CONFIG_MAIN_LOG_LEVEL);

/**
 * PM Notifier callback for power state entry
 */
static void pm_notify_state_entry(enum pm_state const state)
{
	/* TODO: enable when this is needed */
	/*
	 * const struct pm_state_info *next_state = pm_state_next_get(0);
	 * uint8_t substate_id = next_state ? next_state->substate_id : 0;
	 */

	switch (state) {
	case PM_STATE_SUSPEND_TO_RAM:
	case PM_STATE_SOFT_OFF:
		break;
	default:
		__ASSERT(false, "Entering unknown power state %d", state);
		LOG_ERR("Entering unknown power state %d", state);
		break;
	}
}

/**
 * PM Notifier callback called BEFORE devices are resumed
 *
 * This restores SE run configuration when resuming from S2RAM states.
 * Note: For SOFT_OFF, the system resets completely and app_set_run_params()
 * runs during normal PRE_KERNEL_1 initialization, so this callback is not needed.
 */
static void pm_notify_pre_device_resume(enum pm_state const state)
{
	switch (state) {
	case PM_STATE_SUSPEND_TO_RAM: {
		run_profile_error = app_set_run_params();
		break;
	}
	case PM_STATE_SOFT_OFF: {
		/* No action needed - SOFT_OFF causes reset, not resume */
		break;
	}
	default: {
		__ASSERT(false, "Pre-resume for unknown power state %d", state);
		LOG_ERR("Pre-resume for unknown power state %d", state);
		break;
	}
	}
}

/**
 * PM Notifier structure
 */
static struct pm_notifier app_pm_notifier = {
	.state_entry = pm_notify_state_entry,
	.pre_device_resume = pm_notify_pre_device_resume,
};

/*
 * This function will be invoked in the PRE_KERNEL_1 phase of the init
 * routine to prevent sleep during startup.
 */
static int app_pre_kernel_init(void)
{
	/* Register PM notifier callbacks */
	pm_notifier_register(&app_pm_notifier);

	app_prevent_off();

	return 0;
}
SYS_INIT(app_pre_kernel_init, PRE_KERNEL_1, 39);

#if defined(CONFIG_CORTEX_M_SYSTICK_LPM_TIMER_HOOKS)

static uint32_t idle_timer_pre_idle;

/* Idle timer used for timer while entering the idle state */
static const struct device *idle_timer = DEVICE_DT_GET(DT_CHOSEN(zephyr_cortex_m_idle_timer));
/**
 * To simplify the driver, implement the callout to Counter API
 * as hooks that would be provided by platform drivers if
 * CONFIG_CORTEX_M_SYSTICK_LPM_TIMER_HOOKS was selected instead.
 */
void z_cms_lptim_hook_on_lpm_entry(uint64_t max_lpm_time_us)
{

	/* Store current value of the selected timer to calculate a
	 * difference in measurements after exiting the idle state.
	 */
	counter_get_value(idle_timer, &idle_timer_pre_idle);
	/**
	 * Disable the counter alarm in case it was already running.
	 */
	/* counter_cancel_channel_alarm(idle_timer, 0); */

	/* Set the alarm using timer that runs the idle.
	 * Needed rump-up/setting time, lower accurency etc. should be
	 * included in the exit-latency in the power state definition.
	 */

	struct counter_alarm_cfg cfg = {
		.callback = NULL,
		.ticks = counter_us_to_ticks(idle_timer, max_lpm_time_us) + idle_timer_pre_idle,
		.user_data = NULL,
		.flags = COUNTER_ALARM_CFG_ABSOLUTE,
	};
	counter_set_channel_alarm(idle_timer, 0, &cfg);
}

uint64_t z_cms_lptim_hook_on_lpm_exit(void)
{
	/**
	 * Calculate how much time elapsed according to counter.
	 */
	uint32_t idle_timer_post, idle_timer_diff;

	counter_get_value(idle_timer, &idle_timer_post);

	/**
	 * Check for counter timer overflow
	 * (TODO: this doesn't work for downcounting timers!)
	 */
	if (idle_timer_pre_idle > idle_timer_post) {
		idle_timer_diff = (counter_get_top_value(idle_timer) - idle_timer_pre_idle) +
				  idle_timer_post + 1;
	} else {
		idle_timer_diff = idle_timer_post - idle_timer_pre_idle;
	}

	return (uint64_t)counter_ticks_to_us(idle_timer, idle_timer_diff);
}
#endif /* CONFIG_CORTEX_M_SYSTICK_LPM_TIMER_COUNTER */

/* Pimpelipom */
static void set_profiles(void)
{
    const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    int ret;
    uint32_t baudrate;

	/* Set the default off profile and apply it */
	get_default_off_cfg(&current_offp);
	ret = set_off_profile(PM_STATE_MODE_STOP_1);
	if (ret) {
		printk("set_off_profile failed: %d\n", ret);
		return;
	}

	/* Set the default run profile */
	/* Profile get is already done in power_mgr_init */
	ret = app_set_run_params();
	if (ret) {
		printk("app_set_run_params failed: %d\n", ret);
		return;
	}

	/* reconf UART */
	ret = uart_line_ctrl_get(uart_dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
    if (!ret) {
            ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_BAUD_RATE, baudrate);
    } else {
		printk("Failed to get UART baud rate: %d\n", ret);
		return;
	}

}

#define BOOT_PARAM_ID_PLF_DIAG            0xD4
#define BOOT_PARAM_ID_BLE_DIAG            0xD5
#define BOOT_PARAM_LEN_PLF_DIAG           4
#define BOOT_PARAM_LEN_BLE_DIAG           4
/* Tag, status and length bytes written by write_tlv_int() */
#define BOOT_PARAM_TLV_HDR_LEN            3

const pinctrl_soc_pin_t pinctrl_ble_dbg[] = {
	PIN_P7_0__BLE_DBG24 | 0x10000, /*Receiver enabled*/
	PIN_P7_1__BLE_DBG25 | 0x10000, /*Receiver enabled*/
	PIN_P7_2__BLE_DBG26 | 0x10000,  /*Receiver enabled*/
	PIN_P7_3__BLE_DBG27 | 0x10000,  /*Receiver enabled*/
	PIN_P7_4__BLE_DBG28 | 0x10000,
	PIN_P7_5__BLE_DBG29 | 0x10000,
	PIN_P7_6__BLE_DBG30 | 0x10000,
	PIN_P7_7__BLE_DBG31 | 0x10000};
	// PIN_P7_7__GPIO| 0x10000

#define DEFAULT_TAG_STATUS (0x00 | 0x02 | 0x04)

static uint8_t *write_tlv_int(uint8_t *target, uint8_t tag, uint32_t value, uint8_t len)
{
	*target++ = tag;
	*target++ = DEFAULT_TAG_STATUS;
	*target++ = len;
	if (len == 1) {
		memcpy(target, (uint8_t *)&value, len);
	} else if (len == 2) {
		memcpy(target, (uint16_t *)&value, len);
	} else {
		memcpy(target, &value, len);
	}

	target += len;

	return target;
}

void set_ble_debug(void)
{
	pinctrl_configure_pins(pinctrl_ble_dbg, ARRAY_SIZE(pinctrl_ble_dbg),
				       PINCTRL_REG_NONE);
}

uint16_t add_extra_nvds_param_length(void)
{
	printk("add_extra_nvds_param_length\n");

#if 0
	return 0;
#else
	return (BOOT_PARAM_TLV_HDR_LEN + BOOT_PARAM_LEN_PLF_DIAG) +
	       (BOOT_PARAM_TLV_HDR_LEN + BOOT_PARAM_LEN_BLE_DIAG);
#endif
}

uint8_t *add_extra_nvds_params(uint8_t *target)
{
	printk("add_extra_nvds_params\n");
#if 0
	return target;
#else
	target = write_tlv_int(target, BOOT_PARAM_ID_PLF_DIAG, 0x90838383 /* plf_diag_configs, 0x90838383 -> HCI Uart out to P7_0 - P7_7 */,
			    BOOT_PARAM_LEN_PLF_DIAG);
	target = write_tlv_int(target, BOOT_PARAM_ID_BLE_DIAG, 0x83838383 /* ble_diag_configs, 0x83838383 */,
			    BOOT_PARAM_LEN_BLE_DIAG);

	return target;
#endif
}

int main(void)
{
	int ret = 0;

	printk("BLE testapp cold start\n");

	// set_profiles();

	set_ble_debug();
	appl_wait_to_continue();


	ret = ble_init();
	if (ret) {
		printk("ble_init: %d\n", ret);
		return ret;
	}

	ret = ble_start();

	if (ret) {
		printk("ble_start: %d\n", ret);
		return ret;
	}

	printk("Init complete!\n");

	while (1) {
		if (ble_is_connected()) {
			k_sleep(K_MSEC(ble_rtc_connected_wakeup));
		} else {
			k_sleep(K_MSEC(ble_rtc_wakeup));
		}
	}
	return 0;
}
