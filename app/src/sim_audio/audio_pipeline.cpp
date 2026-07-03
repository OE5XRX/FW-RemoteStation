#include "audio_pipeline.h"

#include "constants.h"

#include <etl/delegate.h>

AudioPipeline::AudioPipeline(AdcSink sink) : sink_(sink) {}

int AudioPipeline::start(SampleSource &src) {
  if (!sink_.ready())
    return sim_audio::err_nodev;

  src_ = &src;
  running_ = true;

  clock_.start(src.sample_rate_hz(), etl::delegate<void()>::create<AudioPipeline, &AudioPipeline::on_tick>(*this));
  return 0;
}

void AudioPipeline::stop() {
  clock_.stop();
  running_ = false;
  src_ = nullptr;
  sink_.write_norm(0.0f);
}

void AudioPipeline::on_tick() {
  if (!running_ || src_ == nullptr)
    return;

  const float s_norm = src_->next_sample_norm();
  sink_.write_norm(s_norm);
}
