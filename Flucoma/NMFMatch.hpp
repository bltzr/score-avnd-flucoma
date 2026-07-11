#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/rt/NMFMatchClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <vector>

namespace Flucoma
{

/* Realtime NMF matching: outputs the activation of each pre-learned spectral
 * base (e.g. from BufNMF) in the incoming audio.
 * The bases file is loaded from the working folder at playback start.
 */
struct NMFMatch
{
  halp_meta(name, "NMFMatch")
  halp_meta(c_name, "fluid_nmfmatch")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Realtime activation tracking against pre-learned NMF bases")
  halp_meta(uuid, "AA22CCBE-5DC4-4AA9-8E79-8820A6399F18")

  using Client = fluid::client::RTNMFMatchClient;
  using Idx = fluid::client::nmfmatch::NMFMatchParamIndex;

  static constexpr int max_components = 8;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    halp::lineedit<"Folder", ""> folder;
    halp::lineedit<"Bases", "nmf_bases.wav"> bases;

    FluCoMa::fluid_long_max_param<Client, Idx::kMaxRank, max_components,
                                  max_components>
        maxComponents;
    FluCoMa::fluid_param_for<Client, Idx::kIterations> iterations;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Activations", std::vector<float>> activations;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;
  FluCoMa::file_ref basesRef;
  double session_rate{48000.};

  void prepare(halp::setup s)
  {
    session_rate = s.rate;
    host.sync_params(inputs);
    host.reconstruct();
    host.prepare(s.rate, s.frames);

    FluCoMa::pool().get(inputs.folder.value, inputs.bases.value, session_rate);
  }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    basesRef.refresh(inputs.folder.value, inputs.bases.value);
    if(basesRef.changed())
      host.set_input_buffer<Idx::kFilterbuf>(FluCoMa::pool().get(
          inputs.folder.value, inputs.bases.value, session_rate));

    const int n = std::min(inputs.maxComponents.value, max_components);
    double features[max_components]{};
    host.process_control(inputs.audio.samples, 1, frames, features, n);

    outputs.activations.value.assign(features, features + n);
  }
};

}
