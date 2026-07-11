#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/AmpFeatureClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <vector>

namespace Flucoma
{

struct AmpFeature
{
  halp_meta(name, "Amp Feature")
  halp_meta(c_name, "fluid_ampfeature")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Amplitude differential between a fast and a slow envelope follower")
  halp_meta(uuid, "29F2D26B-15FB-4528-8FE5-A470317B645D")

  using Client = fluid::client::RTAmpFeatureClient;
  using Idx = fluid::client::ampfeature::AmpFeatureParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kFastRampUpTime> fastRampUp;
    FluCoMa::fluid_param_for<Client, Idx::kFastRampDownTime> fastRampDown;
    FluCoMa::fluid_param_for<Client, Idx::kSlowRampUpTime> slowRampUp;
    FluCoMa::fluid_param_for<Client, Idx::kSlowRampDownTime> slowRampDown;
    FluCoMa::fluid_param_for<Client, Idx::kSilenceThreshold> floorLevel;
    FluCoMa::fluid_param_for<Client, Idx::kHiPassFreq> highPassFreq;
  } inputs;

  struct outs
  {
    halp::val_port<"Amplitude Differential", float> feature;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  // The client is AudioIn + AudioOut (one feature sample per input sample),
  // so it needs a real block-sized output buffer; the port gets the block's
  // last (most recent) value.
  std::vector<double> out_buffer;

  void prepare(halp::setup s)
  {
    out_buffer.resize(s.frames);
    host.prepare(s.rate, s.frames);
  }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    if(std::size_t(frames) > out_buffer.size())
      out_buffer.resize(frames);

    double* outs[1]{out_buffer.data()};
    host.process(inputs.audio.samples, 1, outs, 1, frames);

    if(frames > 0)
      outputs.feature = static_cast<float>(out_buffer[frames - 1]);
  }
};

}
