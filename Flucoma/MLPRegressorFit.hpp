#pragma once

/* Trains a multi-layer perceptron regressor mapping an input dataset to a
 * target dataset (both from the working folder, points matched by id) and
 * writes the trained network as flucoma-JSON. Training runs ONLY when
 * explicitly triggered (Fit toggle) — models are never retrained behind the
 * user's back.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/MLP.hpp>
#include <flucoma/algorithms/public/SGD.hpp>
#include <flucoma/data/FluidDataSet.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/folder_combobox.hpp>
#include <halp/controls.enums.hpp>
#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <QFile>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace Flucoma
{

struct MLPRegressorFit
{
  halp_meta(name, "MLPRegressorFit")
  halp_meta(c_name, "fluid_mlpregressorfit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Trains a neural network regressor mapping an input dataset to "
            "a target dataset")
  halp_meta(uuid, "01269682-682D-48FD-A2BB-541195D9E466")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Input dataset", "Folder", "json", "in.json"> source;
    halp::folder_combobox<"Target dataset", "Folder", "json", "out.json"> target;
    halp::lineedit<"Model file", "mlp.json"> model;
    // Space-separated hidden layer sizes, e.g. "6 4"
    halp::lineedit<"Hidden layers", "3"> hidden;
    struct
    {
      // Same order as flucoma's activation function table
      halp__enum_combobox("Activation", ReLU, Identity, Sigmoid, ReLU, Tanh)
    } activation;
    struct
    {
      halp__enum_combobox(
          "Output activation", Identity, Identity, Sigmoid, ReLU, Tanh)
    } output_activation;
    struct : halp::spinbox_i32<"Iterations", halp::range{1, 100000, 1000}>
    {
    } iterations;
    halp::hslider_f32<"Learning rate", halp::range{0.0001, 1., 0.01}> rate;
    halp::hslider_f32<"Momentum", halp::range{0., 0.99, 0.9}> momentum;
    struct : halp::spinbox_i32<"Batch size", halp::range{1, 8192, 50}>
    {
    } batch;
    halp::hslider_f32<"Validation", halp::range{0., 0.9, 0.2}> validation;
    halp::toggle<"Fit"> fit;
  } inputs;

  struct outs
  {
    halp::val_port<"Points", int> points;
    halp::val_port<"Loss", float> loss;
    halp::val_port<"Status", std::string> status;
  } outputs;

  bool last_fit{false};

  struct worker_t
  {
    std::function<void(std::shared_ptr<ins>)> request;

    static std::function<void(MLPRegressorFit&)> work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](MLPRegressorFit& self) {
          self.outputs.status.value = msg;
        };
      };

      auto load = [&](const std::string& name,
                      fluid::FluidDataSet<std::string, double, 1>& ds,
                      std::string& err) {
        const auto path = FluCoMa::resolve_in_folder(in.folder.value, name);
        QFile f(path);
        if(path.isEmpty() || !f.open(QIODevice::ReadOnly))
        {
          err = "Could not open dataset file: " + name;
          return false;
        }
        try
        {
          const auto j = nlohmann::json::parse(f.readAll().toStdString());
          ds = j.get<fluid::FluidDataSet<std::string, double, 1>>();
        }
        catch(const std::exception& e)
        {
          err = "Dataset parse error (" + name + "): " + e.what();
          return false;
        }
        return true;
      };

      fluid::FluidDataSet<std::string, double, 1> src(1), tgt(1);
      std::string err;
      if(!load(in.source.value, src, err))
        return fail(err);
      if(!load(in.target.value, tgt, err))
        return fail(err);
      if(src.size() < 2)
        return fail("Input dataset needs at least 2 points");
      if(tgt.size() == 0)
        return fail("Target dataset is empty");

      // Hidden layer sizes: space-separated ints; empty means no hidden layer
      fluid::FluidTensor<fluid::index, 1> hidden;
      {
        std::vector<fluid::index> sizes;
        std::istringstream iss(in.hidden.value);
        long long v{};
        while(iss >> v)
        {
          if(v < 1 || v > 8192)
            return fail("Invalid hidden layer size");
          sizes.push_back((fluid::index)v);
        }
        if(!iss.eof())
          return fail("Could not parse hidden layer sizes");
        hidden = fluid::FluidTensor<fluid::index, 1>(
            (fluid::index)sizes.size());
        for(std::size_t i = 0; i < sizes.size(); i++)
          hidden((fluid::index)i) = sizes[i];
      }

      // Align target points to the input points by id
      fluid::FluidTensor<double, 2> aligned(src.size(), tgt.pointSize());
      {
        auto ids = src.getIds();
        for(fluid::index r = 0; r < src.size(); r++)
          if(!tgt.get(ids(r), aligned.row(r)))
            return fail(
                "Id missing from the target dataset: " + std::string(ids(r)));
      }

      fluid::algorithm::MLP mlp;
      mlp.init(
          src.pointSize(), tgt.pointSize(), hidden,
          (fluid::index)in.activation.value,
          (fluid::index)in.output_activation.value, -1);

      const auto batch
          = std::clamp<fluid::index>(in.batch.value, 1, src.size());

      fluid::algorithm::SGD sgd;
      const double error = sgd.train(
          mlp, src.getData(), aligned, in.iterations.value, batch,
          in.rate.value, in.momentum.value, in.validation.value, -1);
      if(error < 0)
        return fail("Training diverged (NaN loss); model not saved");

      const auto model_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.model.value);
      {
        QFile out(model_path);
        if(model_path.isEmpty() || !out.open(QIODevice::WriteOnly))
          return fail("Could not write model file");
        nlohmann::json j = mlp;
        const auto text = j.dump(2);
        out.write(text.data(), (qint64)text.size());
      }

      char buf[64];
      snprintf(buf, sizeof(buf), "OK (loss: %.6g)", error);
      std::string status = buf;

      const int n = (int)src.size();
      return [n, error, status = std::move(status)](MLPRegressorFit& self) {
        self.outputs.points.value = n;
        self.outputs.loss.value = (float)error;
        self.outputs.status.value = status;
      };
    }
  } worker;

  void operator()(int)
  {
    const bool p = inputs.fit.value;
    if(p && !last_fit)
      worker.request(std::make_shared<ins>(inputs));
    last_fit = p;
  }
};

}
