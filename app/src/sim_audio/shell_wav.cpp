#include "adc_sink.h"
#include "audio_pipeline.h"
#include "constants.h"
#include "sine_source.h"
#include "wav_source.h"

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/shell/shell.h>

#include <cstdlib>

namespace {
constexpr uint8_t adc_channel_id = 0;
}

static const device *g_adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc0));

static AdcSink g_sink(g_adc_dev, adc_channel_id);
static AudioPipeline g_pipe(g_sink);
static WavSource g_wav;
static SineSource g_sine;

static int cmd_wav_load(const shell *sh, size_t, char **argv) {
  const int rc = g_wav.load(argv[1]);
  if (rc == 0) {
    shell_print(sh, "loaded: rate=%u Hz, samples=%u", g_wav.sample_rate_hz(),
                (unsigned)g_wav.count_samples());
  } else {
    shell_error(sh, "load failed: %d", rc);
  }
  return rc;
}

static int cmd_wav_start(const shell *sh, size_t, char **) {
  if (!g_wav.loaded()) {
    shell_error(sh, "no wav loaded");
    return sim_audio::err_inval;
  }
  const int rc = g_pipe.start(g_wav);
  if (rc == 0)
    shell_print(sh, "started wav (loop)");
  else
    shell_error(sh, "start failed: %d", rc);
  return rc;
}

static int cmd_wav_sine(const shell *sh, size_t argc, char **argv) {
  // wav sine [freq_hz] [amp_norm 0..1] [rate_hz]
  uint32_t freq_hz = sim_audio::default_sine_freq_hz;
  float amp_norm = sim_audio::default_sine_amp;
  uint32_t rate_hz = sim_audio::default_gen_rate_hz;

  if (argc >= 3)
    freq_hz = (uint32_t)strtoul(argv[1], nullptr, 10);
  if (argc >= 4)
    amp_norm = strtof(argv[2], nullptr);
  if (argc >= 5)
    rate_hz = (uint32_t)strtoul(argv[3], nullptr, 10);

  if (rate_hz == 0u || freq_hz == 0u || freq_hz > (rate_hz / 2u)) {
    shell_error(sh, "freq must be 1..%u (Nyquist)", rate_hz / 2u);
    return sim_audio::err_inval;
  }
  if (!(amp_norm >= 0.0f && amp_norm <= 1.0f)) {
    shell_error(sh, "amp must be 0.0..1.0");
    return sim_audio::err_inval;
  }

  g_sine.configure(freq_hz, amp_norm, rate_hz);
  const int rc = g_pipe.start(g_sine);
  if (rc == 0) {
    shell_print(sh, "started sine: %u Hz amp=%.3f rate=%u Hz", freq_hz,
                (double)amp_norm, rate_hz);
  } else {
    shell_error(sh, "start failed: %d", rc);
  }
  return rc;
}

static int cmd_wav_stop(const shell *sh, size_t, char **) {
  g_pipe.stop();
  shell_print(sh, "stopped");
  return 0;
}

static int cmd_wav_info(const shell *sh, size_t, char **) {
  shell_print(sh, "pipeline running=%d", g_pipe.running() ? 1 : 0);
  const auto *src = g_pipe.source();
  shell_print(sh, "source=%s",
              (src == &g_wav) ? "wav" : ((src == &g_sine) ? "sine" : "none"));

  shell_print(sh, "wav: loaded=%d rate=%u Hz samples=%u pos=%u",
              g_wav.loaded() ? 1 : 0, g_wav.sample_rate_hz(),
              (unsigned)g_wav.count_samples(), (unsigned)g_wav.pos_samples());

  shell_print(sh, "sine: freq=%u Hz amp=%.3f rate=%u Hz", g_sine.freq_hz(),
              (double)g_sine.amp_norm(), g_sine.sample_rate_hz());

  return 0;
}

static int cmd_adc_read(const shell *sh, size_t, char **) {
  if (!device_is_ready(g_adc_dev)) {
    shell_error(sh, "adc0 not ready");
    return sim_audio::err_nodev;
  }

  adc_channel_cfg ch_cfg{};
  ch_cfg.gain = ADC_GAIN_1;
  ch_cfg.reference = ADC_REF_INTERNAL;
  ch_cfg.acquisition_time = ADC_ACQ_TIME_DEFAULT;
  ch_cfg.channel_id = adc_channel_id;

  int16_t sample_raw = 0;
  adc_sequence seq{};
  seq.channels = BIT(adc_channel_id);
  seq.buffer = &sample_raw;
  seq.buffer_size = sizeof(sample_raw);
  seq.resolution = 12;

  int rc = adc_channel_setup(g_adc_dev, &ch_cfg);
  if (rc) {
    shell_error(sh, "adc_channel_setup: %d", rc);
    return rc;
  }

  rc = adc_read(g_adc_dev, &seq);
  if (rc) {
    shell_error(sh, "adc_read: %d", rc);
    return rc;
  }

  shell_print(sh, "adc raw=%d (range %u..%u)", sample_raw,
              sim_audio::adc_raw_min, sim_audio::adc_raw_max_12bit);
  return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    wav_cmds,
    SHELL_CMD_ARG(load, NULL, "wav load <path.wav>", cmd_wav_load, 2, 0),
    SHELL_CMD(start, NULL, "wav start", cmd_wav_start),
    SHELL_CMD_ARG(sine, NULL, "wav sine [freq_hz] [amp 0..1] [rate_hz]",
                  cmd_wav_sine, 1, 3),
    SHELL_CMD(stop, NULL, "wav stop", cmd_wav_stop),
    SHELL_CMD(info, NULL, "wav info", cmd_wav_info), SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(wav, &wav_cmds, "WAV/Sine control", NULL);
SHELL_CMD_REGISTER(adc_read, NULL, "adc_read", cmd_adc_read);
