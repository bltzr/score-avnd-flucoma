#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/nrt/BufThreshClient.hpp>

#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{

/* Offline thresholding of a sound file read from the working folder: sample
 * values below the threshold are set to zero. Runs in a worker thread when
 * playback starts and whenever "Process" is toggled on.
 */
struct BufThresh
{
  halp_meta(name, "BufThresh")
  halp_meta(c_name, "fluid_bufthresh")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Zero the sample values of a sound file that fall below a "
            "threshold")
  halp_meta(uuid, "6BECD963-F909-4BA0-9768-ED905EEE2159")

  using Client
      = fluid::client::ClientWrapper<fluid::client::bufthresh::BufThreshClient>;
  using Idx = fluid::client::bufthresh::BufSelectParamIndex;

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Source", "source.wav"> source;
    halp::lineedit<"Destination", "out.wav"> destination;

    halp::toggle<"Process"> process;

    // Unconstrained in flucoma (thresholds on feature data can be any
    // magnitude); give it an explicit bipolar UI range.
    FluCoMa::fluid_float_param_ranged<Client, Idx::kThresh, -1000., 1000., 0.>
        threshold;

    FluCoMa::fluid_long_param_ranged<Client, Idx::kStartFrame, 0, 100000000, 0>
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
    halp::val_port<"Status", std::string> status;
  } outputs;

  double session_rate{48000.};
  bool last_process{false};

  struct worker_t
  {
    // The request payload must stay small (oscr wraps it in a 128-byte
    // SmallFun), so the inputs snapshot travels on the heap.
    std::function<void(std::shared_ptr<ins>, double)> request;

    static std::function<void(BufThresh&)>
    work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;
      FluCoMa::FluidNRTHost<Client> host;
      FluCoMa::sync_params_into(host.params, in);

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](BufThresh& self) {
          self.outputs.status.value = msg;
        };
      };

      auto source = FluCoMa::pool().get(in.folder.value, in.source.value, rate);
      if(!source)
        return fail("Could not load source file");
      host.set_input_buffer<Idx::kSource>(std::move(source));

      if(in.destination.value.empty())
        return fail("No destination file name");

      auto dest = host.make_output_buffer<Idx::kDest>();

      auto result = host.run();
      if(!result.ok())
        return fail(result.message().c_str());

      if(!FluCoMa::pool().write_through(
             in.folder.value, in.destination.value, *dest, rate))
        return fail("Could not write destination file");

      // Prime the change tracker so our own get() doesn't retrigger a run.
      return [f = in.folder.value, s = in.source.value](BufThresh& self) {
        self.outputs.status.value = "OK";
        self.sourceRef.refresh(f, s);
        self.sourceRef.changed();
      };
    }
  } worker;

  FluCoMa::file_ref sourceRef;
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

    // Re-run when the source file changes on disk or the path is edited
    // (pool versions move; primed after each run by the completion).
    sourceRef.refresh(inputs.folder.value, inputs.source.value);
    bool inputs_changed = sourceRef.changed();
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
