#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/pdm/pdm_alif.h>

LOG_MODULE_REGISTER(test, LOG_LEVEL_DBG);

uint32_t fir[18] = {0x00000001, 0x00000003, 0x00000003, 0x000007F4, 0x00000004, 0x000007ED,
		    0x000007F5, 0x000007F4, 0x000007D3, 0x000007FE, 0x000007BC, 0x000007E5,
		    0x000007D9, 0x00000793, 0x00000029, 0x0000072C, 0x00000072, 0x000002FD};

#define AUDIO_L       PDM_AUDIO_CHANNEL_4
#define AUDIO_R       PDM_AUDIO_CHANNEL_5
#define AUDIO_L_2     PDM_AUDIO_CHANNEL_0
#define AUDIO_R_2     PDM_AUDIO_CHANNEL_1
#define PDM_CHANNELS  ((1 << AUDIO_L) | (1 << AUDIO_R) | (1 << AUDIO_L_2) | (1 << AUDIO_R_2))
#define CHANNEL_COUNT 4

#define BLOCK_SIZE_BYTES (512 * CHANNEL_COUNT)
#define BLOCK_COUNT      2

#define TIMEOUT  250
#define PDM_MODE PDM_MODE_HIGH_QUALITY_1024_CLK_FRQ

#define PHASE           0x0000001F
#define GAIN            0x00000600
#define PEAK_DETECT_TH  0x00060002
#define PEAK_DETECT_ITV 0x0004002D
#define IIR_COEF        0x00000004

K_MEM_SLAB_DEFINE_STATIC(pcm_slab, BLOCK_SIZE_BYTES, BLOCK_COUNT, 4);

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

	pdm_set_ch_gain(pcmj_device, AUDIO_L_2, GAIN);
	pdm_set_ch_phase(pcmj_device, AUDIO_L_2, PHASE);
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

	pdm_mode(pcmj_device, PDM_MODE);
}

int main(void)
{
	printk("Start test\n");

	const struct device *dmic = DEVICE_DT_GET(DT_NODELABEL(lppdm));
	if (!device_is_ready(dmic)) {
		LOG_ERR("DMIC/PDM device not ready");
		return 0;
	}

	struct dmic_cfg cfg = {0};
	cfg.channel.req_num_chan = CHANNEL_COUNT;
	cfg.channel.req_chan_map_lo = PDM_CHANNELS;

	static struct pcm_stream_cfg stream_cfg;
	stream_cfg.block_size = BLOCK_SIZE_BYTES;
	stream_cfg.mem_slab = &pcm_slab;
	cfg.streams = &stream_cfg;

	int ret = dmic_configure(dmic, &cfg);
	if (ret) {
		LOG_ERR("dmic_configure failed: %d", ret);
		return 0;
	}
	pdm_ch_config(dmic);

	ret = dmic_trigger(dmic, DMIC_TRIGGER_START);
	if (ret) {
		LOG_ERR("DMIC_TRIGGER_START failed: %d", ret);
		return 0;
	}

	while (1) {
		void *buffer = NULL;
		size_t size = 0;

		ret = dmic_read(dmic, 0, &buffer, &size, TIMEOUT);

		if (ret) {
			LOG_ERR("Data read fail %d", ret);
			break;
		}

		const int16_t *sample_ptr = (const int16_t *)buffer;
		size_t sample_cnt = size / sizeof(int16_t);

		int16_t max_a = 0;
		int16_t max_b = 0;
		int16_t max_c = 0;
		int16_t max_d = 0;

		for (size_t i = 0; i + 1 < sample_cnt; i += 4) {
			if (sample_ptr[i] > max_a) {
				max_a = sample_ptr[i];
			}
			if (sample_ptr[i + 1] > max_b) {
				max_b = sample_ptr[i + 1];
			}
			if (sample_ptr[i + 2] > max_c) {
				max_c = sample_ptr[i + 2];
			}
			if (sample_ptr[i + 3] > max_d) {
				max_d = sample_ptr[i + 3];
			}
		}

		LOG_INF("peak%5d peak%5d peak%5d peak%5d", max_a, max_b, max_c, max_d);
		k_mem_slab_free(&pcm_slab, buffer);
	}

	(void)dmic_trigger(dmic, DMIC_TRIGGER_STOP);

	return 0;
}
