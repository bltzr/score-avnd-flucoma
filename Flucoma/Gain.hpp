#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/GainClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct Gain
{
  halp_meta(name, "Gain")
  halp_meta(c_name, "fluid_gain")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Applies gain to a signal, either from a control value or from a "
            "second audio-rate gain signal")
  halp_meta(uuid, "DB364D46-232A-4504-9D66-8A1FB79C7054")

  using Client = fluid::client::RTGainClient;
  using Idx = fluid::client::gain::GainParamTags;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;
    halp::fixed_audio_bus<"Gain", double, 1> gain;

    // Note: the client only reads this control when the second input has no
    // data; with a fixed bus the audio-rate gain input always takes over.
    FluCoMa::fluid_param_for<Client, Idx::kGain> gainAmount;
  } inputs;

  struct outs
  {
    halp::fixed_audio_bus<"Out", double, 1> audio;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    double* in_chans[2]{inputs.audio.samples[0], inputs.gain.samples[0]};
    host.process(in_chans, 2, outputs.audio.samples, 1, frames);
  }
};

}
