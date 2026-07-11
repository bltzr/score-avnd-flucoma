#pragma once

/* Fits a KD-tree on a dataset from the working folder and writes the model
 * as flucoma-JSON. Training runs ONLY when explicitly triggered (Process
 * toggle / programmatic trigger) — models are never retrained behind the
 * user's back.
 */

#include "FluidBuffers.hpp"

#include <flucoma/algorithms/public/KDTree.hpp>
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

struct KDTreeFit
{
  halp_meta(name, "KDTreeFit")
  halp_meta(c_name, "fluid_kdtreefit")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description, "Fits a KD-tree on a dataset for nearest-neighbour queries")
  halp_meta(uuid, "7B92F173-9A37-4AD6-A0CB-CC871496FC4B")

  struct ins
  {
    halp::lineedit<"Folder", ""> folder;
    halp::lineedit<"Dataset file", "corpus.json"> dataset;
    halp::lineedit<"Tree file", "kdtree.json"> tree;
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

    static std::function<void(KDTreeFit&)> work(std::shared_ptr<ins> in_ptr)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](KDTreeFit& self) {
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

      fluid::algorithm::KDTree tree(ds);

      const auto tree_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.tree.value);
      QFile out(tree_path);
      if(tree_path.isEmpty() || !out.open(QIODevice::WriteOnly))
        return fail("Could not write tree file");
      nlohmann::json j = tree;
      const auto text = j.dump(2);
      out.write(text.data(), (qint64)text.size());

      const int n = (int)ds.size();
      return [n](KDTreeFit& self) {
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
