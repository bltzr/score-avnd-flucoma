#pragma once

/* Runs incoming feature vectors through a trained MLP regressor from the
 * working folder, every tick. The model hot-reloads when its file changes on
 * disk — retrain with MLPRegressorFit while playing and predictions follow.
 * In: input-space vector; Out: regressed target-space vector.
 */

#include "FluidBuffers.hpp"

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

struct MLPRegressorPoint
{
  halp_meta(name, "MLPRegressorPoint")
  halp_meta(c_name, "fluid_mlpregressorpoint")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Regresses a feature vector through a trained neural network")
  halp_meta(uuid, "9D3D89AF-6F50-4163-9A0A-0864682FCD95")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Model file", "mlp.json"> model_file;
    halp::val_port<"In", std::vector<float>> input;
  } inputs;

  struct outs
  {
    halp::val_port<"Out", std::vector<float>> output;
    halp::val_port<"Status", std::string> status;
  } outputs;

  struct Model
  {
    fluid::algorithm::MLP mlp;
    fluid::index dims_in{0}, dims_out{0};
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

    static std::function<void(MLPRegressorPoint&)>
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
          const auto& layers = j.at("layers");
          if(layers.empty())
            throw std::runtime_error("model has no layers");
          m->dims_in = layers.front().at("rows").get<fluid::index>();
          m->dims_out = layers.back().at("cols").get<fluid::index>();
          fluid::algorithm::from_json(j, m->mlp);
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

      return [m = std::move(m), err = std::move(err)](MLPRegressorPoint& self) {
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
    if(out_buf.size() != model->dims_out)
      out_buf = fluid::FluidTensor<double, 1>(model->dims_out);

    for(fluid::index i = 0; i < model->dims_in; i++)
      in_buf(i) = inputs.input.value[(std::size_t)i];

    model->mlp.processFrame(in_buf, out_buf, 0, model->mlp.size());

    auto& o = outputs.output.value;
    o.resize((std::size_t)model->dims_out);
    for(fluid::index i = 0; i < model->dims_out; i++)
      o[(std::size_t)i] = (float)out_buf(i);
  }
};

}
