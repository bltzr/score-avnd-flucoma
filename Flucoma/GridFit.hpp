#pragma once

/* Snaps a 2D embedded dataset (e.g. a UMAP corpus map) from the working
 * folder onto a regular grid, writing the gridded dataset as flucoma-JSON.
 * Each output point holds its (column, row) grid cell. Fit-only, no model
 * file. Runs ONLY when explicitly triggered (Fit toggle).
 */

#include "FluidBuffers.hpp"

// FluidDataSet.hpp must come first: Grid.hpp uses FluidDataSet but does not
// include it itself.
#include <flucoma/data/FluidDataSet.hpp>

#include <flucoma/algorithms/public/Grid.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/folder_combobox.hpp>
#include <halp/controls.enums.hpp>
#include <halp/controls.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <QFile>

#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{

struct GridFit
{
  halp_meta(name, "GridFit")
  halp_meta(c_name, "fluid_gridfit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Assigns the points of a 2D embedded dataset to a regular grid")
  halp_meta(uuid, "52D3B4E7-1CA2-4490-8A16-11C565A27CCB")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Dataset file", "Folder", "json", "corpus_umap.json"> dataset;
    halp::lineedit<"Output dataset", "corpus_grid.json"> output;
    struct : halp::spinbox_i32<"Oversampling", halp::range{1, 16, 1}>
    {
    } oversample;
    struct : halp::spinbox_i32<"Extent", halp::range{0, 1024, 0}>
    {
    } extent;
    struct
    {
      // Which axis the Extent constrains (0: columns, 1: rows)
      halp__enum_combobox("Extent axis", Horizontal, Horizontal, Vertical)
    } axis;
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

    static std::function<void(GridFit&)> work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](GridFit& self) {
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
      if(ds.pointSize() != 2)
        return fail("Grid needs a 2D dataset (e.g. a UMAP embedding)");

      fluid::algorithm::Grid grid;
      auto gridded = grid.process(
          ds, in.oversample.value, in.extent.value,
          (fluid::index)in.axis.value);
      // Grid returns an empty dataset when the points are degenerate
      // (zero area) or when the assignment fails.
      if(gridded.size() == 0)
        return fail("Grid assignment failed (degenerate point layout?)");

      const auto out_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.output.value);
      QFile out(out_path);
      if(out_path.isEmpty() || !out.open(QIODevice::WriteOnly))
        return fail("Could not write output dataset file");
      nlohmann::json j = gridded;
      const auto text = j.dump(2);
      out.write(text.data(), (qint64)text.size());

      const int n = (int)gridded.size();
      return [n](GridFit& self) {
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
