#pragma once

/* Fits a principal component analysis on a dataset from the working folder
 * and writes the model as flucoma-JSON. Optionally writes the dataset
 * projected onto the first K components alongside. Training runs ONLY when
 * explicitly triggered (Fit toggle) — models are never retrained behind the
 * user's back.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/util/AlgorithmUtils.hpp> // for fluid::algorithm::epsilon, used by PCA.hpp

#include <flucoma/algorithms/public/PCA.hpp>
#include <flucoma/data/FluidDataSet.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/file_port.hpp>
#include <halp/folder_combobox.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <QFile>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{

struct PCAFit
{
  halp_meta(name, "PCAFit")
  halp_meta(c_name, "fluid_pcafit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Fits a principal component analysis on a dataset for "
            "dimensionality reduction")
  halp_meta(uuid, "213D05F9-1643-4B6D-944D-41DC644AD9FB")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Dataset file", "Folder", "json", "corpus.json"> dataset;
    halp::lineedit<"Model file", "pca.json"> model;
    halp::lineedit<"Output dataset", "corpus_pca.json"> output;
    struct : halp::spinbox_i32<"Components", halp::range{1, 64, 2}>
    {
    } k;
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

    static std::function<void(PCAFit&)> work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](PCAFit& self) {
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
      if(ds.size() < 2)
        return fail("Dataset needs at least 2 points");

      fluid::algorithm::PCA pca;
      pca.init(ds.getData());

      const auto k
          = std::clamp<fluid::index>(in.k.value, 1, pca.size());

      const auto model_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.model.value);
      {
        QFile out(model_path);
        if(model_path.isEmpty() || !out.open(QIODevice::WriteOnly))
          return fail("Could not write model file");
        nlohmann::json j = pca;
        const auto text = j.dump(2);
        out.write(text.data(), (qint64)text.size());
      }

      std::string status = "OK";
      if(!in.output.value.empty())
      {
        fluid::FluidTensor<double, 2> transformed(ds.size(), k);
        const double variance
            = pca.process(ds.getData(), transformed, k, false);

        fluid::FluidDataSet<std::string, double, 1> out_ds(k);
        auto ids = ds.getIds();
        for(fluid::index r = 0; r < ds.size(); r++)
          out_ds.add(ids(r), transformed.row(r));

        const auto out_path
            = FluCoMa::resolve_in_folder(in.folder.value, in.output.value);
        QFile out(out_path);
        if(out_path.isEmpty() || !out.open(QIODevice::WriteOnly))
          return fail("Could not write output dataset file");
        nlohmann::json j = out_ds;
        const auto text = j.dump(2);
        out.write(text.data(), (qint64)text.size());

        char buf[64];
        snprintf(buf, sizeof(buf), "OK (explained variance: %.1f%%)",
                 100. * variance);
        status = buf;
      }

      const int n = (int)ds.size();
      return [n, status = std::move(status)](PCAFit& self) {
        self.outputs.points.value = n;
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
