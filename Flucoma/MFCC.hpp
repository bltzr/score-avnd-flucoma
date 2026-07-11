#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/MFCCClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <vector>

namespace Flucoma
{

struct MFCC
{
  halp_meta(name, "MFCC")
  halp_meta(c_name, "fluid_mfcc")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description, "Mel-frequency cepstral coefficients of an audio input")
  halp_meta(uuid, "919B3A9C-F604-4B97-87DB-E0D3E4882880")

  using Client = fluid::client::RTMFCCClient;
  using Idx = fluid::client::mfcc::MFCCParamIndex;

  static constexpr int max_coefs = 40;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_long_max_param<Client, Idx::kNCoefs, 13, max_coefs> numCoeffs;
    FluCoMa::fluid_long_max_param<Client, Idx::kNBands, 40, 128> numBands;
    FluCoMa::fluid_param_for<Client, Idx::kDrop0> startCoeff;
    FluCoMa::fluid_param_for<Client, Idx::kMinFreq> minFreq;
    FluCoMa::fluid_param_for<Client, Idx::kMaxFreq> maxFreq;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Coefficients", std::vector<float>> coefs;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    const int n = std::min(inputs.numCoeffs.value, max_coefs);
    double features[max_coefs]{};
    host.process_control(inputs.audio.samples, 1, frames, features, n);

    outputs.coefs.value.assign(features, features + n);
  }
};

}
