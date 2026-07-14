#pragma once

/* Fits a robust scaling (median-centered, scaled by an inter-percentile
 * range) on a dataset from the working folder and writes the model as
 * flucoma-JSON. Optionally writes the scaled dataset alongside. Training runs
 * ONLY when explicitly triggered (Fit toggle) — models are never retrained
 * behind the user's back.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/RobustScaling.hpp>
#include <flucoma/data/FluidDataSet.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/file_port.hpp>
#include <halp/folder_combobox.hpp>
#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <QFile>

#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{

struct RobustScaleFit
{
  halp_meta(name, "RobustScaleFit")
  halp_meta(c_name, "fluid_robustscalefit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Fits a robust scaling (median / percentile range) on a dataset")
  halp_meta(uuid, "E44A2816-EA12-4BC4-92E5-145D2E56698D")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::folder_combobox<"Dataset file", "Folder", "json", "corpus.json"> dataset;
    halp::lineedit<"Model file", "robustscale.json"> model;
    halp::lineedit<"Output dataset", "corpus_robustscaled.json"> output;
    halp::spinbox_f32<"Low percentile", halp::range{0., 100., 25.}> low;
    halp::spinbox_f32<"High percentile", halp::range{0., 100., 75.}> high;
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

    static std::function<void(RobustScaleFit&)>
    work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](RobustScaleFit& self) {
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
      if(in.low.value > in.high.value)
        return fail("Low percentile must not exceed high percentile");

      fluid::algorithm::RobustScaling scaler;
      scaler.init(in.low.value, in.high.value, ds.getData());

      const auto model_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.model.value);
      {
        QFile out(model_path);
        if(model_path.isEmpty() || !out.open(QIODevice::WriteOnly))
          return fail("Could not write model file");
        nlohmann::json j = scaler;
        const auto text = j.dump(2);
        out.write(text.data(), (qint64)text.size());
      }

      if(!in.output.value.empty())
      {
        fluid::FluidTensor<double, 2> transformed(ds.size(), ds.pointSize());
        scaler.process(ds.getData(), transformed, false);

        fluid::FluidDataSet<std::string, double, 1> out_ds(ds.pointSize());
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
      }

      const int n = (int)ds.size();
      return [n](RobustScaleFit& self) {
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
