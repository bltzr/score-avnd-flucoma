#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/SineFeatureClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <vector>

namespace Flucoma
{

struct SineFeature
{
  halp_meta(name, "SineFeature")
  halp_meta(c_name, "fluid_sinefeature")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(
      description,
      "Frequencies and magnitudes of sinusoidal peaks of an audio input")
  halp_meta(uuid, "E86C0908-FA7A-44CB-A3DF-5075933B11B5")

  using Client = fluid::client::RTSineFeatureClient;
  using Idx = fluid::client::sinefeature::SineFeatureParamIndex;

  static constexpr int max_peaks = 128;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_long_max_param<Client, Idx::kNPeaks, 10, max_peaks> numPeaks;
    FluCoMa::fluid_param_for<Client, Idx::kDetectionThreshold> threshold;
    FluCoMa::fluid_param_for<Client, Idx::kSortBy> sortBy;
    FluCoMa::fluid_param_for<Client, Idx::kLogFreq> freqUnit;
    FluCoMa::fluid_param_for<Client, Idx::kLogMag> magUnit;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Peak Frequencies", std::vector<float>> freqs;
    halp::val_port<"Peak Magnitudes", std::vector<float>> mags;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    // The client writes two control rows (peak frequencies, peak magnitudes),
    // so build the tensor views inline instead of using process_control,
    // which only handles a single feature row.
    const int n = std::min(inputs.numPeaks.value, max_peaks);
    double freqs[max_peaks]{};
    double mags[max_peaks]{};

    std::vector<fluid::FluidTensorView<double, 1>> ins, outs;
    ins.emplace_back(inputs.audio.samples[0], 0, frames);
    outs.emplace_back(freqs, 0, n);
    outs.emplace_back(mags, 0, n);

    host.context.hostVectorSize(frames);
    host.client.process(ins, outs, host.context);

    outputs.freqs.value.assign(freqs, freqs + n);
    outputs.mags.value.assign(mags, mags + n);
  }
};

}
