#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/nrt/BufStatsClient.hpp>

#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

/* Offline statistics over a buffer read from the working folder.
 * Runs in a worker thread when playback starts and whenever "Process" is
 * toggled on. Results go to the "Stats" value output and to
 * <folder>/<stats file> as a float32 WAV readable by other FluCoMa objects.
 */
struct BufStats
{
  halp_meta(name, "BufStats")
  halp_meta(c_name, "fluid_bufstats")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Statistics (mean, stddev, skewness, kurtosis, percentiles) over "
            "the channels of a sound file")
  halp_meta(uuid, "87E4C818-031A-4AA1-B47E-CFE6CE66CFE2")

  using Client
      = fluid::client::ClientWrapper<fluid::client::bufstats::BufferStatsClient>;
  using Idx = fluid::client::bufstats::BufferStatsParamIndex;

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Source", "source.wav"> source;
    halp::lineedit<"Stats file", "stats.wav"> statsFile;
    halp::lineedit<"Weights", ""> weights; // optional weights buffer

    halp::toggle<"Process"> process;

    // kSelect (ChoicesT) not exposed yet: default computes all statistics.
    FluCoMa::fluid_param_for<Client, Idx::kNumDerivatives> numDerivs;
    FluCoMa::fluid_param_for<Client, Idx::kLow> low;
    FluCoMa::fluid_param_for<Client, Idx::kMiddle> middle;
    FluCoMa::fluid_param_for<Client, Idx::kHigh> high;
    FluCoMa::fluid_param_for<Client, Idx::kOutliersCutoff> outliersCutoff;

    FluCoMa::fluid_long_param_ranged<Client, Idx::kOffset, 0, 100000000, 0>
        startFrame;
    FluCoMa::fluid_auto_toggle<Client, Idx::kNumFrames> allFrames;
    FluCoMa::fluid_long_param_ranged<Client, Idx::kNumFrames, 1, 100000000, 1>
        numFrames;
    FluCoMa::fluid_long_param_ranged<Client, Idx::kStartChan, 0, 64, 0> startChan;
    FluCoMa::fluid_auto_toggle<Client, Idx::kNumChans> allChans;
    FluCoMa::fluid_long_param_ranged<Client, Idx::kNumChans, 1, 64, 1> numChans;
  } inputs;

  struct outs
  {
    // One row per source channel: [mean, stddev, skew, kurt, low, mid, high]
    // (x (1 + numDerivs)), flattened.
    halp::val_port<"Stats", std::vector<float>> stats;
    halp::val_port<"Status", std::string> status;
  } outputs;

  double session_rate{48000.};
  bool last_process{false};

  // The job is self-contained: the worker owns its client and parameter set,
  // reads/writes the folder, and posts results back to the object.
  struct worker_t
  {
    // The request payload must stay small (oscr wraps it in a 128-byte
    // SmallFun), so the inputs snapshot travels on the heap.
    std::function<void(std::shared_ptr<ins>, double)> request;

    static std::function<void(BufStats&)> work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;
      FluCoMa::FluidNRTHost<Client> host;
      FluCoMa::sync_params_into(host.params, in);

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](BufStats& self) {
          self.outputs.status.value = msg;
        };
      };

      auto source = FluCoMa::pool().get(in.folder.value, in.source.value, rate);
      if(!source)
        return fail("Could not load source file");
      host.set_input_buffer<Idx::kSource>(std::move(source));

      if(!in.weights.value.empty())
      {
        if(auto w = FluCoMa::pool().get(in.folder.value, in.weights.value, rate))
          host.set_input_buffer<Idx::kWeights>(std::move(w));
      }

      auto stats = host.make_output_buffer<Idx::kStats>();

      auto result = host.run();
      if(!result.ok())
        return fail(result.message().c_str());

      std::vector<float> out;
      {
        fluid::client::BufferAdaptor::ReadAccess acc(stats.get());
        if(acc.exists() && acc.valid())
          for(fluid::index c = 0; c < acc.numChans(); ++c)
          {
            auto ch = acc.samps(c);
            for(fluid::index i = 0; i < ch.size(); ++i)
              out.push_back(ch(i));
          }
      }

      if(!in.statsFile.value.empty())
        FluCoMa::pool().write_through(
            in.folder.value, in.statsFile.value, *stats, rate);

      // Prime the change trackers so our own get()s don't retrigger a run.
      return [out = std::move(out), f = in.folder.value, s = in.source.value,
              w = in.weights.value](BufStats& self) {
        self.outputs.stats.value = out;
        self.outputs.status.value = "OK";
        self.sourceRef.refresh(f, s);
        self.sourceRef.changed();
        self.weightsRef.refresh(f, w);
        self.weightsRef.changed();
      };
    }
  } worker;

  FluCoMa::file_ref sourceRef, weightsRef;
  bool first_run_pending{true};

  void prepare(halp::setup s)
  {
    session_rate = s.rate;
    first_run_pending = true;
  }

  void operator()(int)
  {
    const bool p = inputs.process.value;
    const bool edge = p && !last_process;
    last_process = p;

    // Re-run when the source/weights files change on disk or the paths are
    // edited (pool versions move; primed after each run by the completion).
    sourceRef.refresh(inputs.folder.value, inputs.source.value);
    weightsRef.refresh(inputs.folder.value, inputs.weights.value);
    bool inputs_changed = sourceRef.changed() | weightsRef.changed();
    // Before the first run nothing is in the pool yet: versions are all 0 and
    // changed() stays false, hence the explicit first-run trigger.
    if(first_run_pending || edge || inputs_changed)
    {
      first_run_pending = false;
      worker.request(std::make_shared<ins>(inputs), session_rate);
    }
  }
};

}
