#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/nrt/NMFClient.hpp>

#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

/* Offline NMF decomposition of a sound file into components.
 * Writes into the working folder: resynthesized components (multichannel),
 * spectral bases and temporal activations — all reusable by other FluCoMa
 * objects (NMFFilter, NMFMatch...).
 * Can take a long time; runs in a worker thread.
 */
struct BufNMF
{
  halp_meta(name, "BufNMF")
  halp_meta(c_name, "fluid_bufnmf")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Non-negative matrix factorisation of a sound file into components")
  halp_meta(uuid, "4110681B-9CD2-46E3-8346-C90073C7C012")

  using Client = fluid::client::ClientWrapper<fluid::client::bufnmf::NMFClient>;
  using Idx = fluid::client::bufnmf::NMFParamIndex;

  struct ins
  {
    halp::lineedit<"Folder", ""> folder;
    halp::lineedit<"Source", "source.wav"> source;
    halp::lineedit<"Resynth file", "nmf_resynth.wav"> resynthFile;
    halp::lineedit<"Bases file", "nmf_bases.wav"> basesFile;
    halp::lineedit<"Activations file", "nmf_activations.wav"> activationsFile;

    halp::toggle<"Process"> process;

    FluCoMa::fluid_long_param_ranged<Client, Idx::kRank, 1, 64, 2> components;
    FluCoMa::fluid_param_for<Client, Idx::kIterations> iterations;
    FluCoMa::fluid_param_for<Client, Idx::kResynthMode> resynthMode;
    FluCoMa::fluid_param_for<Client, Idx::kFiltersUpdate> basesMode;
    FluCoMa::fluid_param_for<Client, Idx::kEnvelopesUpdate> actMode;

    FluCoMa::fluid_long_param_ranged<Client, Idx::kOffset, 0, 100000000, 0>
        startFrame;
    FluCoMa::fluid_auto_toggle<Client, Idx::kNumFrames> allFrames;
    FluCoMa::fluid_long_param_ranged<Client, Idx::kNumFrames, 1, 100000000, 1>
        numFrames;
    FluCoMa::fluid_long_param_ranged<Client, Idx::kStartChan, 0, 64, 0> startChan;
    FluCoMa::fluid_auto_toggle<Client, Idx::kNumChans> allChans;
    FluCoMa::fluid_long_param_ranged<Client, Idx::kNumChans, 1, 64, 1> numChans;

    FluCoMa::fluid_fft_window<Client, Idx::kFFT> window;
    FluCoMa::fluid_fft_hop<Client, Idx::kFFT> hop;
    FluCoMa::fluid_fft_size<Client, Idx::kFFT> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Status", std::string> status;
  } outputs;

  double session_rate{48000.};
  bool last_process{false};

  struct worker_t
  {
    std::function<void(std::shared_ptr<ins>, double)> request;

    static std::function<void(BufNMF&)> work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;
      FluCoMa::FluidNRTHost<Client> host;
      FluCoMa::sync_params_into(host.params, in);

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](BufNMF& self) {
          self.outputs.status.value = msg;
        };
      };

      // Resynth is only computed when resynthMode != 0; enable it implicitly
      // when the user gave a resynth file name.
      if(!in.resynthFile.value.empty())
        host.params.template set<Idx::kResynthMode>(1, nullptr);

      auto source = FluCoMa::pool().get(in.folder.value, in.source.value, rate);
      if(!source)
        return fail("Could not load source file");
      host.set_input_buffer<Idx::kSource>(std::move(source));

      auto resynth = host.make_output_buffer<Idx::kResynth>();
      auto bases = host.make_output_buffer<Idx::kFilters>();
      auto activations = host.make_output_buffer<Idx::kEnvelopes>();

      auto result = host.run();
      if(!result.ok())
        return fail(result.message().c_str());

      if(!in.resynthFile.value.empty())
        FluCoMa::pool().write_through(
            in.folder.value, in.resynthFile.value, *resynth, rate);
      if(!in.basesFile.value.empty())
        FluCoMa::pool().write_through(
            in.folder.value, in.basesFile.value, *bases, rate);
      if(!in.activationsFile.value.empty())
        FluCoMa::pool().write_through(
            in.folder.value, in.activationsFile.value, *activations, rate);

      return [](BufNMF& self) { self.outputs.status.value = "OK"; };
    }
  } worker;

  void prepare(halp::setup s)
  {
    session_rate = s.rate;
    // NMF is expensive: unlike the cheap buffer objects, do NOT run
    // automatically at playback start — only on explicit "Process".
  }

  void operator()(int)
  {
    const bool p = inputs.process.value;
    if(p && !last_process)
      worker.request(std::make_shared<ins>(inputs), session_rate);
    last_process = p;
  }
};

}
