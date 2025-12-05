/* Copyright (c) 2019 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * Based on ST7789V sample:
 * Copyright (c) 2019 Marc Reilly
 *
 * Copyright 2024 Alif Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(disp, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/cdc200.h>
#ifdef CONFIG_MIPI_DSI
#include <zephyr/drivers/mipi_dsi/dsi_dw.h>
#endif /* CONFIG_MIPI_DSI */
#include "alif_logo.h"

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/drivers/counter.h>
#include <cmsis_core.h>
#include <soc.h>
#include <se_service.h>
#include <power_mgr.h>

#define RED_ARGB8888	0x00ff0000
#define GREEN_ARGB8888	0x0000ff00
#define BLUE_ARGB8888	0x000000ff
#define RED_RGB888	0x00ff0000
#define GREEN_RGB888	0x0000ff00
#define BLUE_RGB888	0x000000ff
#define RED_RGB565	0xf800
#define GREEN_RGB565	0x07e0
#define BLUE_RGB565	0x001f

#define CDC200_PIXEL_SIZE_ARGB8888	4
#define CDC200_PIXEL_SIZE_RGB888	3
#define CDC200_PIXEL_SIZE_RGB565	2

#define SOC_STOP_MODE_PD PD_VBAT_AON_MASK
#define DEEP_SLEEP_IN_USEC (10 * 1000 * 1000)

/**
 * By default STOP mode is requested.
 * For Standby, set the SOC_REQUESTED_POWER_MODE to SOC_STANDBY_MODE_PD
 */
#define SOC_REQUESTED_POWER_MODE SOC_STOP_MODE_PD

#if defined(CONFIG_SOC_SERIES_ENSEMBLE_E1C) || defined(CONFIG_SOC_SERIES_BALLETTO_B1)
	#define APP_RET_MEM_BLOCKS SRAM4_1_MASK | SRAM4_2_MASK | SRAM4_3_MASK | SRAM4_4_MASK | \
					SRAM5_1_MASK | SRAM5_2_MASK | SRAM5_3_MASK | SRAM5_4_MASK |\
					SRAM5_5_MASK
	#define SERAM_MEMORY_BLOCKS_IN_USE SERAM_1_MASK | SERAM_2_MASK | SERAM_3_MASK | SERAM_4_MASK
#else
	#define APP_RET_MEM_BLOCKS SRAM4_1_MASK | SRAM4_2_MASK | SRAM5_1_MASK | SRAM5_2_MASK
	#define SERAM_MEMORY_BLOCKS_IN_USE SERAM_MASK
#endif

#if DT_NODE_HAS_COMPAT_STATUS(DT_NODELABEL(rtc0), snps_dw_apb_rtc, okay)
	#define WAKEUP_SOURCE DT_NODELABEL(rtc0)
	#define SE_OFFP_EWIC_CFG EWIC_RTC_A
	#define SE_OFFP_WAKEUP_EVENTS WE_LPRTC
#elif DT_NODE_HAS_COMPAT_STATUS(DT_NODELABEL(timer0), snps_dw_timers, okay)
	#define WAKEUP_SOURCE DT_NODELABEL(timer0)
	#define SE_OFFP_EWIC_CFG EWIC_VBAT_TIMER
	#define SE_OFFP_WAKEUP_EVENTS WE_LPTIMER0
#else
#error "Wakeup Device not enabled in the dts"
#endif

enum corner {
	TOP_LEFT,
	TOP_RIGHT,
	BOTTOM_RIGHT,
	BOTTOM_LEFT
};

typedef void (*fill_buffer)(enum corner corner, uint8_t grey, uint8_t *buf,
			    size_t buf_size);

/**
 * Use the HFOSC clock for the UART console
 */
#if DT_SAME_NODE(DT_NODELABEL(uart4), DT_CHOSEN(zephyr_console))
#define CONSOLE_UART_NUM 4
#elif DT_SAME_NODE(DT_NODELABEL(uart2), DT_CHOSEN(zephyr_console))
#define CONSOLE_UART_NUM 2
#else
#error "Specify the uart console number"
#endif

#define UART_CTRL_CLK_SEL_POS 8

static int app_set_run_params(void)
{
	run_profile_t runp;
	int ret;
#if 0
	ret = se_service_sync();
	if (ret) {
		printk("SE: not responding to service calls %d\n", ret);
		return 0;
	}

	ret = se_service_get_run_cfg(&runp);
	if (ret) {
		printk("SE: get_run_cfg failed = %d.\n", ret);
		return 0;
	}

	runp.power_domains = PD_SYST_MASK | PD_SSE700_AON_MASK;
	runp.dcdc_voltage  = 825;
	runp.dcdc_mode     = DCDC_MODE_PWM;
	runp.aon_clk_src   = CLK_SRC_LFXO;
	runp.run_clk_src   = CLK_SRC_PLL;

	runp.cpu_clk_freq  = CLOCK_FREQUENCY_160MHZ;
	if (SCB->VTOR) {
		runp.memory_blocks |= MRAM_MASK;
	}

	ret = se_service_set_run_cfg(&runp);
	if (ret) {
		printk("SE: set_run_cfg failed = %d.\n", ret);
		return 0;
	}
#ifdef EARLY_BOOT_SYSTOP_ON
	app_restore_host_systop();
#endif
#endif
	return 0;
}

static int app_set_off_params(void)
{
	return power_mgr_set_offprofile(PM_STATE_MODE_STOP);
#if 0

	int ret;
	off_profile_t offp;
	printk("XXXXXXX SET OFF PARAM\n");
	ret = se_service_get_off_cfg(&offp);
	if (ret) {
		printk("SE: get_off_cfg failed = %d.\n", ret);
		printk("ERROR: Can't establish SE connection, app exiting..\n");
		return ret;
	}

	offp.power_domains = SOC_REQUESTED_POWER_MODE;
	offp.aon_clk_src   = CLK_SRC_LFXO;
	offp.stby_clk_src  = CLK_SRC_HFXO;
	offp.ewic_cfg      = SE_OFFP_EWIC_CFG;
	offp.wakeup_events = SE_OFFP_WAKEUP_EVENTS;
	offp.vtor_address  = SCB->VTOR;
	offp.memory_blocks = MRAM_MASK;

	/*
	 * Enable the HE TCM retention only if the VTOR is present.
	 * This is just for this test application.
	 */
	if (!SCB->VTOR) {
		offp.memory_blocks = APP_RET_MEM_BLOCKS | SERAM_MEMORY_BLOCKS_IN_USE;
	} else {
		offp.memory_blocks |= SERAM_MEMORY_BLOCKS_IN_USE;
	}


	printk("SE: VTOR = %x\n", offp.vtor_address);
	printk("SE: MEMBLOCKS = %x\n", offp.memory_blocks);

	ret = se_service_set_off_cfg(&offp);
	if (ret) {
		printk("SE: set_off_cfg failed = %d.\n", ret);
		printk("ERROR: Can't establish SE connection, app exiting..\n");
		return ret;
	}

	return 0;
#endif
}

static void pm_notify_state_entry(enum pm_state state)
{
	printk("pm_notify_state_entry\n");
	const struct pm_state_info *next_state = pm_state_next_get(0);
	uint8_t substate_id = next_state ? next_state->substate_id : 0;
	int ret;

	switch (state) {
	case PM_STATE_SUSPEND_TO_RAM:
	case PM_STATE_SOFT_OFF:
		ret = app_set_off_params();
		__ASSERT(ret == 0, "app_set_off_params failed = %d", ret);
		LOG_ERR("app_set_off_params failed = %d", ret);
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
static void pm_notify_pre_device_resume(enum pm_state state)
{
	int ret;
	printk("pm_notify_pre_device_resume\n");
	switch (state) {
	case PM_STATE_SUSPEND_TO_RAM:
		ret = app_set_run_params();
		__ASSERT(ret == 0, "app_set_run_params failed = %d", ret);
		LOG_ERR("app_set_run_params failed = %d", ret);
		break;
	case PM_STATE_SOFT_OFF:
		/* No action needed - SOFT_OFF causes reset, not resume */
		break;
	default:
		__ASSERT(false, "Pre-resume for unknown power state %d", state);
		LOG_ERR("Pre-resume for unknown power state %d", state);
		break;
	}
}

/**
 * PM Notifier structure
 */
static struct pm_notifier app_pm_notifier = {
	.state_entry = pm_notify_state_entry,
	.state_exit = pm_notify_pre_device_resume,
};


/*
 * This function will be invoked in the PRE_KERNEL_2 phase of the init routine.
 */
static int app_pre_kernel_init(void)
{
	/* Register PM notifier callbacks */
	pm_notifier_register(&app_pm_notifier);

	return 0;
}
SYS_INIT(app_pre_kernel_init, PRE_KERNEL_2, 0);

static int app_pre_console_init(void)
{
	/* Enable HFOSC in CGU */
	sys_set_bits(CGU_CLK_ENA, BIT(23));

	/* Enable HFOSC for the UART console */
	sys_clear_bits(EXPSLV_UART_CTRL, BIT((CONSOLE_UART_NUM + UART_CTRL_CLK_SEL_POS)));

	return 0;
}
SYS_INIT(app_pre_console_init, PRE_KERNEL_1, 50);
SYS_INIT(app_set_run_params, PRE_KERNEL_1, 46);




#if (!defined(CONFIG_MIPI_DSI) || \
	!DT_NODE_HAS_PROP(DT_ALIAS(mipi_dsi), dpi_video_pattern_gen))
static void fill_buffer_argb8888(enum corner corner, uint8_t grey, uint8_t *buf,
				 size_t buf_size)
{
	uint32_t color = 0;

	switch (corner) {
	case TOP_LEFT:
		color = RED_ARGB8888;
		break;
	case TOP_RIGHT:
		color = GREEN_ARGB8888;
		break;
	case BOTTOM_RIGHT:
		color = BLUE_ARGB8888;
		break;
	case BOTTOM_LEFT:
		color = grey << 16 | grey << 8 | grey;
		break;
	}

	for (size_t idx = 0; idx < buf_size; idx += 4) {
		*((uint32_t *)(buf + idx)) = color;
	}
}

static void fill_buffer_rgb888(enum corner corner, uint8_t grey, uint8_t *buf,
			       size_t buf_size)
{
	uint32_t color = 0;

	switch (corner) {
	case TOP_LEFT:
		color = RED_RGB888;
		break;
	case TOP_RIGHT:
		color = GREEN_RGB888;
		break;
	case BOTTOM_RIGHT:
		color = BLUE_RGB888;
		break;
	case BOTTOM_LEFT:
		color = grey << 16 | grey << 8 | grey;
		break;
	}

	for (size_t idx = 0; idx < buf_size; idx += 3) {
		*(buf + idx + 2) = (color >> 16) & 0xff;
		*(buf + idx + 1) = (color >> 8) & 0xff;
		*(buf + idx + 0) = (color >> 0) & 0xff;
	}
}

static uint16_t get_rgb565_color(enum corner corner, uint8_t grey)
{
	uint16_t color = 0;

	switch (corner) {
	case TOP_LEFT:
		color = RED_RGB565;
		break;
	case TOP_RIGHT:
		color = GREEN_RGB565;
		break;
	case BOTTOM_RIGHT:
		color = BLUE_RGB565;
		break;
	case BOTTOM_LEFT:
		color = (grey & 0x1f) << 11 |
			(grey & 0x3f) << 5 | (grey & 0x1F);
		break;
	}
	return color;
}

static void fill_buffer_rgb565(enum corner corner, uint8_t grey, uint8_t *buf,
			       size_t buf_size)
{
	uint16_t color = get_rgb565_color(corner, grey);

	for (size_t idx = 0; idx < buf_size; idx += 2) {
		*(buf + idx + 1) = (color >> 8) & 0xFFu;
		*(buf + idx + 0) = (color >> 0) & 0xFFu;
	}
}

int get_pixel_size(enum display_pixel_format fmt)
{
	if (fmt == PIXEL_FORMAT_RGB_888)
		return CDC200_PIXEL_SIZE_RGB888;
	else if (fmt == PIXEL_FORMAT_ARGB_8888)
		return CDC200_PIXEL_SIZE_ARGB8888;
	else if (fmt == PIXEL_FORMAT_RGB_565)
		return CDC200_PIXEL_SIZE_RGB565;
	else
		return 0;
}
#endif /* (!defined(CONFIG_MIPI_DSI) || \
	* !DT_NODE_HAS_PROP(DT_ALIAS(mipi_dsi), dpi_video_pattern_gen))
	*/

static volatile uint32_t alarm_cb_status;
static void alarm_callback_fn(const struct device *wakeup_dev,
				uint8_t chan_id, uint32_t ticks,
				void *user_data)
{
	printk("%s: !!! Alarm !!!\n", wakeup_dev->name);
	alarm_cb_status = 1;
}

static int app_enter_normal_sleep(uint32_t sleep_usec)
{
#if defined(CONFIG_CORTEX_M_SYSTICK_IDLE_TIMER)
	k_sleep(K_USEC(sleep_usec));
#else
	const struct device *const wakeup_dev = DEVICE_DT_GET(WAKEUP_SOURCE);
	struct counter_alarm_cfg alarm_cfg;
	int ret;

	alarm_cfg.flags = 0;
	alarm_cfg.ticks = counter_us_to_ticks(wakeup_dev, sleep_usec);
	alarm_cfg.callback = alarm_callback_fn;
	alarm_cfg.user_data = &alarm_cfg;

	ret = counter_set_channel_alarm(wakeup_dev, 0, &alarm_cfg);
	if (ret) {
		printk("Couldnt set the alarm\n");
		return ret;
	}
	printk("Set alarm for %u microseconds\n", sleep_usec);

	k_sleep(K_USEC(sleep_usec));

	if (!alarm_cb_status) {
		return -1;
	}
	alarm_cb_status = 0;


#endif
	return 0;
}

static int app_enter_deep_sleep(uint32_t sleep_usec)
{
#if defined(CONFIG_CORTEX_M_SYSTICK_IDLE_TIMER)
	/**
	 * Set a delay more than the min-residency-us configured so that
	 * the sub-system will go to OFF state.
	 */
	k_sleep(K_USEC(sleep_usec));
#else
	const struct device *const wakeup_dev = DEVICE_DT_GET(WAKEUP_SOURCE);
	struct counter_alarm_cfg alarm_cfg;
	int ret;
	/*
	 * Set the alarm and delay so that idle thread can run
	 */
	alarm_cfg.ticks = counter_us_to_ticks(wakeup_dev, sleep_usec);
	ret = counter_set_channel_alarm(wakeup_dev, 0, &alarm_cfg);
	if (ret) {
		printk("Failed to set the alarm (err %d)", ret);
		return ret;
	}

	printk("Set alarm for %u microseconds\n\n", sleep_usec);

	if (ret) {
		printk("Couldnt set the alarm\n");
		return ret;
	}

	sys_poweroff();

#endif

	return 0;
}

/* Thread stack & control block */
#define MY_TASK_STACK_SIZE 2048
#define MY_TASK_PRIORITY   5

K_THREAD_STACK_DEFINE(my_task_stack, MY_TASK_STACK_SIZE);
static struct k_thread my_task_thread;

void my_task(void)
{
    while (1) {
        /* -------- Work phase (5 seconds) -------- */
        printk("Work phase start\n");
	int64_t start = k_uptime_get();

	while (k_uptime_get() - start < 5000) {
            /* Do your work here */
            k_msleep(1);
        }
        LOG_INF("Work phase end");

        /* -------- Sleep phase (5 seconds) -------- */
        LOG_INF("Sleeping 5 seconds");
        k_msleep(5000);
    }
}

/* Create the thread */
// K_THREAD_DEFINE(my_task_id, 2048, my_task, NULL, NULL, NULL,
//                 5, 0, 0);

int main(void)
{

	printk("%x\n", power_mgr_get_wakeup_reason());

#if (!defined(CONFIG_MIPI_DSI) || \
	!DT_NODE_HAS_PROP(DT_ALIAS(mipi_dsi), dpi_video_pattern_gen))
	struct display_buffer_descriptor buf_desc;
	struct cdc200_display_caps capabilities;
	struct cdc200_fb_desc fb_l2 = { 0 };
	fill_buffer fill_buffer_fnc = NULL;
	const struct device *display_dev;
	size_t pixel_size = 0;
	size_t buf_size = 0;
	uint8_t grey_count;
	size_t rect_w = 2;
	size_t rect_h = 1;
	uint8_t *buf;
	size_t scale;
	size_t x;
	size_t y;
	int ret;
	int calc = 0;
#endif /* (!defined(CONFIG_MIPI_DSI) || \
	* !DT_NODE_HAS_PROP(DT_ALIAS(mipi_dsi), dpi_video_pattern_gen))
	*/

#if defined(CONFIG_MIPI_DSI)
	struct display_capabilities panel_caps;
	const struct device *panel;
	const struct device *dsi;


	panel = DEVICE_DT_GET(DT_ALIAS(panel));
	if (!device_is_ready(panel)) {
		LOG_ERR("Device %s not found. Aborting sample.",
			panel->name);
		return -1;
	}

	dsi = DEVICE_DT_GET(DT_ALIAS(mipi_dsi));
	if (!device_is_ready(dsi)) {
		LOG_ERR("Device %s not found. Aborting sample.",
			dsi->name);
		return -1;
	}

	LOG_INF("Rotating the display by 180 degrees");
	ret = display_set_orientation(panel, DISPLAY_ORIENTATION_ROTATED_180);
	if (ret == -ENOTSUP)
		LOG_INF("Un-supported Display Rotation.");

	LOG_INF("Enable Ensemble-DSI Device video mode.");
	ret = dsi_dw_set_mode(dsi, DSI_DW_VIDEO_MODE);
	if (ret) {
		LOG_ERR("DSI Host controller set to video mode.");
		return -1;
	}

	display_get_capabilities(panel, &panel_caps);
	LOG_INF("Panel Orientation - %d", panel_caps.current_orientation);

	display_blanking_off(panel);
#endif /* defined(CONFIG_MIPI_DSI) */

#if (!defined(CONFIG_MIPI_DSI) || \
	!DT_NODE_HAS_PROP(DT_ALIAS(mipi_dsi), dpi_video_pattern_gen))

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found. Aborting sample.",
			display_dev->name);
		return -1;
	}
	ret = app_set_off_params();
	if (ret) {
		printk("ERROR: app exiting..\n");
		return 0;
	}

	LOG_INF("Display sample for %s", display_dev->name);
	LOG_INF("Enabling CDC200 Device.");
	cdc200_set_enable(display_dev, true);
	cdc200_get_capabilities(display_dev, &capabilities);

	LOG_INF("Display Capabilities");
	LOG_INF("Panel resolution, supported formats - (%d, %d), %d",
			capabilities.x_panel_resolution,
			capabilities.y_panel_resolution,
			capabilities.supported_pixel_formats);
	LOG_INF("CDC200 orientation - %d",
			capabilities.current_orientation);

	for (int i = 0; i <= 1; i++) {
		LOG_INF("Display Capabilities layer %d:", i + 1);
		LOG_INF("\tlayer_enabled - %d",
				capabilities.layer[i].layer_en);
		LOG_INF("\t(x_res, y_res) - (%d, %d)",
				capabilities.layer[i].x_resolution,
				capabilities.layer[i].y_resolution);
		LOG_INF("\tcurr_pix_fmt - %d",
				capabilities.layer[i].current_pixel_format);
	}

	scale = (capabilities.layer[0].x_resolution / 8);
	rect_w *= scale;
	rect_h *= scale;
	buf_size = rect_w * rect_h;

	if (buf_size < (capabilities.layer[0].x_resolution)) {
		buf_size = capabilities.layer[0].x_resolution;
	}

	pixel_size =
		MAX(get_pixel_size(capabilities.layer[0].current_pixel_format),
		get_pixel_size(capabilities.layer[1].current_pixel_format));
	buf_size *= pixel_size;

	switch (capabilities.layer[0].current_pixel_format) {
	case PIXEL_FORMAT_ARGB_8888:
		fill_buffer_fnc = fill_buffer_argb8888;
		break;
	case PIXEL_FORMAT_RGB_888:
		fill_buffer_fnc = fill_buffer_rgb888;
		break;
	case PIXEL_FORMAT_RGB_565:
		fill_buffer_fnc = fill_buffer_rgb565;
		break;
	default:
		LOG_ERR("Unsupported pixel format. Aborting sample.");
		return -1;
	}

	buf = k_malloc(buf_size);
	if (buf == NULL) {
		LOG_ERR("Could not allocate memory."
			"Aborting sample. Required Heap Size - %d", buf_size);
		return -1;
	}

	if (capabilities.layer[1].layer_en) {
		cdc200_get_framebuffer(display_dev, 1, &fb_l2);
		memset((uint8_t *) fb_l2.fb_addr, 0, fb_l2.fb_size);
		memcpy((uint8_t *) fb_l2.fb_addr, logo, sizeof(logo));
	}

	if (capabilities.layer[0].layer_en) {
		cdc200_get_framebuffer(display_dev, 0, &fb_l2);
		LOG_INF("FB0 - 0x%08x, size - %d",
		(uint32_t)fb_l2.fb_addr, fb_l2.fb_size);
		(void)memset(buf, 0xFFu, buf_size);

		buf_desc.buf_size = buf_size;
		buf_desc.pitch = capabilities.layer[0].x_resolution;
		buf_desc.width = capabilities.layer[0].x_resolution;
		buf_desc.height = 1;

		for (int idx = 0;
		idx < capabilities.layer[0].y_resolution; idx += 1) {
			cdc200_display_write(display_dev, 0, 0,
					idx, &buf_desc, buf);
		}

		buf_desc.pitch = rect_w;
		buf_desc.width = rect_w;
		buf_desc.height = rect_h;

		fill_buffer_fnc(TOP_LEFT, 0, buf, buf_size);
		x = 0;
		y = 0;
		cdc200_display_write(display_dev, 0, x, y, &buf_desc, buf);

		fill_buffer_fnc(TOP_RIGHT, 0, buf, buf_size);
		x = capabilities.layer[0].x_resolution - rect_w;
		y = 0;
		cdc200_display_write(display_dev, 0, x, y, &buf_desc, buf);

		fill_buffer_fnc(BOTTOM_RIGHT, 0, buf, buf_size);
		x = capabilities.layer[0].x_resolution - rect_w;
		y = capabilities.layer[0].y_resolution - rect_h;
		cdc200_display_write(display_dev, 0, x, y, &buf_desc, buf);

		display_blanking_off(display_dev);

		grey_count = 0;
		x = 0;
		y = capabilities.layer[0].y_resolution - rect_h;



		while (1) {

			fill_buffer_fnc(BOTTOM_LEFT, grey_count,
					buf, buf_size);
			cdc200_display_write(display_dev, 0, x,
					     y, &buf_desc, buf);
			++grey_count;
			k_msleep(100);

			if(calc++ > 50) {
				printk("Sleep time\n");

				cdc200_set_enable(display_dev, false);
				power_mgr_ready_for_sleep();
				ret = app_enter_deep_sleep(DEEP_SLEEP_IN_USEC);
				calc = 0;
				if (ret) {
					printk("ERROR: app exiting..\n");
					return 0;
				}
			}
		}
	}
#endif /* (!defined(CONFIG_MIPI_DSI) || \
	* !DT_NODE_HAS_PROP(DT_ALIAS(mipi_dsi), dpi_video_pattern_gen))
	*/
	return 0;
}
