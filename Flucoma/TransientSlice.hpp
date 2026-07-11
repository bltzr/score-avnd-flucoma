#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/TransientSliceClient.hpp>

#include <halp/audio.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct TransientSlice
{
  halp_meta(name, "TransientSlice")
  halp_meta(c_name, "fluid_transientslice")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Transient-based slicer (de-clicking model): emits a 1 at "
            "detected slice onsets, 0 otherwise")
  halp_meta(uuid, "8833824D-86B5-4032-826F-08B775AFC81A")

  using Client = fluid::client::RTTransientSliceClient;
  using Idx = fluid::client::transientslice::TransientParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kOrder> order;
    FluCoMa::fluid_param_for<Client, Idx::kBlockSize> blockSize;
    FluCoMa::fluid_param_for<Client, Idx::kPadding> padding;
    FluCoMa::fluid_param_for<Client, Idx::kSkew> skew;
    FluCoMa::fluid_param_for<Client, Idx::kThreshFwd> threshFwd;
    FluCoMa::fluid_param_for<Client, Idx::kThreshBack> threshBack;
    FluCoMa::fluid_param_for<Client, Idx::kWinSize> windowSize;
    FluCoMa::fluid_param_for<Client, Idx::kDebounce> clumpLength;
    FluCoMa::fluid_param_for<Client, Idx::kMinSeg> minSliceLength;
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
