#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/SpectralShapeClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct SpectralShape
{
  halp_meta(name, "Spectral Shape")
  halp_meta(c_name, "fluid_spectralshape")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Seven statistical descriptors of the spectral shape of an audio input")
  halp_meta(uuid, "264D2F81-379F-49B5-AFD2-9A3C1ADBF29E")

  using Client = fluid::client::RTSpectralShapeClient;
  using Idx = fluid::client::spectralshape::SpectralShapeParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    // kSelect (ChoicesT) not exposed yet: default computes all outputs.
    FluCoMa::fluid_param_for<Client, Idx::kMinFreq> minFreq;
    // -1 = automatic (Nyquist); the toggle forces -1 and overrides the slider
    FluCoMa::fluid_auto_toggle<Client, Idx::kMaxFreq> maxFreqAuto;
    FluCoMa::fluid_float_param_ranged<Client, Idx::kMaxFreq, 20., 22000., 10000.>
        maxFreq;
    FluCoMa::fluid_param_for<Client, Idx::kRollOffPercent> rolloffPercent;
    FluCoMa::fluid_param_for<Client, Idx::kFreqUnits> unit;
    FluCoMa::fluid_param_for<Client, Idx::kAmpMeasure> power;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Centroid", float> centroid;
    halp::val_port<"Spread", float> spread;
    halp::val_port<"Skewness", float> skewness;
    halp::val_port<"Kurtosis", float> kurtosis;
    halp::val_port<"Rolloff", float> rolloff;
    halp::val_port<"Flatness", float> flatness;
    halp::val_port<"Crest", float> crest;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    double features[7]{};
    host.process_control(inputs.audio.samples, 1, frames, features, 7);

    outputs.centroid = static_cast<float>(features[0]);
    outputs.spread = static_cast<float>(features[1]);
    outputs.skewness = static_cast<float>(features[2]);
    outputs.kurtosis = static_cast<float>(features[3]);
    outputs.rolloff = static_cast<float>(features[4]);
    outputs.flatness = static_cast<float>(features[5]);
    outputs.crest = static_cast<float>(features[6]);
  }
};

}
