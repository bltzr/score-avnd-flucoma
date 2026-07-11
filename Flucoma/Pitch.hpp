#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/PitchClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct Pitch
{
  halp_meta(name, "Pitch")
  halp_meta(c_name, "fluid_pitch")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Pitch and pitch confidence estimation (Cepstrum, HPS or YinFFT)")
  halp_meta(uuid, "D57B2DE3-C9B2-4C1F-87F6-D1760003194A")

  using Client = fluid::client::RTPitchClient;
  using Idx = fluid::client::pitch::PitchParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    // kSelect (ChoicesT multi-select) intentionally not exposed yet: default
    // computes all outputs.
    FluCoMa::fluid_param_for<Client, Idx::kAlgorithm> algorithm;
    FluCoMa::fluid_param_for<Client, Idx::kMinFreq> minFreq;
    FluCoMa::fluid_param_for<Client, Idx::kMaxFreq> maxFreq;
    FluCoMa::fluid_param_for<Client, Idx::kUnit> unit;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Pitch", float> pitch;
    halp::val_port<"Confidence", float> confidence;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    double features[2]{};
    host.process_control(inputs.audio.samples, 1, frames, features, 2);

    outputs.pitch = static_cast<float>(features[0]);
    outputs.confidence = static_cast<float>(features[1]);
  }
};

}
