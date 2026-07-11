#pragma once

/* Fits a UMAP dimensionality reduction on a dataset from the working folder,
 * writing the embedded dataset and the model (flucoma-JSON). Training runs
 * ONLY when explicitly triggered (Fit toggle).
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/UMAP.hpp>
#include <flucoma/data/FluidDataSet.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <QFile>

#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{

struct UMAPFit
{
  halp_meta(name, "UMAPFit")
  halp_meta(c_name, "fluid_umapfit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Fits a UMAP dimensionality reduction on a dataset")
  halp_meta(uuid, "F1D038C7-F82A-4562-B871-1B83635B2D72")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Dataset file", "corpus.json"> dataset;
    halp::lineedit<"Model file", "umap.json"> model;
    halp::lineedit<"Output dataset", "corpus_umap.json"> output;
    struct : halp::spinbox_i32<"Neighbours", halp::range{2, 128, 15}>
    {
    } k;
    struct : halp::spinbox_i32<"Dimensions", halp::range{1, 32, 2}>
    {
    } dims;
    halp::hslider_f32<"Min distance", halp::range{0.001, 1., 0.1}> minDist;
    struct : halp::spinbox_i32<"Iterations", halp::range{1, 2000, 200}>
    {
    } iterations;
    halp::toggle<"Fit"> fit;
  } inputs;

  struct outs
  {
    halp::val_port<"Points", int> points;
    halp::val_port<"Status", std::string> status;
  } outputs;

  bool last_fit{false};

  struct worker_t
  {
    std::function<void(std::shared_ptr<ins>)> request;

    static std::function<void(UMAPFit&)> work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](UMAPFit& self) {
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
      const auto k = std::min<fluid::index>(in.k.value, ds.size() - 1);
      if(ds.size() < 3 || k < 2)
        return fail("Dataset too small for UMAP");

      fluid::algorithm::UMAP umap;
      auto embedded = umap.train(
          ds, k, in.dims.value, in.minDist.value, in.iterations.value);

      const auto model_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.model.value);
      {
        QFile out(model_path);
        if(model_path.isEmpty() || !out.open(QIODevice::WriteOnly))
          return fail("Could not write model file");
        nlohmann::json j = umap;
        const auto text = j.dump(2);
        out.write(text.data(), (qint64)text.size());
      }

      if(!in.output.value.empty())
      {
        const auto out_path
            = FluCoMa::resolve_in_folder(in.folder.value, in.output.value);
        QFile out(out_path);
        if(out_path.isEmpty() || !out.open(QIODevice::WriteOnly))
          return fail("Could not write output dataset file");
        nlohmann::json j = embedded;
        const auto text = j.dump(2);
        out.write(text.data(), (qint64)text.size());
      }

      const int n = (int)ds.size();
      return [n](UMAPFit& self) {
        self.outputs.points.value = n;
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
