#pragma once

/* Applies a fitted standardization to incoming points, every tick.
 * The model hot-reloads when its file changes on disk — refit with
 * StandardizeFit while playing and the scaling follows.
 *
 * Feed the input with a feature vector and get the standardized vector out;
 * the Inverse toggle maps standardized values back to the original range.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/Standardization.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <QFile>
#include <QFileInfo>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

struct StandardizePoint
{
  halp_meta(name, "StandardizePoint")
  halp_meta(c_name, "fluid_standardizepoint")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Scales incoming points with a fitted standardization")
  halp_meta(uuid, "F2E0FC57-3572-4E83-9ECD-9FF967006320")

  struct ins
  {
    halp::lineedit<"Folder", ""> folder;
    halp::lineedit<"Model file", "standardize.json"> model_file;
    halp::toggle<"Inverse"> inverse;
    halp::val_port<"In", std::vector<float>> in;
  } inputs;

  struct outs
  {
    halp::val_port<"Out", std::vector<float>> out;
    halp::val_port<"Status", std::string> status;
  } outputs;

  struct Model
  {
    fluid::algorithm::Standardization scaler;
    fluid::index dims{0};
  };

  std::shared_ptr<const Model> model;
  int64_t model_mtime{-1};
  long poll_phase{0};
  bool load_inflight{false};
  double session_rate{48000.};

  fluid::FluidTensor<double, 1> in_buf, out_buf;

  struct load_request
  {
    std::string model_path;
  };

  struct worker_t
  {
    std::function<void(std::shared_ptr<load_request>)> request;

    static std::function<void(StandardizePoint&)>
    work(std::shared_ptr<load_request> rq)
    {
      auto m = std::make_shared<Model>();
      std::string err;

      QFile f(QString::fromStdString(rq->model_path));
      if(f.open(QIODevice::ReadOnly))
      {
        try
        {
          const auto j = nlohmann::json::parse(f.readAll().toStdString());
          m->dims = j.at("cols").get<fluid::index>();
          fluid::algorithm::from_json(j, m->scaler);
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

      return [m = std::move(m), err = std::move(err)](StandardizePoint& self) {
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
    model_mtime = -1; // force reload on next tick
    poll_phase = LONG_MAX / 2;
  }

  static int64_t mtime_of(const QString& p)
  {
    QFileInfo f{p};
    return f.exists() ? f.lastModified().toMSecsSinceEpoch() : 0;
  }

  void operator()(int frames)
  {
    // Poll the model file ~2x/second; reload in the worker on change.
    poll_phase += frames > 0 ? frames : 64;
    if(poll_phase >= (long)(session_rate / 2) && !load_inflight)
    {
      poll_phase = 0;
      const auto mp = FluCoMa::resolve_in_folder(
          inputs.folder.value, inputs.model_file.value);
      const auto mm = mtime_of(mp);
      if(mm != model_mtime)
      {
        model_mtime = mm;
        if(mm != 0)
        {
          load_inflight = true;
          auto rq = std::make_shared<load_request>();
          rq->model_path = mp.toStdString();
          worker.request(std::move(rq));
        }
        else
          model.reset();
      }
    }

    if(!model || inputs.in.value.empty())
      return;
    if((fluid::index)inputs.in.value.size() != model->dims)
    {
      outputs.status.value = "Input size does not match model dimensions";
      return;
    }

    if(in_buf.size() != model->dims)
      in_buf = fluid::FluidTensor<double, 1>(model->dims);
    if(out_buf.size() != model->dims)
      out_buf = fluid::FluidTensor<double, 1>(model->dims);

    for(fluid::index i = 0; i < model->dims; i++)
      in_buf(i) = inputs.in.value[(std::size_t)i];

    model->scaler.processFrame(in_buf, out_buf, inputs.inverse.value);

    auto& o = outputs.out.value;
    o.resize((std::size_t)model->dims);
    for(fluid::index i = 0; i < model->dims; i++)
      o[(std::size_t)i] = (float)out_buf(i);
  }
};

}
