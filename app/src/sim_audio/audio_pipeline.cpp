#include "audio_pipeline.h"
#include "constants.h"

AudioPipeline::AudioPipeline(AdcSink sink) : sink_(sink) {}

int AudioPipeline::start(SampleSource &src) {
  if (!sink_.ready())
    return sim_audio::err_nodev;

  src_ = &src;
  running_ = true;

  clock_.start(src.sample_rate_hz(), &AudioPipeline::on_tick, this);
  return 0;
}

void AudioPipeline::stop() {
  clock_.stop();
  running_ = false;
  src_ = nullptr;
  sink_.write_norm(0.0f);
}

void AudioPipeline::on_tick(void *user) {
  auto *self = static_cast<AudioPipeline *>(user);
  if (!self || !self->running_ || !self->src_)
    return;

  const float s_norm = self->src_->next_sample_norm();
  self->sink_.write_norm(s_norm);
}
