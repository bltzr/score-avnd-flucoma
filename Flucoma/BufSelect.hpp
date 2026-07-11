#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/nrt/BufSelectClient.hpp>

#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{
// bufselect's parameter enum is anonymous; alias its namespace instead of a
// `using Idx = ...ParamIndex` enum alias.
namespace bufselect = fluid::client::bufselect;

/* Offline selection of arbitrary frames and channels of a sound file read
 * from the working folder into a destination file. Runs in a worker thread
 * when playback starts and whenever "Process" is toggled on.
 *
 * The "indices" and "channels" LongArrayT parameters are not exposed yet
 * (avnd has no port type for flucoma long arrays): both keep their {-1}
 * default, which selects all frames and all channels — i.e. a copy.
 */
struct BufSelect
{
  halp_meta(name, "BufSelect")
  halp_meta(c_name, "fluid_bufselect")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Select frames and channels of a sound file into a destination "
            "sound file")
  halp_meta(uuid, "5FBD956E-E62A-462E-8C0D-E65792044238")

  using Client
      = fluid::client::ClientWrapper<fluid::client::bufselect::BufSelectClient>;

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Source", "source.wav"> source;
    halp::lineedit<"Destination", "out.wav"> destination;

    halp::toggle<"Process"> process;

    // kIndices / kChannels (LongArrayT) skipped: defaults ({-1}) select all
    // frames / channels.
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

    static std::function<void(BufSelect&)>
    work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;
      FluCoMa::FluidNRTHost<Client> host;
      FluCoMa::sync_params_into(host.params, in);

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](BufSelect& self) {
          self.outputs.status.value = msg;
        };
      };

      auto source = FluCoMa::pool().get(in.folder.value, in.source.value, rate);
      if(!source)
        return fail("Could not load source file");
      host.set_input_buffer<bufselect::kSource>(std::move(source));

      if(in.destination.value.empty())
        return fail("No destination file name");

      auto dest = host.make_output_buffer<bufselect::kDest>();

      auto result = host.run();
      if(!result.ok())
        return fail(result.message().c_str());

      if(!FluCoMa::pool().write_through(
             in.folder.value, in.destination.value, *dest, rate))
        return fail("Could not write destination file");

      // Prime the change tracker so our own get() doesn't retrigger a run.
      return [f = in.folder.value, s = in.source.value](BufSelect& self) {
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
