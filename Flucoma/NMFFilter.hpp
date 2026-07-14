#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/rt/NMFFilterClient.hpp>

#include <halp/folder_combobox.hpp>
#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

/* Realtime NMF filtering: splits the input into components matching spectral
 * bases learned offline (e.g. by BufNMF), one output channel per component.
 * The bases file is loaded from the working folder at playback start.
 */
struct NMFFilter
{
  halp_meta(name, "NMFFilter")
  halp_meta(c_name, "fluid_nmffilter")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Realtime source separation against pre-learned NMF bases")
  halp_meta(uuid, "0DB7C767-35D0-4589-89A1-B409FAD2D4D8")

  using Client = fluid::client::RTNMFFilterClient;
  using Idx = fluid::client::nmffilter::NMFFilterIndex;

  static constexpr int max_components = 8;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Bases", "Folder", "wav", "nmf_bases.wav"> bases;

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
    halp::fixed_audio_bus<"Components", double, max_components> audio;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;
  FluCoMa::file_ref basesRef;
  double session_rate{48000.};

  void prepare(halp::setup s)
  {
    session_rate = s.rate;
    host.sync_params(inputs);
    host.reconstruct(); // output rank is sized at construction
    host.prepare(s.rate, s.frames);

    // Warm the pool so the first tick's swap is just a pointer exchange.
    FluCoMa::pool().get(inputs.folder.value, inputs.bases.value, session_rate);
  }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    // Follows path edits and disk changes (score::FileWatch via the pool).
    basesRef.refresh(inputs.folder.value, inputs.bases.value);
    if(basesRef.changed())
      host.set_input_buffer<Idx::kFilterbuf>(FluCoMa::pool().get(
          inputs.folder.value, inputs.bases.value, session_rate));

    host.process(
        inputs.audio.samples, 1, outputs.audio.samples, max_components, frames);
  }
};

}
