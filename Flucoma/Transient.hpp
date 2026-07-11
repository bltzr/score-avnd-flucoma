#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/TransientClient.hpp>

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

namespace Flucoma
{

struct Transient
{
  halp_meta(name, "Transient")
  halp_meta(c_name, "fluid_transient")
  halp_meta(category, "Audio/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Transient extraction: separates clicks and attacks from the "
            "residual signal")
  halp_meta(uuid, "A96AB80E-A807-462A-B66B-2F28E5277C74")

  using Client = fluid::client::RTTransientClient;
  using Idx = fluid::client::transient::TransientParamIndex;

  struct ins
  {
    halp::fixed_audio_bus<"In", double, 1> audio;

    FluCoMa::fluid_param_for<Client, Idx::kOrder> order;
    FluCoMa::fluid_param_for<Client, Idx::kBlockSize> blockSize;
    FluCoMa::fluid_param_for<Client, Idx::kPadding> padding;
    FluCoMa::fluid_param_for<Client, Idx::kSkew> skew;
    FluCoMa::fluid_param_for<Client, Idx::kThreshFwd> threshFwd;
    FluCoMa::fluid_param_for<Client, Idx::kThreshBack> threshBack;
    FluCoMa::fluid_param_for<Client, Idx::kWinSize> windowSize;
    FluCoMa::fluid_param_for<Client, Idx::kDebounce> clumpLength;
  } inputs;

  struct outs
  {
    halp::fixed_audio_bus<"Transients", double, 1> transients;
    halp::fixed_audio_bus<"Residual", double, 1> residual;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  void prepare(halp::setup s) { host.prepare(s.rate, s.frames); }

  void operator()(int frames)
  {
    host.sync_params(inputs);

    double* out_chans[2]{
        outputs.transients.samples[0], outputs.residual.samples[0]};
    host.process(inputs.audio.samples, 1, out_chans, 2, frames);
  }
};

}
