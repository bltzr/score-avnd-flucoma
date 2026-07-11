#pragma once

/* Nearest-neighbour queries against a fitted KD-tree from the working folder.
 * The tree (and the corpus meta with slice times) hot-reload when their files
 * change on disk — retrain with KDTreeFit while playing and queries follow.
 *
 * Feed the query input with a feature vector (e.g. the MFCC analyzer's
 * output) and get, every tick: the k nearest point ids, their integer
 * indices, distances, and — when a corpus meta file is present — the slice
 * start times in seconds, ready to drive playback position.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/KDTree.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <QFile>
#include <QFileInfo>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

struct KDTreeQuery
{
  halp_meta(name, "KDTreeQuery")
  halp_meta(c_name, "fluid_kdtreequery")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "K-nearest-neighbour lookup in a fitted KD-tree, with slice-time "
            "mapping for corpus playback")
  halp_meta(uuid, "5E609C87-7AB6-4DF0-8E80-A468D661E231")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Tree file", "kdtree.json"> tree;
    halp::lineedit<"Meta file", "corpus.json.meta.json"> meta;
    struct : halp::spinbox_i32<"K", halp::range{1, 64, 1}>
    {
    } k;
    halp::val_port<"Query", std::vector<float>> query;
  } inputs;

  struct outs
  {
    halp::val_port<"Indices", std::vector<int>> indices;
    halp::val_port<"Distances", std::vector<float>> distances;
    halp::val_port<"Starts (s)", std::vector<float>> starts;
    halp::val_port<"Status", std::string> status;
  } outputs;

  struct Model
  {
    fluid::algorithm::KDTree tree;
    fluid::index dims{0};
    std::map<std::string, std::pair<float, float>> slices; // id -> [start,end] s
  };

  std::shared_ptr<const Model> model;
  int64_t tree_mtime{-1}, meta_mtime{-1};
  long poll_phase{0};
  bool load_inflight{false};
  double session_rate{48000.};

  struct load_request
  {
    std::string tree_path, meta_path;
  };

  struct worker_t
  {
    std::function<void(std::shared_ptr<load_request>)> request;

    static std::function<void(KDTreeQuery&)>
    work(std::shared_ptr<load_request> rq)
    {
      auto m = std::make_shared<Model>();
      std::string err;

      QFile f(QString::fromStdString(rq->tree_path));
      if(f.open(QIODevice::ReadOnly))
      {
        try
        {
          const auto j = nlohmann::json::parse(f.readAll().toStdString());
          m->dims = j.at("cols").get<fluid::index>();
          fluid::algorithm::from_json(j, m->tree);
        }
        catch(const std::exception& e)
        {
          err = std::string("Tree parse error: ") + e.what();
          m.reset();
        }
      }
      else
      {
        err = "Could not open tree file";
        m.reset();
      }

      if(m && !rq->meta_path.empty())
      {
        QFile mf(QString::fromStdString(rq->meta_path));
        if(mf.open(QIODevice::ReadOnly))
        {
          try
          {
            const auto j = nlohmann::json::parse(mf.readAll().toStdString());
            for(auto it = j.begin(); it != j.end(); ++it)
              if(it.value().is_array() && it.value().size() >= 2)
                m->slices[it.key()]
                    = {it.value()[0].get<float>(), it.value()[1].get<float>()};
          }
          catch(...)
          {
            // meta is optional; ignore parse failures
          }
        }
      }

      return [m = std::move(m), err = std::move(err)](KDTreeQuery& self) {
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
    tree_mtime = meta_mtime = -1; // force reload on next tick
    poll_phase = LONG_MAX / 2;
  }

  static int64_t mtime_of(const QString& p)
  {
    QFileInfo f{p};
    return f.exists() ? f.lastModified().toMSecsSinceEpoch() : 0;
  }

  void operator()(int frames)
  {
    // Poll the model files ~2x/second; reload in the worker on change.
    poll_phase += frames > 0 ? frames : 64;
    if(poll_phase >= (long)(session_rate / 2) && !load_inflight)
    {
      poll_phase = 0;
      const auto tp
          = FluCoMa::resolve_in_folder(inputs.folder.value, inputs.tree.value);
      const auto mp
          = FluCoMa::resolve_in_folder(inputs.folder.value, inputs.meta.value);
      const auto tm = mtime_of(tp), mm = mtime_of(mp);
      if(tm != tree_mtime || mm != meta_mtime)
      {
        tree_mtime = tm;
        meta_mtime = mm;
        if(tm != 0)
        {
          load_inflight = true;
          auto rq = std::make_shared<load_request>();
          rq->tree_path = tp.toStdString();
          rq->meta_path = mp.toStdString();
          worker.request(std::move(rq));
        }
        else
          model.reset();
      }
    }

    if(!model || inputs.query.value.empty())
      return;
    if((fluid::index)inputs.query.value.size() != model->dims)
    {
      outputs.status.value = "Query size does not match tree dimensions";
      return;
    }

    fluid::FluidTensor<double, 1> q(model->dims);
    for(fluid::index i = 0; i < model->dims; i++)
      q(i) = inputs.query.value[(std::size_t)i];

    const int k = inputs.k.value;
    auto [dists, ids] = model->tree.kNearest(q, k);

    auto& oi = outputs.indices.value;
    auto& od = outputs.distances.value;
    auto& os = outputs.starts.value;
    oi.clear();
    od.clear();
    os.clear();
    for(std::size_t i = 0; i < ids.size(); i++)
    {
      const std::string& id = *ids[i];
      oi.push_back(atoi(id.c_str()));
      od.push_back((float)dists[i]);
      if(auto it = model->slices.find(id); it != model->slices.end())
        os.push_back(it->second.first);
    }
  }
};

}
