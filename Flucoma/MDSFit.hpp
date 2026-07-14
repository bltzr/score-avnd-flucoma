#pragma once

/* Multidimensional scaling: embeds a dataset from the working folder into a
 * lower-dimensional space that preserves pairwise distances, and writes the
 * embedded dataset as flucoma-JSON. MDS has no reusable model — it is
 * fit-only (dataset in, embedded dataset out). Runs ONLY when explicitly
 * triggered (Fit toggle).
 *
 * Note: the KL and JS metrics expect non-negative data.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/MDS.hpp>
#include <flucoma/data/FluidDataSet.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/folder_combobox.hpp>
#include <halp/controls.enums.hpp>
#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <QFile>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{

struct MDSFit
{
  halp_meta(name, "MDSFit")
  halp_meta(c_name, "fluid_mdsfit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Embeds a dataset in a lower-dimensional space with "
            "multidimensional scaling")
  halp_meta(uuid, "31428F8F-E5A3-4730-A3B6-1B4886200226")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Dataset file", "Folder", "json", "corpus.json"> dataset;
    halp::lineedit<"Output dataset", "corpus_mds.json"> output;
    struct : halp::spinbox_i32<"Dimensions", halp::range{1, 32, 2}>
    {
    } dims;
    struct
    {
      // Same order as fluid::algorithm::DistanceFuncs::Distance
      halp__enum_combobox(
          "Distance", Euclidean, Manhattan, Euclidean, SqEuclidean, Max, Min,
          KL, Cosine, JS)
    } metric;
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

    static std::function<void(MDSFit&)> work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](MDSFit& self) {
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

      // The embedding comes from the SVD of the n x n doubly-centered
      // distance matrix, so at most n dimensions are available.
      const auto k = std::clamp<fluid::index>(in.dims.value, 1, ds.size());

      fluid::algorithm::MDS mds;
      fluid::FluidTensor<double, 2> embedded(ds.size(), k);
      mds.process(
          ds.getData(), embedded, (fluid::index)in.metric.value, k);

      fluid::FluidDataSet<std::string, double, 1> out_ds(k);
      auto ids = ds.getIds();
      for(fluid::index r = 0; r < ds.size(); r++)
        out_ds.add(ids(r), embedded.row(r));

      const auto out_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.output.value);
      QFile out(out_path);
      if(out_path.isEmpty() || !out.open(QIODevice::WriteOnly))
        return fail("Could not write output dataset file");
      nlohmann::json j = out_ds;
      const auto text = j.dump(2);
      out.write(text.data(), (qint64)text.size());

      const int n = (int)ds.size();
      return [n](MDSFit& self) {
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
