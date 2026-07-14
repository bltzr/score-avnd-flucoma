#pragma once

/* Trains a spherical k-means clustering on a dataset from the working folder
 * and writes the model (normalized cluster means) as flucoma-JSON. Points are
 * clustered by direction (cosine similarity) rather than distance. Training
 * runs ONLY when explicitly triggered (Fit toggle) — models are never
 * retrained behind the user's back.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/SKMeans.hpp>
#include <flucoma/data/FluidDataSet.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/folder_combobox.hpp>
#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <QFile>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Flucoma
{

struct SKMeansFit
{
  halp_meta(name, "SKMeansFit")
  halp_meta(c_name, "fluid_skmeansfit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description, "Trains a spherical k-means clustering on a dataset")
  halp_meta(uuid, "C695488B-9F4A-4C03-8207-D27D316A892D")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Dataset file", "Folder", "json", "corpus.json"> dataset;
    halp::lineedit<"Model file", "skmeans.json"> model;
    struct : halp::spinbox_i32<"Clusters", halp::range{1, 256, 4}>
    {
    } k;
    struct : halp::spinbox_i32<"Iterations", halp::range{1, 1000, 100}>
    {
    } iterations;
    halp::toggle<"Fit"> fit;
  } inputs;

  struct outs
  {
    halp::val_port<"Points", int> points;
    halp::val_port<"Sizes", std::vector<int>> sizes;
    halp::val_port<"Status", std::string> status;
  } outputs;

  bool last_fit{false};

  struct worker_t
  {
    std::function<void(std::shared_ptr<ins>)> request;

    static std::function<void(SKMeansFit&)> work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](SKMeansFit& self) {
          self.outputs.status.value = msg;
        };
      };

      const auto ds_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.dataset.value);
      QFile f(ds_path);
      if(ds_path.isEmpty() || !f.open(QIODevice::ReadOnly))
        return fail("Could not open dataset file");

      fluid::FluidDataSet<std::string, double, 1> ds(1);
      try
      {
        const auto j = nlohmann::json::parse(f.readAll().toStdString());
        ds = j.get<fluid::FluidDataSet<std::string, double, 1>>();
      }
      catch(const std::exception& e)
      {
        return fail(std::string("Dataset parse error: ") + e.what());
      }
      if(ds.size() == 0)
        return fail("Dataset is empty");

      const fluid::index k = in.k.value;
      if(k > ds.size())
        return fail("More clusters than points in the dataset");

      fluid::algorithm::SKMeans skmeans;
      skmeans.train(ds, k, in.iterations.value,
                    fluid::algorithm::SKMeans::InitMethod::randomPartion, -1);

      const auto model_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.model.value);
      {
        QFile out(model_path);
        if(model_path.isEmpty() || !out.open(QIODevice::WriteOnly))
          return fail("Could not write model file");
        nlohmann::json j = skmeans;
        const auto text = j.dump(2);
        out.write(text.data(), (qint64)text.size());
      }

      std::vector<int> sizes((std::size_t)k);
      for(fluid::index c = 0; c < k; c++)
        sizes[(std::size_t)c] = (int)skmeans.getClusterSize(c);

      const int n = (int)ds.size();
      return [n, sizes = std::move(sizes)](SKMeansFit& self) {
        self.outputs.points.value = n;
        self.outputs.sizes.value = sizes;
        self.outputs.status.value = "OK";
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
