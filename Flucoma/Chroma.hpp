#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/ChromaClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <vector>

namespace Flucoma
{

struct Chroma
{
  halp_meta(name, "Chroma")
  halp_meta(c_name, "fluid_chroma")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description, "Energies at chroma bins of an audio input")
  halp_meta(uuid, "2A1304FC-19F8-459B-97D0-E0AD66566261")

  using Client = fluid::client::RTChromaClient;
  using Idx = fluid::client::chroma::ChromaParamIndex;

  static constexpr int max_chroma = 96;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_long_max_param<Client, Idx::kNChroma, 12, max_chroma>
        numChroma;
    FluCoMa::fluid_param_for<Client, Idx::kRef> ref;
    FluCoMa::fluid_param_for<Client, Idx::kNorm> normalize;
    FluCoMa::fluid_param_for<Client, Idx::kMinFreq> minFreq;
    // -1 = automatic (Nyquist); the toggle forces -1 and overrides the slider
    FluCoMa::fluid_auto_toggle<Client, Idx::kMaxFreq> maxFreqAuto;
    FluCoMa::fluid_float_param_ranged<Client, Idx::kMaxFreq, 20., 22000., 10000.>
        maxFreq;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Chroma", std::vector<float>> chroma;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    const int n = std::min(inputs.numChroma.value, max_chroma);
    double features[max_chroma]{};
    host.process_control(inputs.audio.samples, 1, frames, features, n);

    outputs.chroma.value.assign(features, features + n);
  }
};

}
