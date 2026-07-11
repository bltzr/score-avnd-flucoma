#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/rt/NoveltySliceClient.hpp>

#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

/* Offline novelty-based slicer (spectrum, MFCC, chroma, pitch or loudness
 * feature) over a sound file from the working folder. Slice positions go to
 * the "Slices (s)" value output (seconds) and to <folder>/<indices file> as
 * sample positions in a float32 WAV — the FluCoMa interchange format.
 */
struct BufNoveltySlice
{
  halp_meta(name, "BufNoveltySlice")
  halp_meta(c_name, "fluid_bufnoveltyslice")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Novelty-based slicer (spectrum, MFCC, chroma, pitch or loudness "
            "feature): segments a sound file and outputs the slice positions")
  halp_meta(uuid, "1438380A-05BC-4BE0-8C94-64179029ABF0")

  // The non-threaded NRT adaptor is itself a complete client; our worker
  // provides the threading.
  using Client = fluid::client::NRTNoveltySliceClient;

  struct ins
  {
    halp::lineedit<"Folder", ""> folder;
    halp::lineedit<"Source", "source.wav"> source;
    halp::lineedit<"Indices file", "indices.wav"> indicesFile;

    halp::toggle<"Process"> process;

    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("algorithm")>
        algorithm;
    // LongRuntimeMax params: defaults copied from the RT defineParameters call.
    FluCoMa::fluid_long_max_param<
        Client, FluCoMa::pidx<Client>("kernelSize"), 3, 101>
        kernelSize;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("threshold")>
        threshold;
    FluCoMa::fluid_long_max_param<
        Client, FluCoMa::pidx<Client>("filterSize"), 1, 100>
        filterSize;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("minSliceLength")>
        minSliceLength;

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

    FluCoMa::fluid_fft_window<Client, FluCoMa::pidx<Client>("fftSettings")>
        window;
    FluCoMa::fluid_fft_hop<Client, FluCoMa::pidx<Client>("fftSettings")> hop;
    FluCoMa::fluid_fft_size<Client, FluCoMa::pidx<Client>("fftSettings")> fft;
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

    static std::function<void(BufNoveltySlice&)>
    work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;
      FluCoMa::FluidNRTHost<Client> host;
      FluCoMa::sync_params_into(host.params, in);

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](BufNoveltySlice& self) {
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
              s = in.source.value](BufNoveltySlice& self) {
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
