/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Copyright (C) 2026 Alif Semiconductor.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/pdm/pdm_alif.h>
#include <zephyr/audio/codec.h>

#define RX_NODE        DT_ALIAS(pdm_audio)
#define I2S_TX_NODE    DT_ALIAS(i2s_tx)
#define CODEC_CFG_NODE DT_ALIAS(audio_codec)

#define AUDIO_L      PDM_AUDIO_CHANNEL_2
#define AUDIO_R      PDM_AUDIO_CHANNEL_3
#define AUDIO_L_2    PDM_AUDIO_CHANNEL_4
#define AUDIO_R_2    PDM_AUDIO_CHANNEL_5
#define AUDIO_L_3    PDM_AUDIO_CHANNEL_0
#define AUDIO_R_3    PDM_AUDIO_CHANNEL_1
//#define PDM_CHANNELS       ((1 << AUDIO_L) | (1 << AUDIO_R) | (1 << AUDIO_L_2) | (1 << AUDIO_R_2) | (1 << AUDIO_L_3) | (1 << AUDIO_R_3))
#define PDM_CHANNELS ((1 << AUDIO_L_2) | (1 << AUDIO_R_2) | (1 << AUDIO_L) | (1 << AUDIO_R))
//#define PDM_CHANNELS ((1 << AUDIO_L_3) | (1 << AUDIO_R_3))

#define NUMBER_OF_CHANNELS 4

#define I2S_CHANNEL_CNT 2

#define SAMPLE_FREQUENCY 16000
#define SAMPLE_BIT_WIDTH 16
#define BYTES_PER_SAMPLE sizeof(int16_t)

#define SAMPLES_PER_BLOCK ((SAMPLE_FREQUENCY / 50))
#define INITIAL_BLOCKS    NUMBER_OF_CHANNELS
#define TIMEOUT           1000

#define BLOCK_SIZE  (BYTES_PER_SAMPLE * SAMPLES_PER_BLOCK)
#define BLOCK_COUNT (INITIAL_BLOCKS * 2)
K_MEM_SLAB_DEFINE_STATIC(mem_slab, (BLOCK_SIZE * (NUMBER_OF_CHANNELS / 2)), BLOCK_COUNT, 4);
K_MEM_SLAB_DEFINE_STATIC(i2s_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

uint32_t fir[PDM_MAX_FIR_COEFFICIENT] = {0x00000001, 0x00000003, 0x00000003, 0x000007F4, 0x00000004,
					 0x000007ED, 0x000007F5, 0x000007F4, 0x000007D3, 0x000007FE,
					 0x000007BC, 0x000007E5, 0x000007D9, 0x00000793, 0x00000029,
					 0x0000072C, 0x00000072, 0x000002FD};

#define PHASE           0x0000001F
#define GAIN            0x00000200
#define PEAK_DETECT_TH  0x00060002
#define PEAK_DETECT_ITV 0x0004002D
#define IIR_COEF        0x00000004
#define PDM_MODE        PDM_MODE_HIGH_QUALITY_1024_CLK_FRQ

void set_config(struct dmic_cfg *cfg, struct pcm_stream_cfg *stream)
{
	uint32_t channel_map = 0;

	cfg->streams = stream;
	cfg->streams[0].mem_slab = &mem_slab;
	cfg->channel.req_num_streams = 1;
	cfg->channel.req_num_chan = NUMBER_OF_CHANNELS;
	cfg->streams[0].block_size = BLOCK_SIZE * (NUMBER_OF_CHANNELS / 2);

	channel_map = PDM_CHANNELS;

	cfg->channel.req_chan_map_lo = channel_map;
}

void pdm_ch_config(const struct device *pcmj_device)
{
	struct pdm_ch_config pdm_coef_reg;

	pdm_set_ch_phase(pcmj_device, AUDIO_L, PHASE);
	pdm_set_ch_gain(pcmj_device, AUDIO_L, GAIN);
	pdm_coef_reg.ch_num = AUDIO_L;
	memcpy(pdm_coef_reg.ch_fir_coef, fir, sizeof(pdm_coef_reg.ch_fir_coef));
	pdm_coef_reg.ch_iir_coef = IIR_COEF;
	pdm_channel_config(pcmj_device, &pdm_coef_reg);

	pdm_set_ch_gain(pcmj_device, AUDIO_R, GAIN);
	pdm_set_ch_phase(pcmj_device, AUDIO_R, PHASE);
	pdm_coef_reg.ch_num = AUDIO_R;
	memcpy(pdm_coef_reg.ch_fir_coef, fir, sizeof(pdm_coef_reg.ch_fir_coef));
	pdm_coef_reg.ch_iir_coef = IIR_COEF;
	pdm_channel_config(pcmj_device, &pdm_coef_reg);

	pdm_set_ch_phase(pcmj_device, AUDIO_L_2, PHASE);
	pdm_set_ch_gain(pcmj_device, AUDIO_L_2, GAIN);
	pdm_coef_reg.ch_num = AUDIO_L_2;
	memcpy(pdm_coef_reg.ch_fir_coef, fir, sizeof(pdm_coef_reg.ch_fir_coef));
	pdm_coef_reg.ch_iir_coef = IIR_COEF;
	pdm_channel_config(pcmj_device, &pdm_coef_reg);

	pdm_set_ch_gain(pcmj_device, AUDIO_R_2, GAIN);
	pdm_set_ch_phase(pcmj_device, AUDIO_R_2, PHASE);
	pdm_coef_reg.ch_num = AUDIO_R_2;
	memcpy(pdm_coef_reg.ch_fir_coef, fir, sizeof(pdm_coef_reg.ch_fir_coef));
	pdm_coef_reg.ch_iir_coef = IIR_COEF;
	pdm_channel_config(pcmj_device, &pdm_coef_reg);

	pdm_set_ch_phase(pcmj_device, AUDIO_L_3, PHASE);
	pdm_set_ch_gain(pcmj_device, AUDIO_L_3, GAIN);
	pdm_coef_reg.ch_num = AUDIO_L_3;
	memcpy(pdm_coef_reg.ch_fir_coef, fir, sizeof(pdm_coef_reg.ch_fir_coef));
	pdm_coef_reg.ch_iir_coef = IIR_COEF;
	pdm_channel_config(pcmj_device, &pdm_coef_reg);

	pdm_set_ch_gain(pcmj_device, AUDIO_R_3, GAIN);
	pdm_set_ch_phase(pcmj_device, AUDIO_R_3, PHASE);
	pdm_coef_reg.ch_num = AUDIO_R_3;
	memcpy(pdm_coef_reg.ch_fir_coef, fir, sizeof(pdm_coef_reg.ch_fir_coef));
	pdm_coef_reg.ch_iir_coef = IIR_COEF;
	pdm_channel_config(pcmj_device, &pdm_coef_reg);

	pdm_mode(pcmj_device, PDM_MODE);
}

static bool configure_streams(const struct device *pdm_mic, const struct device *i2s_dev_tx)
{
	struct i2s_config config;
	struct dmic_cfg cfg;
	struct pcm_stream_cfg stream;

	config.word_size = SAMPLE_BIT_WIDTH;
	config.channels = I2S_CHANNEL_CNT;
	config.format = I2S_FMT_DATA_FORMAT_I2S;
	config.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER;
	config.frame_clk_freq = SAMPLE_FREQUENCY;
	// config.mem_slab = &mem_slab;
	config.mem_slab = &i2s_slab;
	config.block_size = BLOCK_SIZE;
	config.timeout = TIMEOUT;

	int ret = i2s_configure(i2s_dev_tx, I2S_DIR_TX, &config);
	if (ret < 0) {
		printk("Failed to configure TX stream: %d\n", ret);
		return false;
	}

	set_config(&cfg, &stream);
	dmic_configure(pdm_mic, &cfg);
	pdm_ch_config(pdm_mic);

	return true;
}

static bool prepare_transfer(const struct device *i2s_dev_tx)
{
	int ret;

	for (int i = 0; i < INITIAL_BLOCKS; ++i) {
		void *mem_block;

		ret = k_mem_slab_alloc(&i2s_slab, &mem_block, K_NO_WAIT);

		if (ret < 0) {
			printk("Failed to allocate TX block %d: %d\n", i, ret);
			return false;
		}

		memset(mem_block, 0, BLOCK_SIZE);

		ret = i2s_write(i2s_dev_tx, mem_block, BLOCK_SIZE);

		if (ret < 0) {
			printk("Failed to write block %d: %d\n", i, ret);
			return false;
		}
	}

	return true;
}

static bool trigger_command(const struct device *dev_rx, const struct device *i2s_dev_tx,
			    enum i2s_trigger_cmd cmd)
{
	int ret = dmic_trigger(dev_rx,
			       (cmd == I2S_TRIGGER_START) ? DMIC_TRIGGER_START : DMIC_TRIGGER_STOP);
	if (ret < 0) {
		printk("Failed to trigger command %d on RX: %d\n", cmd, ret);
		return false;
	}

	ret = i2s_trigger(i2s_dev_tx, I2S_DIR_TX, cmd);
	if (ret < 0) {
		printk("Failed to trigger command %d on TX: %d\n", cmd, ret);
		return false;
	}

	return true;
}

int main(void)
{
	const struct device *const dev_rx = DEVICE_DT_GET(RX_NODE);
	const struct device *const i2s_dev_tx = DEVICE_DT_GET(I2S_TX_NODE);
	const struct device *const codec_dev = DEVICE_DT_GET(CODEC_CFG_NODE);

	printk("PDM echo sample\n");

	if (!device_is_ready(dev_rx)) {
		printk("%s is not ready\n", dev_rx->name);
		return 0;
	}

	if (!device_is_ready(i2s_dev_tx)) {
		printk("%s is not ready\n", i2s_dev_tx->name);
		return 0;
	}

	if (!device_is_ready(codec_dev)) {
		printk("%s is not ready\n", codec_dev->name);
		return 0;
	}

	if (!configure_streams(dev_rx, i2s_dev_tx)) {
		return 0;
	}

	if (!prepare_transfer(i2s_dev_tx)) {
		return 0;
	}

	audio_codec_start_output(codec_dev);

	if (!trigger_command(dev_rx, i2s_dev_tx, I2S_TRIGGER_START)) {
		return 0;
	}

	printk("Streams started\n");

	while (1) {
		void *mem_block;

		uint32_t block_size;
		void *i2s_block;
		int ret;
		int i, x = 0;
		uint32_t y = 0;

		ret = dmic_read(dev_rx, 0, &mem_block, &block_size, 5000);

		if (ret < 0) {
			printk("Failed to read data: %d\n", ret);
			break;
		}

		ret = k_mem_slab_alloc(&i2s_slab, &i2s_block, K_NO_WAIT);
		if (ret) {
			printk("Failed to allocate i2s slab, err %d\n", ret);
			return ret;
		}

		int16_t *src = mem_block;
		int16_t *dst = i2s_block;
		size_t total_input_samples = block_size / sizeof(uint16_t);
		size_t frames = total_input_samples / NUMBER_OF_CHANNELS;


		if (NUMBER_OF_CHANNELS == 6) {
			for (size_t i = 0; i < frames; i++) {
				dst[0] = (src[0] + src[2] + src[4]) / 3;
				dst[1] = (src[1] + src[3] + src[5]) / 3;
				src += 6;
				dst += 2; // stereo output
			}
		} else if (NUMBER_OF_CHANNELS == 4) {
			for (size_t i = 0; i < frames; i++) {
				dst[0] = (src[0] + src[2]) / 2;
				dst[1] = (src[1] + src[3]) / 2;
				src += 4;
				dst += 2; // stereo output
			}
		} else if (NUMBER_OF_CHANNELS == 2) {
			for (size_t i = 0; i < frames; i++) {
				dst[0] = src[0];
				dst[1] = src[1];
				src += 2;
				dst += 2; // stereo output
			}
		}

		k_mem_slab_free(&mem_slab, mem_block);

		ret = i2s_write(i2s_dev_tx, i2s_block, BLOCK_SIZE);
		if (ret < 0) {
			printk("Failed to write data: %d\n", ret);
			break;
		}
	}

	dmic_trigger(dev_rx, DMIC_TRIGGER_STOP);
}
