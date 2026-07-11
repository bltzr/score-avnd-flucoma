#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/AmpSliceClient.hpp>

#include <halp/audio.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct AmpSlice
{
  halp_meta(name, "AmpSlice")
  halp_meta(c_name, "fluid_ampslice")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Absolute amplitude envelope slicer: emits a 1 at detected slice "
            "onsets, 0 otherwise")
  halp_meta(uuid, "970DF651-7F67-4962-BB74-AE17241E6FEC")

  using Client = fluid::client::RTAmpSliceClient;
  using Idx = fluid::client::ampslice::AmpSliceParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kFastRampUpTime> fastRampUp;
    FluCoMa::fluid_param_for<Client, Idx::kFastRampDownTime> fastRampDown;
    FluCoMa::fluid_param_for<Client, Idx::kSlowRampUpTime> slowRampUp;
    FluCoMa::fluid_param_for<Client, Idx::kSlowRampDownTime> slowRampDown;
    FluCoMa::fluid_param_for<Client, Idx::kOnThreshold> onThreshold;
    FluCoMa::fluid_param_for<Client, Idx::kOffThreshold> offThreshold;
    FluCoMa::fluid_param_for<Client, Idx::kSilenceThreshold> floor;
    FluCoMa::fluid_param_for<Client, Idx::kDebounce> minSliceLength;
    FluCoMa::fluid_param_for<Client, Idx::kHiPassFreq> highPassFreq;
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
