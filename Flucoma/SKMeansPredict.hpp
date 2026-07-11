#pragma once

/* Assigns incoming points to the nearest cluster of a trained spherical
 * k-means model, every tick. The model hot-reloads when its file changes on
 * disk — retrain with SKMeansFit while playing and predictions follow.
 *
 * Feed the input with a feature vector and get the winning cluster index and
 * the distance to every (normalized) cluster centroid.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/SKMeans.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <QFile>
#include <QFileInfo>

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

struct SKMeansPredict
{
  halp_meta(name, "SKMeansPredict")
  halp_meta(c_name, "fluid_skmeanspredict")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Assigns incoming points to the nearest cluster of a trained "
            "spherical k-means model")
  halp_meta(uuid, "63B386F6-00CF-46E1-B706-DF68C175734F")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Model file", "skmeans.json"> model_file;
    halp::val_port<"In", std::vector<float>> in;
  } inputs;

  struct outs
  {
    halp::val_port<"Cluster", int> cluster;
    halp::val_port<"Distances", std::vector<float>> distances;
    halp::val_port<"Status", std::string> status;
  } outputs;

  struct Model
  {
    fluid::algorithm::SKMeans skmeans;
    fluid::FluidTensor<double, 2> means;
    fluid::index dims{0};
    fluid::index k{0};
  };

  std::shared_ptr<const Model> model;
  int64_t model_mtime{-1};
  long poll_phase{0};
  bool load_inflight{false};
  double session_rate{48000.};

  fluid::FluidTensor<double, 1> in_buf;

  struct load_request
  {
    std::string model_path;
  };

  struct worker_t
  {
    std::function<void(std::shared_ptr<load_request>)> request;

    static std::function<void(SKMeansPredict&)>
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
          m->k = j.at("rows").get<fluid::index>();
          fluid::algorithm::from_json(j, m->skmeans);
          // Keep a copy of the centroids for cheap per-tick distances
          m->means = fluid::FluidTensor<double, 2>(m->k, m->dims);
          j.at("means").get_to(m->means);
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

      return [m = std::move(m), err = std::move(err)](SKMeansPredict& self) {
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

    for(fluid::index i = 0; i < model->dims; i++)
      in_buf(i) = inputs.in.value[(std::size_t)i];

    outputs.cluster.value = (int)model->skmeans.vq(in_buf);

    auto& od = outputs.distances.value;
    od.resize((std::size_t)model->k);
    for(fluid::index c = 0; c < model->k; c++)
    {
      double sum = 0;
      for(fluid::index i = 0; i < model->dims; i++)
      {
        const double d = in_buf(i) - model->means(c, i);
        sum += d * d;
      }
      od[(std::size_t)c] = (float)std::sqrt(sum);
    }
  }
};

}
