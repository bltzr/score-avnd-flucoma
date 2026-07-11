#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/NoveltyFeatureClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct NoveltyFeature
{
  halp_meta(name, "Novelty Feature")
  halp_meta(c_name, "fluid_noveltyfeature")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Novelty curve of an audio input, from self-similarity of a chosen feature")
  halp_meta(uuid, "850A41EC-591E-4D25-85BA-0DFE6F7C74CA")

  using Client = fluid::client::RTNoveltyFeatureClient;
  using Idx = fluid::client::noveltyfeature::NoveltyParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kFeature> algorithm;
    // LongParamRuntimeMax: defaults from defineParameters; UI caps chosen to
    // match the flucoma CLI/Max wrappers' customary maxima (odd kernel <= 101,
    // smoothing filter <= 100).
    FluCoMa::fluid_long_max_param<Client, Idx::kKernelSize, 3, 101> kernelSize;
    FluCoMa::fluid_long_max_param<Client, Idx::kFilterSize, 1, 100> filterSize;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Novelty", float> novelty;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    double features[1]{};
    host.process_control(inputs.audio.samples, 1, frames, features, 1);

    outputs.novelty = static_cast<float>(features[0]);
  }
};

}
