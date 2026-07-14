#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/rt/ChromaClient.hpp>

#include <halp/folder_combobox.hpp>
#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{

/* Offline chroma analysis of a sound file read from the working folder.
 * Runs in a worker thread when playback starts and whenever "Process" is
 * toggled on. The feature buffer (one channel per chroma bin, one frame per
 * analysis hop) is written to <folder>/<features file> as a float32 WAV whose
 * sample rate encodes the analysis hop rate.
 */
struct BufChroma
{
  halp_meta(name, "BufChroma")
  halp_meta(c_name, "fluid_bufchroma")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description, "Energies at chroma bins of a sound file")
  halp_meta(uuid, "1A943BBC-D19B-4213-9354-70D66AC776E2")

  // NRT parameter sets are composed by makeNRTParams: RT enum indices do not
  // apply, so every parameter is looked up by name with FluCoMa::pidx.
  using Client = fluid::client::NRTChromaClient;

  static constexpr int max_chroma = 96;

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Source", "Folder", "wav aif aiff flac mp3 m4a ogg", "source.wav"> source;
    halp::lineedit<"Features file", "chroma.wav"> featuresFile;

    halp::toggle<"Process"> process;

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

    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("padding")> padding;

    FluCoMa::fluid_long_max_param<
        Client, FluCoMa::pidx<Client>("numChroma"), 12, max_chroma>
        numChroma;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("ref")> ref;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("normalize")>
        normalize;
    FluCoMa::fluid_param_for<Client, FluCoMa::pidx<Client>("minFreq")> minFreq;
    // -1 = automatic (Nyquist); the toggle forces -1 and overrides the slider
    FluCoMa::fluid_auto_toggle<Client, FluCoMa::pidx<Client>("maxFreq")>
        maxFreqAuto;
    FluCoMa::fluid_float_param_ranged<
        Client, FluCoMa::pidx<Client>("maxFreq"), 20., 22000., 10000.>
        maxFreq;

    FluCoMa::fluid_fft_window<Client, FluCoMa::pidx<Client>("fftSettings")>
        window;
    FluCoMa::fluid_fft_hop<Client, FluCoMa::pidx<Client>("fftSettings")> hop;
    FluCoMa::fluid_fft_size<Client, FluCoMa::pidx<Client>("fftSettings")> fft;
  } inputs;

  struct outs
  {
    halp::val_port<"Status", std::string> status;
    // Number of analysis frames written to the features buffer.
    halp::val_port<"Frames", int> frames;
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

    static std::function<void(BufChroma&)>
    work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;
      FluCoMa::FluidNRTHost<Client> host;
      FluCoMa::sync_params_into(host.params, in);

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](BufChroma& self) {
          self.outputs.status.value = msg;
        };
      };

      auto source = FluCoMa::pool().get(in.folder.value, in.source.value, rate);
      if(!source)
        return fail("Could not load source file");
      host.set_input_buffer<FluCoMa::pidx<Client>("source")>(std::move(source));

      auto features
          = host.make_output_buffer<FluCoMa::pidx<Client>("features")>();

      auto result = host.run();
      if(!result.ok())
        return fail(result.message().c_str());

      int frames = 0;
      {
        fluid::client::BufferAdaptor::ReadAccess acc(features.get());
        if(acc.exists() && acc.valid())
          frames = static_cast<int>(acc.numFrames());
      }

      if(!FluCoMa::pool().write_through(
             in.folder.value, in.featuresFile.value, *features, rate))
        return fail("Could not save features file");

      // Prime the change tracker so our own get() doesn't retrigger a run.
      return [frames, f = in.folder.value, s = in.source.value](BufChroma& self) {
        self.outputs.frames.value = frames;
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
