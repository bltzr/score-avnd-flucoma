#pragma once

#include "FluidBufferPool.hpp"

#include <flucoma/clients/nrt/BufSTFTClient.hpp>

#include <halp/folder_combobox.hpp>
#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{

/* Offline STFT / inverse STFT of a sound file to/from magnitude and phase
 * buffers in the working folder.
 * Forward: source -> magnitude + phase files.
 * Inverse: magnitude + phase files -> resynthesis file.
 */
struct BufSTFT
{
  halp_meta(name, "BufSTFT")
  halp_meta(c_name, "fluid_bufstft")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Offline STFT of a sound file to magnitude/phase buffers, and back")
  halp_meta(uuid, "AC681124-E6C1-4C33-B297-F59420B49CA6")

  using Client
      = fluid::client::ClientWrapper<fluid::client::bufstft::BufferSTFTClient>;
  using Idx = fluid::client::bufstft::BufferSTFTParamIndex;

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Source", "Folder", "wav aif aiff flac mp3 m4a ogg", "source.wav"> source;
    halp::lineedit<"Magnitude file", "stft_mag.wav"> magFile;
    halp::lineedit<"Phase file", "stft_phase.wav"> phaseFile;
    halp::lineedit<"Resynth file", ""> resynthFile;

    halp::toggle<"Process"> process;

    FluCoMa::fluid_param_for<Client, Idx::kInvert> inverse;
    FluCoMa::fluid_param_for<Client, Idx::kPadding> padding;

    FluCoMa::fluid_long_param_ranged<Client, Idx::kOffset, 0, 100000000, 0>
        startFrame;
    FluCoMa::fluid_auto_toggle<Client, Idx::kNumFrames> allFrames;
    FluCoMa::fluid_long_param_ranged<Client, Idx::kNumFrames, 1, 100000000, 1>
        numFrames;
    FluCoMa::fluid_long_param_ranged<Client, Idx::kStartChan, 0, 64, 0> startChan;

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

    static std::function<void(BufSTFT&)> work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;
      FluCoMa::FluidNRTHost<Client> host;
      FluCoMa::sync_params_into(host.params, in);

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](BufSTFT& self) {
          self.outputs.status.value = msg;
        };
      };

      const bool inverse = in.inverse.value != 0;

      auto mag = host.make_output_buffer<Idx::kMag>();
      auto phase = host.make_output_buffer<Idx::kPhase>();
      auto resynth = host.make_output_buffer<Idx::kResynth>();

      if(inverse)
      {
        // magnitude + phase are inputs here: preload them into the buffers
        auto m = FluCoMa::pool().get(in.folder.value, in.magFile.value, rate);
        auto p = FluCoMa::pool().get(in.folder.value, in.phaseFile.value, rate);
        if(!m || !p)
          return fail("Could not load magnitude/phase files");
        // MemoryBufferAdaptor is only assignable from a shared_ptr lvalue
        std::shared_ptr<fluid::client::BufferAdaptor> mb = m, pb = p;
        *mag = mb;
        *phase = pb;
      }
      else
      {
        auto source
            = FluCoMa::pool().get(in.folder.value, in.source.value, rate);
        if(!source)
          return fail("Could not load source file");
        host.set_input_buffer<Idx::kSource>(std::move(source));
      }

      auto result = host.run();
      if(!result.ok())
        return fail(result.message().c_str());

      if(inverse)
      {
        if(!in.resynthFile.value.empty())
          FluCoMa::pool().write_through(
              in.folder.value, in.resynthFile.value, *resynth, rate);
      }
      else
      {
        if(!in.magFile.value.empty())
          FluCoMa::pool().write_through(
              in.folder.value, in.magFile.value, *mag, rate);
        if(!in.phaseFile.value.empty())
          FluCoMa::pool().write_through(
              in.folder.value, in.phaseFile.value, *phase, rate);
      }

      return [](BufSTFT& self) { self.outputs.status.value = "OK"; };
    }
  } worker;

  void prepare(halp::setup s) { session_rate = s.rate; }

  void operator()(int)
  {
    const bool p = inputs.process.value;
    if(p && !last_process)
      worker.request(std::make_shared<ins>(inputs), session_rate);
    last_process = p;
  }
};

}
