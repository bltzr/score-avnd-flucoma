#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/rt/NMFMorphClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

/* Realtime cross-synthesis morphing between two sets of NMF bases (learned
 * offline, e.g. by two BufNMF passes), driven by stored activations.
 * All three files are loaded from the working folder at playback start.
 * This is a generator: no audio input.
 */
struct NMFMorph
{
  halp_meta(name, "NMFMorph")
  halp_meta(c_name, "fluid_nmfmorph")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Realtime NMF cross-synthesis morphing between two spectral bases")
  halp_meta(uuid, "9FF9B364-2E74-4469-9FB2-0BF67EC67DA7")

  using Client = fluid::client::RTNMFMorphClient;
  using Idx = fluid::client::nmfmorph::NMFFilterIndex;

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Source bases", "nmf_bases_a.wav"> sourceBases;
    halp::lineedit<"Target bases", "nmf_bases_b.wav"> targetBases;
    halp::lineedit<"Activations", "nmf_activations.wav"> activations;

    FluCoMa::fluid_param_for<Client, Idx::kAutoAssign> autoassign;
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
  FluCoMa::file_ref sourceRef, targetRef, actRef;
  double session_rate{48000.};

  void prepare(halp::setup s)
  {
    session_rate = s.rate;
    host.sync_params(inputs);
    host.reconstruct();
    host.prepare(s.rate, s.frames);

    FluCoMa::pool().get(inputs.folder.value, inputs.sourceBases.value, s.rate);
    FluCoMa::pool().get(inputs.folder.value, inputs.targetBases.value, s.rate);
    FluCoMa::pool().get(inputs.folder.value, inputs.activations.value, s.rate);
  }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    sourceRef.refresh(inputs.folder.value, inputs.sourceBases.value);
    if(sourceRef.changed())
      host.set_input_buffer<Idx::kSourceBuf>(FluCoMa::pool().get(
          inputs.folder.value, inputs.sourceBases.value, session_rate));
    targetRef.refresh(inputs.folder.value, inputs.targetBases.value);
    if(targetRef.changed())
      host.set_input_buffer<Idx::kTargetBuf>(FluCoMa::pool().get(
          inputs.folder.value, inputs.targetBases.value, session_rate));
    actRef.refresh(inputs.folder.value, inputs.activations.value);
    if(actRef.changed())
      host.set_input_buffer<Idx::kActBuf>(FluCoMa::pool().get(
          inputs.folder.value, inputs.activations.value, session_rate));

    host.process(nullptr, 0, outputs.audio.samples, 1, frames);
  }
};

}
