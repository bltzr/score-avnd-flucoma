#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/OnsetSliceClient.hpp>

#include <halp/audio.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct OnsetSlice
{
  halp_meta(name, "OnsetSlice")
  halp_meta(c_name, "fluid_onsetslice")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Spectral difference onset slicer: emits a 1 at detected slice "
            "onsets, 0 otherwise")
  halp_meta(uuid, "36827D1F-28DF-4619-BCAD-3CE4098B8C97")

  using Client = fluid::client::RTOnsetSliceClient;
  using Idx = fluid::client::onsetslice::OnsetParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kFunction> metric;
    FluCoMa::fluid_param_for<Client, Idx::kThreshold> threshold;
    FluCoMa::fluid_param_for<Client, Idx::kDebounce> minSliceLength;
    FluCoMa::fluid_param_for<Client, Idx::kFilterSize> filterSize;
    FluCoMa::fluid_param_for<Client, Idx::kFrameDelta> frameDelta;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::fixed_audio_bus<"Out", double, 1> audio;
    halp::val_port<"Slice", bool> slice;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);
    host.process(inputs.audio.samples, 1, outputs.audio.samples, 1, frames);

    bool detected = false;
    for(int i = 0; i < frames; ++i)
      if(outputs.audio.samples[0][i] > 0.5)
      {
        detected = true;
        break;
      }
    outputs.slice = detected;
  }
};

}
