#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/MelBandsClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <vector>

namespace Flucoma
{

struct MelBands
{
  halp_meta(name, "MelBands")
  halp_meta(c_name, "fluid_melbands")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description, "Mel band magnitudes of an audio input")
  halp_meta(uuid, "627D6376-B5B6-4F62-94D5-BEDA6C32CEB6")

  using Client = fluid::client::RTMelBandsClient;
  using Idx = fluid::client::melbands::MFCCParamIndex;

  static constexpr int max_bands = 128;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_long_max_param<Client, Idx::kNBands, 40, max_bands> numBands;
    FluCoMa::fluid_param_for<Client, Idx::kMinFreq> minFreq;
    FluCoMa::fluid_param_for<Client, Idx::kMaxFreq> maxFreq;
    FluCoMa::fluid_param_for<Client, Idx::kNormalize> normalize;
    FluCoMa::fluid_param_for<Client, Idx::kScale> scale;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Mel Bands", std::vector<float>> bands;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    const int n = std::min(inputs.numBands.value, max_bands);
    double features[max_bands]{};
    host.process_control(inputs.audio.samples, 1, frames, features, n);

    outputs.bands.value.assign(features, features + n);
  }
};

}
