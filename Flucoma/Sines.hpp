#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/SinesClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct Sines
{
  halp_meta(name, "Sines")
  halp_meta(c_name, "fluid_sines")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Sinusoidal modelling: separates tracked sinusoidal components "
            "from the residual signal")
  halp_meta(uuid, "973B1B91-D814-4AF9-83F6-8E7328B3835E")

  using Client = fluid::client::RTSinesClient;
  using Idx = fluid::client::sines::SinesParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kBandwidth> bandwidth;
    FluCoMa::fluid_param_for<Client, Idx::kDetectionThreshold> detectionThreshold;
    FluCoMa::fluid_param_for<Client, Idx::kBirthLowThreshold> birthLowThreshold;
    FluCoMa::fluid_param_for<Client, Idx::kBirthHighThreshold> birthHighThreshold;
    FluCoMa::fluid_param_for<Client, Idx::kMinTrackLen> minTrackLen;
    FluCoMa::fluid_param_for<Client, Idx::kTrackMethod> trackMethod;
    FluCoMa::fluid_param_for<Client, Idx::kTrackMagRange> trackMagRange;
    FluCoMa::fluid_param_for<Client, Idx::kTrackFreqRange> trackFreqRange;
    FluCoMa::fluid_param_for<Client, Idx::kTrackProb> trackProb;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::fixed_audio_bus<"Sines", double, 1> sines;
    halp::fixed_audio_bus<"Residual", double, 1> residual;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    double* out_chans[2]{outputs.sines.samples[0], outputs.residual.samples[0]};
    host.process(inputs.audio.samples, 1, out_chans, 2, frames);
  }
};

}
