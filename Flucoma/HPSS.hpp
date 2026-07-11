#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/HPSSClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct HPSS
{
  halp_meta(name, "HPSS")
  halp_meta(c_name, "fluid_hpss")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Harmonic-percussive source separation using median filtering, "
            "with optional residual component")
  halp_meta(uuid, "7AA71F6B-1A85-47B5-BC98-DFA9696746F8")

  using Client = fluid::client::RTHPSSClient;
  using Idx = fluid::client::hpss::HPSSParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_long_max_param<Client, Idx::kHSize, 17, 101> harmFilterSize;
    FluCoMa::fluid_long_max_param<Client, Idx::kPSize, 31, 101> percFilterSize;
    FluCoMa::fluid_param_for<Client, Idx::kMode> maskingMode;
    // kHThresh (FloatPairsArrayT) not exposed yet
    // kPThresh (FloatPairsArrayT) not exposed yet

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::fixed_audio_bus<"Harmonic", double, 1> harmonic;
    halp::fixed_audio_bus<"Percussive", double, 1> percussive;
    halp::fixed_audio_bus<"Residual", double, 1> residual;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    double* out_chans[3]{
        outputs.harmonic.samples[0], outputs.percussive.samples[0],
        outputs.residual.samples[0]};
    host.process(inputs.audio.samples, 1, out_chans, 3, frames);
  }
};

}
