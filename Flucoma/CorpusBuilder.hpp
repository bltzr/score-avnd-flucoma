#pragma once

/* Corpus builder: turns slice points + a feature buffer from the working
 * folder into a FluCoMa dataset (flucoma-JSON, interchangeable with Max/SC).
 *
 * For each slice [t_i, t_i+1) from the indices buffer, the feature frames
 * covering that span are averaged into one point. Point ids are zero-padded
 * slice numbers. A companion <dataset>.meta.json stores id -> [start, end]
 * seconds so query results can be mapped back to positions in the source.
 */

#include "FluidBufferPool.hpp"

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

struct CorpusBuilder
{
  halp_meta(name, "CorpusBuilder")
  halp_meta(c_name, "fluid_corpusbuilder")
  halp_meta(category, "Analysis/FluCoMa")
  halp_meta(author, "FluCoMa project, ossia team")
  halp_meta(description,
            "Builds a FluCoMa dataset from slice points and a feature buffer: "
            "one point per slice (features averaged over the slice)")
  halp_meta(uuid, "54190E4B-E953-4B3E-A3B8-1787C7B9A106")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::lineedit<"Indices file", "indices.wav"> indices;
    halp::lineedit<"Features file", "mfcc.wav"> features;
    halp::lineedit<"Dataset file", "corpus.json"> dataset;
    halp::toggle<"Process"> process;
  } inputs;

  struct outs
  {
    halp::val_port<"Points", int> points;
    halp::val_port<"Status", std::string> status;
  } outputs;

  double session_rate{48000.};
  bool last_process{false};
  bool first_run_pending{true};
  FluCoMa::file_ref indicesRef, featuresRef;

  struct worker_t
  {
    std::function<void(std::shared_ptr<ins>, double)> request;

    static std::function<void(CorpusBuilder&)>
    work(std::shared_ptr<ins> in_ptr, double rate)
    {
      ins& in = *in_ptr;

      auto fail = [](std::string msg) {
        return [msg = std::move(msg)](CorpusBuilder& self) {
          self.outputs.status.value = msg;
        };
      };

      auto idxBuf = FluCoMa::pool().get(in.folder.value, in.indices.value, rate);
      auto featBuf
          = FluCoMa::pool().get(in.folder.value, in.features.value, rate);
      if(!idxBuf)
        return fail("Could not load indices file");
      if(!featBuf)
        return fail("Could not load features file");

      fluid::client::BufferAdaptor::ReadAccess idx(idxBuf.get());
      fluid::client::BufferAdaptor::ReadAccess feat(featBuf.get());
      if(!idx.valid() || !feat.valid())
        return fail("Invalid buffers");

      const double idx_rate = idx.sampleRate() > 0 ? idx.sampleRate() : rate;
      const double feat_rate = feat.sampleRate() > 0 ? feat.sampleRate() : rate;
      const auto dims = feat.numChans();
      const auto n_frames = feat.numFrames();

      // slice starts in seconds (channel 0; -1 sentinels filtered out)
      std::vector<double> starts;
      {
        auto ch = idx.samps(0);
        for(fluid::index i = 0; i < ch.size(); i++)
          if(ch(i) >= 0)
            starts.push_back(ch(i) / idx_rate);
      }
      if(starts.empty())
        return fail("No slices in indices file");

      const double total_dur = n_frames / feat_rate;

      fluid::FluidDataSet<std::string, double, 1> ds(dims);
      fluid::FluidTensor<double, 1> point(dims);
      nlohmann::json meta;

      int n_points = 0;
      for(std::size_t s = 0; s < starts.size(); s++)
      {
        const double t0 = starts[s];
        const double t1 = s + 1 < starts.size() ? starts[s + 1] : total_dur;
        auto f0 = (fluid::index)std::floor(t0 * feat_rate);
        auto f1 = (fluid::index)std::floor(t1 * feat_rate);
        f0 = std::clamp<fluid::index>(f0, 0, n_frames - 1);
        f1 = std::clamp<fluid::index>(f1, f0 + 1, n_frames);

        for(fluid::index d = 0; d < dims; d++)
        {
          auto ch = feat.samps(d);
          double sum = 0;
          for(fluid::index f = f0; f < f1; f++)
            sum += ch(f);
          point(d) = sum / double(f1 - f0);
        }

        char id[16];
        snprintf(id, sizeof(id), "%04zu", s);
        ds.add(id, point);
        meta[id] = {t0, t1};
        n_points++;
      }

      // write dataset + companion meta
      const auto ds_path
          = FluCoMa::resolve_in_folder(in.folder.value, in.dataset.value);
      if(ds_path.isEmpty())
        return fail("No dataset file name");
      {
        nlohmann::json j = ds;
        QFile f(ds_path);
        if(!f.open(QIODevice::WriteOnly))
          return fail("Could not write dataset file");
        const auto text = j.dump(2);
        f.write(text.data(), (qint64)text.size());
      }
      {
        QFile f(ds_path + ".meta.json");
        if(f.open(QIODevice::WriteOnly))
        {
          const auto text = meta.dump(2);
          f.write(text.data(), (qint64)text.size());
        }
      }

      return [n_points, f = in.folder.value, i = in.indices.value,
              ft = in.features.value](CorpusBuilder& self) {
        self.outputs.points.value = n_points;
        self.outputs.status.value = "OK";
        self.indicesRef.refresh(f, i);
        self.indicesRef.changed();
        self.featuresRef.refresh(f, ft);
        self.featuresRef.changed();
      };
    }
  } worker;

  void prepare(halp::setup s)
  {
    session_rate = s.rate;
    first_run_pending = true;
  }

  void operator()(int)
  {
    const bool p = inputs.process.value;
    const bool edge = p && !last_process;
    last_process = p;

    indicesRef.refresh(inputs.folder.value, inputs.indices.value);
    featuresRef.refresh(inputs.folder.value, inputs.features.value);
    const bool changed = indicesRef.changed() | featuresRef.changed();

    if(first_run_pending || edge || changed)
    {
      first_run_pending = false;
      worker.request(std::make_shared<ins>(inputs), session_rate);
    }
  }
};

}
