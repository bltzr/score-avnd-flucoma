#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/AudioTransportClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct AudioTransport
{
  halp_meta(name, "AudioTransport")
  halp_meta(c_name, "fluid_audiotransport")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Spectral interpolation between two sounds using optimal "
            "transport of their spectra")
  halp_meta(uuid, "94FACB93-4A00-4B21-B4B8-52BC31EAE95F")

  using Client = fluid::client::RTAudioTransportClient;
  using Idx = fluid::client::audiotransport::AudioTransportParamTags;

  struct ins
  {
    halp::fixed_audio_bus<"In A", double, 1> audioA;
    halp::fixed_audio_bus<"In B", double, 1> audioB;

    FluCoMa::fluid_param_for<Client, Idx::kInterpolation> interpolation;

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

    double* in_chans[2]{inputs.audioA.samples[0], inputs.audioB.samples[0]};
    host.process(in_chans, 2, outputs.audio.samples, 1, frames);
  }
};

}
