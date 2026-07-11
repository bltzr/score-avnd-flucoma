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

    // Only read by the client when no audio-rate gain is supplied (below)
    FluCoMa::fluid_param_for<Client, Idx::kGain> gainAmount;

    // Explicit switch between the control value and the audio-rate "Gain"
    // input (a fixed bus always carries data, so the client can't tell an
    // unconnected input from silence on its own). Appended last: earlier
    // scenarios keep their port ids.
    halp::toggle<"Use audio-rate gain"> useAudioGain;
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

    // A null second channel makes the client fall back to the gain control
    double* in_chans[2]{
        inputs.audio.samples[0],
        inputs.useAudioGain ? inputs.gain.samples[0] : nullptr};
    host.process(in_chans, 2, outputs.audio.samples, 1, frames);
  }
};

}
