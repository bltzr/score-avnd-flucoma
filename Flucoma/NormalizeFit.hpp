#pragma once

/* Fits a min/max normalization on a dataset from the working folder and
 * writes the model as flucoma-JSON. Optionally writes the normalized dataset
 * alongside. Training runs ONLY when explicitly triggered (Fit toggle) —
 * models are never retrained behind the user's back.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/Normalization.hpp>
#include <flucoma/data/FluidDataSet.hpp>
#include <flucoma/data/FluidJSON.hpp>

#include <halp/controls.hpp>
#include <halp/meta.hpp>

#include <QFile>

#include <functional>
#include <memory>
#include <string>

namespace Flucoma
{

struct NormalizeFit
{
  halp_meta(name, "NormalizeFit")
  halp_meta(c_name, "fluid_normalizefit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description, "Fits a min/max normalization on a dataset")
  halp_meta(uuid, "58FFD5F6-AFEA-4C43-BD04-F7F73324C97A")

  struct ins
  {
    halp::lineedit<"Folder", ""> folder;
    halp::lineedit<"Dataset file", "corpus.json"> dataset;
    halp::lineedit<"Model file", "normalize.json"> model;
    halp::lineedit<"Output dataset", "corpus_normalized.json"> output;
    halp::spinbox_f32<"Min", halp::range{-9999., 9999., 0.}> min;
    halp::spinbox_f32<"Max", halp::range{-9999., 9999., 1.}> max;
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

    static std::function<void(NormalizeFit&)> work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](NormalizeFit& self) {
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

      fluid::algorithm::Normalization scaler;
      scaler.init(in.min.value, in.max.value, ds.getData());

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
      return [n](NormalizeFit& self) {
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
