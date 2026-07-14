#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/rt/AmpSliceClient.hpp>

#include <halp/folder_combobox.hpp>
#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

/* Offline absolute-amplitude envelope slicer over a sound file from the
 * working folder. Slice positions go to the "Slices (s)" value output
 * (seconds) and to <folder>/<indices file> as sample positions in a float32
 * WAV — the FluCoMa interchange format.
 */
struct BufAmpSlice
{
  halp_meta(name, "BufAmpSlice")
  halp_meta(c_name, "fluid_bufampslice")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Absolute amplitude envelope slicer: segments a sound file and "
            "outputs the slice positions")
  halp_meta(uuid, "36C8F2C2-5F8B-487D-8CF4-D18A734CA95B")

  // The non-threaded NRT adaptor is itself a complete client; our worker
  // provides the threading.
  using Client = fluid::client::NRTAmpSliceClient;

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Source", "Folder", "wav aif aiff flac mp3 m4a ogg", "source.wav"> source;
    halp::lineedit<"Indices file", "indices.wav"> indicesFile;

    halp::toggle<"Process"> process;

    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("fastRampUp")>
        fastRampUp;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("fastRampDown")>
        fastRampDown;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("slowRampUp")>
        slowRampUp;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("slowRampDown")>
        slowRampDown;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("onThreshold")>
        onThreshold;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("offThreshold")>
        offThreshold;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("floor")> floor;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("minSliceLength")>
        minSliceLength;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("highPassFreq")>
        highPassFreq;

    FluCoMa::fluid_long_param_ranged<
        Client, FluCoMa::pidx<Client>("startFrame"), 0, 100000000, 0>
        startFrame;
    FluCoMa::fluid_auto_toggle<Client, FluCoMa::pidx<Client>("numFrames")>
        allFrames;
    FluCoMa::fluid_long_param_ranged<
        Client, FluCoMa::pidx<Client>("numFrames"), 1, 100000000, 1>
        numFrames;
    FluCoMa::fluid_long_param_ranged<
        Client, FluCoMa::pidx<Client>("startChan"), 0, 64, 0>
        startChan;
    FluCoMa::fluid_auto_toggle<Client, FluCoMa::pidx<Client>("numChans")>
        allChans;
    FluCoMa::fluid_long_param_ranged<
        Client, FluCoMa::pidx<Client>("numChans"), 1, 64, 1>
        numChans;
  } inputs;

  struct outs
  {
    // Slice onset positions, in seconds at the source file's sample rate.
    halp::val_port<"Slices (s)", std::vector<float>> slices;
    halp::val_port<"Status", std::string> status;
  } outputs;

  double session_rate{48000.};
  bool last_process{false};

  struct worker_t
  {
    // The request payload must stay small (oscr wraps it in a 128-byte
    // SmallFun), so the inputs snapshot travels on the heap.
    std::function<void(std::shared_ptr<ins>, double)> request;

    static std::function<void(BufAmpSlice&)>
    work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;
      FluCoMa::FluidNRTHost<Client> host;
      FluCoMa::sync_params_into(host.params, in);

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](BufAmpSlice& self) {
          self.outputs.status.value = msg;
        };
      };

      auto source = FluCoMa::pool().get(in.folder.value, in.source.value, rate);
      if(!source)
        return fail("Could not load source file");

      // Slice indices come back in samples at the source rate; capture it
      // before the buffer is moved into the parameter set.
      double source_rate = rate;
      {
        fluid::client::BufferAdaptor::ReadAccess acc(source.get());
        if(acc.exists() && acc.valid() && acc.sampleRate() > 0)
          source_rate = acc.sampleRate();
      }
      host.set_input_buffer<FluCoMa::pidx<Client>("source")>(std::move(source));

      auto indices = host.make_output_buffer<FluCoMa::pidx<Client>("indices")>();

      auto result = host.run();
      if(!result.ok())
        return fail(result.message().c_str());

      std::vector<float> out;
      {
        fluid::client::BufferAdaptor::ReadAccess acc(indices.get());
        if(acc.exists() && acc.valid())
          for(fluid::index i = 0; i < acc.numFrames(); ++i)
            for(fluid::index c = 0; c < acc.numChans(); ++c)
            {
              const float v = acc.samps(c)(i);
              if(v >= 0.f) // -1 marks "no slice found"
                out.push_back(static_cast<float>(v / source_rate));
            }
      }

      if(!in.indicesFile.value.empty())
        FluCoMa::pool().write_through(
            in.folder.value, in.indicesFile.value, *indices, rate);

      // Prime the change tracker so our own get() doesn't retrigger a run.
      return [out = std::move(out), f = in.folder.value,
              s = in.source.value](BufAmpSlice& self) {
        self.outputs.slices.value = out;
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
