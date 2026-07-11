#pragma once

/* Runs incoming feature vectors through a trained MLP classifier from the
 * working folder, every tick. The model hot-reloads when its file changes on
 * disk — retrain with MLPClassifierFit while playing and predictions follow.
 * In: input-space vector; Out: winning label, its class index, and the
 * winning output activation as a confidence value.
 */

#include "FluidBuffers.hpp"

// FluidDataSet must precede LabelSetEncoder.hpp (upstream missing include)
#include <flucoma/data/FluidDataSet.hpp>
#include <flucoma/algorithms/public/LabelSetEncoder.hpp>
#include <flucoma/algorithms/public/MLP.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <QFile>
#include <QFileInfo>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

struct MLPClassifierPoint
{
  halp_meta(name, "MLPClassifierPoint")
  halp_meta(c_name, "fluid_mlpclassifierpoint")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Classifies a feature vector through a trained neural network")
  halp_meta(uuid, "4DDDD35A-754F-42B4-AE22-ABA81E58084C")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Model file", "mlpclassifier.json"> model_file;
    halp::val_port<"In", std::vector<float>> input;
  } inputs;

  struct outs
  {
    halp::val_port<"Label", std::string> label;
    halp::val_port<"Index", int> index;
    halp::val_port<"Confidence", float> confidence;
    halp::val_port<"Status", std::string> status;
  } outputs;

  struct Model
  {
    fluid::algorithm::MLP mlp;
    fluid::algorithm::LabelSetEncoder encoder;
    fluid::index dims_in{0}, num_labels{0};
  };

  std::shared_ptr<const Model> model;
  int64_t model_mtime{-1};
  long poll_phase{0};
  bool load_inflight{false};
  double session_rate{48000.};

  fluid::FluidTensor<double, 1> in_buf, out_buf;

  struct load_request
  {
    std::string path;
  };

  struct worker_t
  {
    std::function<void(std::shared_ptr<load_request>)> request;

    static std::function<void(MLPClassifierPoint&)>
    work(std::shared_ptr<load_request> rq)
    {
      auto m = std::make_shared<Model>();
      std::string err;

      QFile f(QString::fromStdString(rq->path));
      if(f.open(QIODevice::ReadOnly))
      {
        try
        {
          const auto j = nlohmann::json::parse(f.readAll().toStdString());
          // Upstream composite layout: {"mlp": {...}, "labels": {...}}
          const auto& jm = j.at("mlp");
          const auto& layers = jm.at("layers");
          if(layers.empty())
            throw std::runtime_error("model has no layers");
          m->dims_in = layers.front().at("rows").get<fluid::index>();
          m->num_labels = layers.back().at("cols").get<fluid::index>();
          fluid::algorithm::from_json(jm, m->mlp);
          fluid::algorithm::from_json(j.at("labels"), m->encoder);
          if(m->encoder.numLabels() != m->num_labels)
            throw std::runtime_error(
                "label count does not match network output size");
        }
        catch(const std::exception& e)
        {
          err = std::string("Model parse error: ") + e.what();
          m.reset();
        }
      }
      else
      {
        err = "Could not open model file";
        m.reset();
      }

      return
          [m = std::move(m), err = std::move(err)](MLPClassifierPoint& self) {
            self.load_inflight = false;
            if(m)
            {
              self.model = m;
              self.outputs.status.value = "OK";
            }
            else
              self.outputs.status.value = err;
          };
    }
  } worker;

  void prepare(halp::setup s)
  {
    session_rate = s.rate;
    model_mtime = -1;
    poll_phase = LONG_MAX / 2;
  }

  void operator()(int frames)
  {
    // Poll the model file ~2x/second; reload in the worker on change.
    poll_phase += frames > 0 ? frames : 64;
    if(poll_phase >= (long)(session_rate / 2) && !load_inflight)
    {
      poll_phase = 0;
      const auto p = FluCoMa::resolve_in_folder(
          inputs.folder.value, inputs.model_file.value);
      QFileInfo fi{p};
      const int64_t m = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;
      if(m != model_mtime)
      {
        model_mtime = m;
        if(m != 0)
        {
          load_inflight = true;
          auto rq = std::make_shared<load_request>();
          rq->path = p.toStdString();
          worker.request(std::move(rq));
        }
        else
          model.reset();
      }
    }

    if(!model || inputs.input.value.empty())
      return;
    if((fluid::index)inputs.input.value.size() != model->dims_in)
    {
      outputs.status.value = "Input size does not match model dimensions";
      return;
    }

    if(in_buf.size() != model->dims_in)
      in_buf = fluid::FluidTensor<double, 1>(model->dims_in);
    if(out_buf.size() != model->num_labels)
      out_buf = fluid::FluidTensor<double, 1>(model->num_labels);

    for(fluid::index i = 0; i < model->dims_in; i++)
      in_buf(i) = inputs.input.value[(std::size_t)i];

    model->mlp.processFrame(in_buf, out_buf, 0, model->mlp.size());

    // Argmax over the one-hot activations (same convention as upstream's
    // LabelSetEncoder::decodeOneHot), keeping the winning value as confidence
    fluid::index best = 0;
    double best_val = 0.;
    for(fluid::index i = 0; i < model->num_labels; i++)
    {
      if(out_buf(i) > best_val)
      {
        best = i;
        best_val = out_buf(i);
      }
    }

    outputs.label.value = model->encoder.decodeIndex(best);
    outputs.index.value = (int)best;
    outputs.confidence.value = (float)best_val;
  }
};

}
