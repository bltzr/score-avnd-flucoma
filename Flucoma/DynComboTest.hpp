#pragma once

/* Validation object for the dynamic-combobox prototype: avendish
 * halp::dynamic_combobox (celtera/avendish#171) + score runtime alternatives
 * (ossia/score#2110). Lists the audio files of a folder in a combobox that
 * repopulates live as the folder changes.
 * Remove once the feature is upstreamed and used by real objects.
 */

#include <halp/audio.hpp>
#include <halp/controls.hpp>
#include <halp/dynamic_combobox.hpp>
#include <halp/file_port.hpp>
#include <halp/meta.hpp>

#include <QDir>

#include <string>
#include <vector>

namespace Flucoma
{

struct DynComboTest
{
  halp_meta(name, "DynComboTest")
  halp_meta(c_name, "fluid_dyncombotest")
  halp_meta(category, "Debug")
  halp_meta(author, "ossia team")
  halp_meta(description, "Test: combobox populated at runtime from a folder")
  halp_meta(uuid, "D3EB3E98-9672-43F6-BAA6-75693ED9AC64")

  struct ins
  {
    halp::folder_port<"Folder"> folder;
    halp::dynamic_combobox<"File"> file;
  } inputs;

  struct outs
  {
    halp::val_port<"Picked", std::string> picked;
  } outputs;

  std::string last_folder;
  std::vector<std::string> names;
  long poll_phase{100000000};
  double session_rate{48000.};

  void prepare(halp::setup s)
  {
    session_rate = s.rate;
    last_folder.clear(); // rescan on first tick
    poll_phase = LONG_MAX / 2;
  }

  void operator()(int frames)
  {
    // Rescan on folder change and every ~2s (test-only: sync scan in tick)
    poll_phase += frames > 0 ? frames : 64;
    const bool changed = inputs.folder.value != last_folder;
    if(changed || poll_phase >= (long)(session_rate * 2))
    {
      poll_phase = 0;
      last_folder = inputs.folder.value;

      std::vector<std::string> found;
      QDir d(QString::fromStdString(last_folder));
      for(const auto& fi :
          d.entryInfoList({"*.wav", "*.aif", "*.aiff"}, QDir::Files, QDir::Name))
        found.push_back(fi.fileName().toStdString());

      if(found != names)
      {
        names = std::move(found);
        if(inputs.file.update_items)
          inputs.file.update_items(names);
      }
    }

    const int i = inputs.file.value;
    outputs.picked.value
        = (i >= 0 && i < (int)names.size()) ? names[(std::size_t)i] : "";
  }
};

}
