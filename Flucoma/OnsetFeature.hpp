#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/OnsetFeatureClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct OnsetFeature
{
  halp_meta(name, "Onset Feature")
  halp_meta(c_name, "fluid_onsetfeature")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Spectral difference onset detection function of an audio input")
  halp_meta(uuid, "7CAB545D-5200-47B6-B95F-8E6BF909E314")

  using Client = fluid::client::RTOnsetFeatureClient;
  using Idx = fluid::client::onsetfeature::OnsetParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kFunction> metric;
    FluCoMa::fluid_param_for<Client, Idx::kFilterSize> filterSize;
    FluCoMa::fluid_param_for<Client, Idx::kFrameDelta> frameDelta;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Spectral Difference", float> feature;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    double features[1]{};
    host.process_control(inputs.audio.samples, 1, frames, features, 1);

    outputs.feature = static_cast<float>(features[0]);
  }
};

}
