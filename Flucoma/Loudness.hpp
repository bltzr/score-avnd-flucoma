#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/LoudnessClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct Loudness
{
  halp_meta(name, "Loudness")
  halp_meta(c_name, "fluid_loudness")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description, "Loudness (EBU R128) and true peak amplitude of an audio input")
  halp_meta(uuid, "BF6E674E-9EBB-4359-9EA4-EEAB17A46C26")

  using Client = fluid::client::RTLoudnessClient;
  using Idx = fluid::client::loudness::LoudnessParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    // kSelect (ChoicesT) not exposed yet: default computes all outputs.
    FluCoMa::fluid_param_for<Client, Idx::kKWeighting> kWeighting;
    FluCoMa::fluid_param_for<Client, Idx::kTruePeak> truePeak;
    FluCoMa::fluid_param_for<Client, Idx::kWindowSize> windowSize;
    FluCoMa::fluid_param_for<Client, Idx::kHopSize> hopSize;
    // kMaxWindowSize (LongParam<Fixed<true>>) is an instantiation-time
    // constant: the algorithm is allocated with its default (16384), so it is
    // intentionally not exposed as a runtime control.
  } inputs;

  struct outs
  {
    halp::val_port<"Loudness", float> loudness;
    halp::val_port<"Peak", float> peak;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    double features[2]{};
    host.process_control(inputs.audio.samples, 1, frames, features, 2);

    outputs.loudness = static_cast<float>(features[0]);
    outputs.peak = static_cast<float>(features[1]);
  }
};

}
