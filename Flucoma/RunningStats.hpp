#pragma once

#include "FluidBridge.hpp"

#include <flucoma/clients/rt/RunningStatsClient.hpp>

#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <vector>

namespace Flucoma
{

struct RunningStats
{
  halp_meta(name, "RunningStats")
  halp_meta(c_name, "fluid_runningstats")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(
      description,
      "Running mean and sample standard deviation of a vector input stream")
  halp_meta(uuid, "C8ADC874-41FA-47E9-AE93-177E8C396A87")

  // ControlIn + ControlOut client: vector in -> two vectors out, no audio.
  using Client = fluid::client::RunningStatsClient;

  struct ins
  {
    halp::val_port<"In", std::vector<float>> input;

    FluCoMa::fluid_long_param_ranged<Client, 0, 2, 65536, 2> history;
  } inputs;

  struct outs
  {
    halp::val_port<"Mean", std::vector<float>> mean;
    halp::val_port<"Std Dev", std::vector<float>> stddev;
  } outputs;

  FluCoMa::FluidRTHost<Client> host;

  // Scratch buffers: the client consumes / produces FluidTensorView<double,1>,
  // while the ports carry std::vector<float>.
  std::vector<double> in_buf, mean_buf, stddev_buf;

  void operator()()
  {
    host.sync_params(inputs);

    const std::size_t n = inputs.input.value.size();
    if(n == 0)
    {
      outputs.mean.value.clear();
      outputs.stddev.value.clear();
      return;
    }

    in_buf.assign(inputs.input.value.begin(), inputs.input.value.end());
    mean_buf.assign(n, 0.);
    stddev_buf.assign(n, 0.);

    // No audio: build the tensor views over local double buffers directly,
    // mirroring what FluidRTHost::process_control does for audio clients.
    std::vector<fluid::FluidTensorView<double, 1>> ins, outs;
    ins.emplace_back(in_buf.data(), 0, static_cast<fluid::index>(n));
    outs.emplace_back(mean_buf.data(), 0, static_cast<fluid::index>(n));
    outs.emplace_back(stddev_buf.data(), 0, static_cast<fluid::index>(n));

    host.client.process(ins, outs, host.context);

    outputs.mean.value.assign(mean_buf.begin(), mean_buf.end());
    outputs.stddev.value.assign(stddev_buf.begin(), stddev_buf.end());
  }
};

}
