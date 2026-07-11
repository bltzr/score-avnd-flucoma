#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/AmpGateClient.hpp>

#include <halp/audio.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct AmpGate
{
  halp_meta(name, "AmpGate")
  halp_meta(c_name, "fluid_ampgate")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Amplitude-based gate: outputs 1 while the signal is 'on', 0 "
            "otherwise")
  halp_meta(uuid, "081968AC-9B25-4E9F-9991-2225F9291FF8")

  using Client = fluid::client::RTAmpGateClient;
  using Idx = fluid::client::ampgate::AmpGateParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kRampUpTime> rampUp;
    FluCoMa::fluid_param_for<Client, Idx::kRampDownTime> rampDown;
    FluCoMa::fluid_param_for<Client, Idx::kOnThreshold> onThreshold;
    FluCoMa::fluid_param_for<Client, Idx::kOffThreshold> offThreshold;
    FluCoMa::fluid_param_for<Client, Idx::kMinEventDuration> minSliceLength;
    FluCoMa::fluid_param_for<Client, Idx::kMinSilenceDuration> minSilenceLength;
    FluCoMa::fluid_param_for<Client, Idx::kMinTimeAboveThreshold> minLengthAbove;
    FluCoMa::fluid_param_for<Client, Idx::kMinTimeBelowThreshold> minLengthBelow;
    FluCoMa::fluid_param_for<Client, Idx::kUpwardLookupTime> lookBack;
    FluCoMa::fluid_param_for<Client, Idx::kDownwardLookupTime> lookAhead;
    FluCoMa::fluid_param_for<Client, Idx::kHiPassFreq> highPassFreq;
    // kMaxSize (LongParam<Fixed<true>>, instantiation-only maximum latency)
    // not exposed: the algorithm is sized once at construction.
  } inputs;

  struct outs
  {
    halp::fixed_audio_bus<"Out", double, 1> audio;
    halp::val_port<"Gate", bool> gate;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);
    host.process(inputs.audio.samples, 1, outputs.audio.samples, 1, frames);

    // Gate semantic: 1 while open; report the state at the end of the block.
    outputs.gate = frames > 0 && outputs.audio.samples[0][frames - 1] > 0.5;
  }
};

}
