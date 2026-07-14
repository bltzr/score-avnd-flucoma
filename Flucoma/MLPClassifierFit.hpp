#pragma once

/* Trains a multi-layer perceptron classifier mapping an input dataset to a
 * label set (both from the working folder, points matched by id) and writes
 * the trained network + label encoder as flucoma-JSON (same composite layout
 * as upstream's MLPClassifier, so files interchange with Max/SC/Pd).
 * Training runs ONLY when explicitly triggered (Fit toggle) — models are
 * never retrained behind the user's back.
 */

#include "FluidBuffers.hpp"

// FluidDataSet must precede LabelSetEncoder.hpp (upstream missing include)
#include <flucoma/data/FluidDataSet.hpp>
#include <flucoma/algorithms/public/LabelSetEncoder.hpp>
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

struct MLPClassifierFit
{
  halp_meta(name, "MLPClassifierFit")
  halp_meta(c_name, "fluid_mlpclassifierfit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Trains a neural network classifier mapping an input dataset to "
            "a set of labels")
  halp_meta(uuid, "ED7EB927-01E2-4599-AD6E-C16DC93C539F")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Input dataset", "Folder", "json", "corpus.json"> source;
    halp::folder_combobox<"Labels file", "Folder", "json", "labels.json"> labels;
    halp::lineedit<"Model file", "mlpclassifier.json"> model;
    // Space-separated hidden layer sizes, e.g. "6 4"
    halp::lineedit<"Hidden layers", "3"> hidden;
    struct
    {
      // Same order as flucoma's activation function table; output layer
      // activation is fixed to Sigmoid upstream (one-hot classification)
      halp__enum_combobox("Activation", ReLU, Identity, Sigmoid, ReLU, Tanh)
    } activation;
    struct : halp::spinbox_i32<"Iterations", halp::range{1, 100000, 1000}>
    {
    } iterations;
    halp::hslider_f32<"Learning rate", halp::range{0.0001, 1., 0.01}> rate;
    halp::hslider_f32<"Momentum", halp::range{0., 0.99, 0.5}> momentum;
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

    static std::function<void(MLPClassifierFit&)>
    work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](MLPClassifierFit& self) {
          self.outputs.status.value = msg;
        };
      };

      auto load_json = [&](const std::string& name, nlohmann::json& j,
                           std::string& err) {
        const auto path = FluCoMa::resolve_in_folder(in.folder.value, name);
        QFile f(path);
        if(path.isEmpty() || !f.open(QIODevice::ReadOnly))
        {
          err = "Could not open file: " + name;
          return false;
        }
        try
        {
          j = nlohmann::json::parse(f.readAll().toStdString());
        }
        catch(const std::exception& e)
        {
          err = "Parse error (" + name + "): " + e.what();
          return false;
        }
        return true;
      };

      fluid::FluidDataSet<std::string, double, 1> src(1);
      fluid::FluidDataSet<std::string, std::string, 1> lbl(1);
      {
        nlohmann::json j;
        std::string err;
        if(!load_json(in.source.value, j, err))
          return fail(err);
        try
        {
          src = j.get<fluid::FluidDataSet<std::string, double, 1>>();
        }
        catch(const std::exception& e)
        {
          return fail(
              "Dataset parse error (" + in.source.value + "): " + e.what());
        }
        if(!load_json(in.labels.value, j, err))
          return fail(err);
        try
        {
          lbl = j.get<fluid::FluidDataSet<std::string, std::string, 1>>();
        }
        catch(const std::exception& e)
        {
          return fail(
              "Labels parse error (" + in.labels.value + "): " + e.what());
        }
      }
      if(src.size() < 2)
        return fail("Input dataset needs at least 2 points");
      if(lbl.size() == 0)
        return fail("Label set is empty");

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

      fluid::algorithm::LabelSetEncoder encoder;
      encoder.fit(lbl);
      if(encoder.numLabels() < 2)
        return fail("Need at least 2 distinct labels");

      // One-hot target matrix aligned to the input points by id
      fluid::FluidTensor<double, 2> one_hot(src.size(), encoder.numLabels());
      one_hot.fill(0);
      {
        auto ids = src.getIds();
        fluid::FluidTensor<std::string, 1> label(1);
        for(fluid::index r = 0; r < src.size(); r++)
        {
          if(!lbl.get(ids(r), label))
            return fail(
                "Id missing from the label set: " + std::string(ids(r)));
          encoder.encodeOneHot(label(0), one_hot.row(r));
        }
      }

      fluid::algorithm::MLP mlp;
      mlp.init(
          src.pointSize(), encoder.numLabels(), hidden,
          (fluid::index)in.activation.value, /* Sigmoid output: */ 1, -1);

      const auto batch
          = std::clamp<fluid::index>(in.batch.value, 1, src.size());

      fluid::algorithm::SGD sgd;
      const double error = sgd.train(
          mlp, src.getData(), one_hot, in.iterations.value, batch,
          in.rate.value, in.momentum.value, in.validation.value, -1);
      if(error < 0)
        return fail("Training diverged (NaN loss); model not saved");

      const auto model_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.model.value);
      {
        QFile out(model_path);
        if(model_path.isEmpty() || !out.open(QIODevice::WriteOnly))
          return fail("Could not write model file");
        // Same composite layout as upstream MLPClassifierData: interchanges
        // with fluid.mlpclassifier~ model files
        nlohmann::json j;
        j["mlp"] = mlp;
        j["labels"] = encoder;
        const auto text = j.dump(2);
        out.write(text.data(), (qint64)text.size());
      }

      char buf[64];
      snprintf(buf, sizeof(buf), "OK (loss: %.6g)", error);
      std::string status = buf;

      const int n = (int)src.size();
      return [n, error, status = std::move(status)](MLPClassifierFit& self) {
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
