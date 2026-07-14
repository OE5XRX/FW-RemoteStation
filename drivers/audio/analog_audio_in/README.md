# analog-audio-in — hardware-timed analog audio capture (STM32)

A reusable Zephyr driver that samples an analog audio input at a fixed rate using a
**hardware timer trigger + circular DMA**, delivering batches of 16-bit PCM to a
consumer callback. It exists because Zephyr's generic ADC/DAC drivers cannot do
hardware-clocked continuous ADC streaming (the STM32 ADC driver is software/kernel-timer
paced with one-shot DMA), which is too slow and jittery for audio.

On `fm_board` (STM32U575) it replaces the SA818 driver's old per-sample
`k_work` + blocking `adc_read` RX path, which only reached ~2.5 kHz against an
advertised 8 kHz and starved the USB host. With this module the capture runs at a true,
sustained 8 kHz.

## How it works

```
sampling-timer (TIM6) --TRGO @ rate--> ADC1 ch --circular DMA--> dma_buf[2 x block]
                                          | half/full-transfer IRQ (every block)
                                          v
   aai_dma_cb (ISR): adc_to_pcm16() -> k_msgq -> submit work
                                          v
   aai_drain_work (system workqueue thread): -> on_samples(samples, count)
```

- The timer's update event (master-mode `TRGO = UPDATE`) triggers one ADC conversion per
  tick; the ADC is set to `DMA_TRANSFER_UNLIMITED` so it keeps issuing a DMA request per
  conversion. The DMA runs circular (GPDMA `source_reload_en`), giving double buffering
  via the half/full-transfer callbacks.
- The DMA callback runs in **ISR context**, so it converts the ready half-buffer to PCM,
  queues it to a `k_msgq`, and submits a work item. The system-workqueue handler delivers
  the blocks to the consumer callback in **thread context**, so the consumer may block or
  take a mutex.

## API

`include/oe5xrx/audio/analog_audio_in.h`:

- `int analog_audio_in_start(dev, cb, user_data)` — start capture; `cb` is invoked once
  per DMA block with `block-samples` PCM samples, from the workqueue thread.
- `int analog_audio_in_stop(dev)` — stop.

## Devicetree

```dts
audio_in: analog-audio-in {
    compatible = "oe5xrx,analog-audio-in";
    io-channels = <&adc1 5>;                 /* ADC instance + channel */
    sampling-timer = <&timers6>;             /* master-mode TRGO = UPDATE */
    dmas = <&gpdma1 0 0 (STM32_DMA_16BITS | STM32_DMA_PRIORITY_HIGH)>;
    dma-names = "rx";
    sampling-frequency = <8000>;
    resolution = <12>;
    block-samples = <8>;                     /* samples per callback (~1 ms @ 8 kHz) */
};
```

`&timers6` needs `status = "okay"; st,mastermode = "UPDATE"; st,prescaler = <0>;` and
`&gpdma1` needs `status = "okay";`.

## Notes / limits

- **STM32-specific** (TIM/ADC via LL, DMA via `dma_stm32u5`). Not built/instantiated on
  `native_sim`; the pure conversion helper `adc_to_pcm16` is unit-tested there.
- The module drives ADC1 via LL; Zephyr's `adc_stm32` driver still binds the same node
  (for the SA818 `io-channels` reference) but does not actively convert — they coexist.
  Avoid issuing `adc_read` on the same ADC channel while capture is running.
- RX only. The SA818 TX (DAC) path is unchanged and still uses the legacy `k_work` path.
