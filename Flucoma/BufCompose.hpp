#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/nrt/BufComposeClient.hpp>

#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{
// bufcompose's parameter enum is anonymous; alias its namespace instead of a
// `using Idx = ...ParamIndex` enum alias.
namespace bufcompose = fluid::client::bufcompose;

/* Offline mixing/copying of a source buffer region into a destination buffer,
 * both read from the working folder. The destination is read-modify-write:
 * if <folder>/<destination> already exists it is loaded first, the source
 * region is summed into it (with source/destination gains), and the result is
 * written back. Runs in a worker thread when playback starts and whenever
 * "Process" is toggled on.
 */
struct BufCompose
{
  halp_meta(name, "BufCompose")
  halp_meta(c_name, "fluid_bufcompose")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Mix or copy a region of a sound file into a destination sound "
            "file, with per-buffer gains")
  halp_meta(uuid, "B527F2FD-B470-4F63-A962-45D73052315B")

  using Client
      = fluid::client::ClientWrapper<fluid::client::bufcompose::BufComposeClient>;

  struct ins
  {
    halp::lineedit<"Folder", ""> folder;
    halp::lineedit<"Source", "source.wav"> source;
    halp::lineedit<"Destination", "dest.wav"> destination;

    halp::toggle<"Process"> process;

    FluCoMa::fluid_long_param_ranged<Client, bufcompose::kOffset, 0, 100000000, 0>
        startFrame;
    FluCoMa::fluid_auto_toggle<Client, bufcompose::kNumFrames> allFrames;
    FluCoMa::fluid_long_param_ranged<Client, bufcompose::kNumFrames, 1, 100000000, 1>
        numFrames;
    FluCoMa::fluid_long_param_ranged<Client, bufcompose::kStartChan, 0, 64, 0> startChan;
    FluCoMa::fluid_auto_toggle<Client, bufcompose::kNChans> allChans;
    FluCoMa::fluid_long_param_ranged<Client, bufcompose::kNChans, 1, 64, 1> numChans;

    // Gains are unconstrained in flucoma; give them a usable bipolar UI range.
    FluCoMa::fluid_float_param_ranged<Client, bufcompose::kGain, -10., 10., 1.> gain;

    FluCoMa::fluid_long_param_ranged<Client, bufcompose::kDestOffset, 0, 100000000, 0>
        destStartFrame;
    FluCoMa::fluid_long_param_ranged<Client, bufcompose::kDestStartChan, 0, 64, 0>
        destStartChan;
    FluCoMa::fluid_float_param_ranged<Client, bufcompose::kDestGain, -10., 10., 0.>
        destGain;
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

    static std::function<void(BufCompose&)>
    work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;
      FluCoMa::FluidNRTHost<Client> host;
      FluCoMa::sync_params_into(host.params, in);

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](BufCompose& self) {
          self.outputs.status.value = msg;
        };
      };

      auto source = FluCoMa::pool().get(in.folder.value, in.source.value, rate);
      if(!source)
        return fail("Could not load source file");
      host.set_input_buffer<bufcompose::kSource>(std::move(source));

      if(in.destination.value.empty())
        return fail("No destination file name");

      // The destination is both read and written: preload its current
      // content from disk if the file exists, so composing accumulates into
      // it instead of starting from silence. Plain load_buffer on purpose:
      // going through pool().get would register a watch on our own output
      // file, which we do not want.
      auto dest = host.make_output_buffer<bufcompose::kDest>();
      if(auto existing
         = FluCoMa::load_buffer(in.folder.value, in.destination.value, rate))
      {
        std::shared_ptr<fluid::client::BufferAdaptor> base = existing;
        *dest = base;
      }

      auto result = host.run();
      if(!result.ok())
        return fail(result.message().c_str());

      if(!FluCoMa::pool().write_through(
             in.folder.value, in.destination.value, *dest, rate))
        return fail("Could not write destination file");

      // Prime the change tracker so our own get() doesn't retrigger a run.
      return [f = in.folder.value, s = in.source.value](BufCompose& self) {
        self.outputs.status.value = "OK";
        self.sourceRef.refresh(f, s);
        self.sourceRef.changed();
      };
    }
  } worker;

  // Only the source is tracked. Tracking the destination would self-retrigger
  // in an infinite loop: every run bumps its pool version via write_through.
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
