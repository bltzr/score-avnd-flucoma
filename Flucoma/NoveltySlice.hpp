#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/NoveltySliceClient.hpp>

#include <halp/audio.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct NoveltySlice
{
  halp_meta(name, "NoveltySlice")
  halp_meta(c_name, "fluid_noveltyslice")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Novelty-based slicer (spectrum, MFCC, chroma, pitch or loudness "
            "feature): emits a 1 at detected slice onsets, 0 otherwise")
  halp_meta(uuid, "F9476417-4A61-4985-AF40-C91D7AEF045A")

  using Client = fluid::client::RTNoveltySliceClient;
  using Idx = fluid::client::noveltyslice::NoveltyParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kFeature> algorithm;
    FluCoMa::fluid_long_max_param<Client, Idx::kKernelSize, 3, 101> kernelSize;
    FluCoMa::fluid_param_for<Client, Idx::kThreshold> threshold;
    FluCoMa::fluid_long_max_param<Client, Idx::kFilterSize, 1, 100> filterSize;
    FluCoMa::fluid_param_for<Client, Idx::kDebounce> minSliceLength;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::fixed_audio_bus<"Out", double, 1> audio;
    halp::val_port<"Slice", bool> slice;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);
    host.process(inputs.audio.samples, 1, outputs.audio.samples, 1, frames);

    bool detected = false;
    for(int i = 0; i < frames; ++i)
      if(outputs.audio.samples[0][i] > 0.5)
      {
        detected = true;
        break;
      }
    outputs.slice = detected;
  }
};

}
