#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/BaseSTFTClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct BaseSTFT
{
  halp_meta(name, "BaseSTFT")
  halp_meta(c_name, "fluid_basestft")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "STFT analysis followed by inverse STFT resynthesis: a "
            "passthrough for testing FFT settings and latency")
  halp_meta(uuid, "6EF8220B-5B72-443B-86A2-5A0E8A7A5BA8")

  using Client = fluid::client::RTSTFTPassClient;
  using Idx = fluid::client::stftpass::STFTParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
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
    host.process(inputs.audio.samples, 1, outputs.audio.samples, 1, frames);
  }
};

}
