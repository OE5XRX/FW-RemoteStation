#ifndef SIM_AUDIO_AUDIO_PIPELINE_H_
#define SIM_AUDIO_AUDIO_PIPELINE_H_

#include "adc_sink.h"
#include "sample_clock.h"
#include "sample_source.h"

class AudioPipeline {
public:
  explicit AudioPipeline(AdcSink sink);

  int start(SampleSource &src);
  void stop();

  bool running() const { return running_; }
  const SampleSource *source() const { return src_; }

private:
  static void on_tick(void *user);

  AdcSink sink_;
  SampleClock clock_;
  SampleSource *src_{nullptr};
  bool running_{false};
};

#endif // SIM_AUDIO_AUDIO_PIPELINE_H_
