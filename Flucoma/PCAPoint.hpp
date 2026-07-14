#pragma once

/* Projects incoming points onto the principal components of a fitted PCA,
 * every tick. The model hot-reloads when its file changes on disk — refit
 * with PCAFit while playing and the projection follows.
 *
 * Feed the input with a feature vector and get the first K principal
 * components out (K clamped to what the model provides).
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/util/AlgorithmUtils.hpp> // for fluid::algorithm::epsilon, used by PCA.hpp

#include <flucoma/algorithms/public/PCA.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/file_port.hpp>
#include <halp/folder_combobox.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

struct PCAPoint
{
  halp_meta(name, "PCAPoint")
  halp_meta(c_name, "fluid_pcapoint")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Projects incoming points onto a fitted PCA for dimensionality "
            "reduction")
  halp_meta(uuid, "4DDA2225-0CC6-4F04-BAB3-012E0DFCD1F6")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Model file", "Folder", "json", "pca.json"> model_file;
    struct : halp::spinbox_i32<"Components", halp::range{1, 64, 2}>
    {
    } k;
    halp::val_port<"In", std::vector<float>> in;
  } inputs;

  struct outs
  {
    halp::val_port<"Out", std::vector<float>> out;
    halp::val_port<"Status", std::string> status;
  } outputs;

  struct Model
  {
    fluid::algorithm::PCA pca;
    fluid::index dims{0};       // input dimensions
    fluid::index components{0}; // components available in the model
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

    static std::function<void(PCAPoint&)>
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
          m->dims = j.at("rows").get<fluid::index>();
          m->components = j.at("cols").get<fluid::index>();
          fluid::algorithm::from_json(j, m->pca);
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

      return [m = std::move(m), err = std::move(err)](PCAPoint& self) {
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

    const auto k
        = std::clamp<fluid::index>(inputs.k.value, 1, model->components);

    if(in_buf.size() != model->dims)
      in_buf = fluid::FluidTensor<double, 1>(model->dims);
    if(out_buf.size() != k)
      out_buf = fluid::FluidTensor<double, 1>(k);

    // NB: PCA::processFrame writes to its input buffer, so refill each tick.
    for(fluid::index i = 0; i < model->dims; i++)
      in_buf(i) = inputs.in.value[(std::size_t)i];

    model->pca.processFrame(in_buf, out_buf, k, false);

    auto& o = outputs.out.value;
    o.resize((std::size_t)k);
    for(fluid::index i = 0; i < k; i++)
      o[(std::size_t)i] = (float)out_buf(i);
  }
};

}
