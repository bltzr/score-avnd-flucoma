#pragma once

/* Folder-based buffer system for FluCoMa NRT clients.
 *
 * Design (after score's Audio Particles process): each NRT object has a
 * "Folder" string port; FluCoMa buffer parameters are file names resolved
 * inside that folder. Input buffers are loaded from disk into flucoma's own
 * MemoryBufferAdaptor; output buffers are MemoryBufferAdaptors written back
 * to the folder as 32-bit float WAV, so one object's result is directly
 * another's source — the folder plays the role of Max's named buffer~ space.
 *
 * WAV files are read/written with dr_wav directly (not
 * ossia::drwav_write_handle, which is integer-PCM only and would clip
 * feature data) and keep their native sample rate — no resampling, since
 * FluCoMa buffers carry meaning in their rate (e.g. analysis hop rate).
 * Non-WAV sources fall back to score's AudioDecoder, resampled to the
 * session rate.
 */

#include "FluidBridge.hpp"

#include <Media/AudioDecoder.hpp>

#include <QDir>
#include <QFile>

#include <dr_wav.h>

#include <flucoma/clients/common/MemoryBufferAdaptor.hpp>

#include <memory>
#include <string>

namespace FluCoMa
{

inline QString resolve_in_folder(const std::string& folder, const std::string& name)
{
  if(folder.empty() || name.empty())
    return {};
  return QDir(QString::fromStdString(folder))
      .filePath(QString::fromStdString(name));
}

// Load an audio file into a MemoryBufferAdaptor. WAV: exact, native rate.
// Other formats: decoded by score, resampled to session_rate.
inline std::shared_ptr<fluid::client::MemoryBufferAdaptor>
load_buffer(const std::string& folder, const std::string& name, double session_rate)
{
  using findex = fluid::index;
  const QString path = resolve_in_folder(folder, name);
  if(path.isEmpty() || !QFile::exists(path))
    return {};

  auto make = [](findex chans, findex frames, double rate) {
    auto buf = std::make_shared<fluid::client::MemoryBufferAdaptor>(chans, frames, rate);
    fluid::client::BufferAdaptor::Access acc(buf.get());
    acc.resize(frames, chans, rate);
    return buf;
  };

  if(path.endsWith(".wav", Qt::CaseInsensitive))
  {
    drwav wav;
    if(!drwav_init_file(&wav, path.toStdString().c_str(), nullptr))
      return {};

    const findex chans = wav.channels;
    const findex frames = static_cast<fluid::index>(wav.totalPCMFrameCount);
    std::vector<float> interleaved(static_cast<std::size_t>(chans * frames));
    const auto read = drwav_read_pcm_frames_f32(
        &wav, wav.totalPCMFrameCount, interleaved.data());
    const double rate = wav.sampleRate;
    drwav_uninit(&wav);
    if(read == 0)
      return {};

    auto buf = make(chans, frames, rate);
    fluid::client::BufferAdaptor::Access acc(buf.get());
    for(findex c = 0; c < chans; ++c)
    {
      auto dst = acc.samps(c);
      for(findex i = 0; i < frames; ++i)
        dst(i) = interleaved[static_cast<std::size_t>(i * chans + c)];
    }
    return buf;
  }

  auto dec = Media::AudioDecoder::decode_synchronous(
      path, static_cast<int>(session_rate));
  if(!dec)
    return {};

  auto& arr = dec->second;
  const findex chans = static_cast<fluid::index>(arr.size());
  const findex frames = chans > 0 ? static_cast<fluid::index>(arr[0].size()) : 0;
  if(chans == 0 || frames == 0)
    return {};

  auto buf = make(chans, frames, session_rate);
  fluid::client::BufferAdaptor::Access acc(buf.get());
  for(findex c = 0; c < chans; ++c)
  {
    auto dst = acc.samps(c);
    const auto& src = arr[static_cast<std::size_t>(c)];
    for(findex i = 0; i < frames; ++i)
      dst(i) = static_cast<float>(src[static_cast<std::size_t>(i)]);
  }
  return buf;
}

// Write a buffer to <folder>/<name> as 32-bit IEEE float WAV (feature values
// are not confined to [-1, 1], so integer PCM is not an option).
inline bool save_buffer(
    const std::string& folder, const std::string& name,
    const fluid::client::BufferAdaptor& buf, double fallback_rate)
{
  using findex = fluid::index;
  const QString path = resolve_in_folder(folder, name);
  if(path.isEmpty())
    return false;

  fluid::client::BufferAdaptor::ReadAccess acc(&buf);
  if(!acc.exists() || !acc.valid())
    return false;

  const findex chans = acc.numChans();
  const findex frames = acc.numFrames();
  const double rate = acc.sampleRate() > 0 ? acc.sampleRate() : fallback_rate;
  if(chans <= 0 || frames <= 0)
    return false;

  QDir().mkpath(QFileInfo(path).absolutePath());

  drwav_data_format format{};
  format.container = drwav_container_riff;
  format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
  format.channels = static_cast<drwav_uint32>(chans);
  format.sampleRate = static_cast<drwav_uint32>(rate);
  format.bitsPerSample = 32;

  drwav wav;
  if(!drwav_init_file_write(&wav, path.toStdString().c_str(), &format, nullptr))
    return false;

  std::vector<float> interleaved(static_cast<std::size_t>(chans * frames));
  for(findex c = 0; c < chans; ++c)
  {
    auto src = acc.samps(c);
    for(findex i = 0; i < frames; ++i)
      interleaved[static_cast<std::size_t>(i * chans + c)] = src(i);
  }

  const auto written = drwav_write_pcm_frames(
      &wav, static_cast<drwav_uint64>(frames), interleaved.data());
  drwav_uninit(&wav);
  return written == static_cast<drwav_uint64>(frames);
}

// --- NRT host ----------------------------------------------------------------

/* Engine member for offline FluCoMa clients (OfflineIn/OfflineOut).
 * The owning avnd object syncs control params, sets buffer params from files,
 * calls run(), then reads back the output MemoryBufferAdaptors.
 */
template <typename Client>
struct FluidNRTHost
{
  using ParamDescType = typename Client::ParamDescType;
  using ParamSetType = fluid::client::ParameterSet<ParamDescType>;

  ParamSetType params;
  fluid::client::FluidContext context;
  Client client;

  FluidNRTHost()
      : params{Client::getParameterDescriptors(), fluid::FluidDefaultAllocator()}
      , client{params, context}
  {
  }

  FluidNRTHost(FluidNRTHost&& other) noexcept
      : params{std::move(other.params)}
      , context{other.context}
      , client{std::move(other.client)}
  {
    client.setParams(params);
  }

  FluidNRTHost(const FluidNRTHost&) = delete;
  FluidNRTHost& operator=(const FluidNRTHost&) = delete;
  FluidNRTHost& operator=(FluidNRTHost&&) = delete;

  template <typename Ins>
  void sync_params(Ins& ins)
  {
    sync_params_into(params, ins);
  }

  template <std::size_t N>
  void set_input_buffer(std::shared_ptr<fluid::client::MemoryBufferAdaptor> buf)
  {
    params.template set<N>(
        fluid::client::InputBufferUnderlyingType(std::move(buf)), nullptr);
  }

  template <std::size_t N>
  std::shared_ptr<fluid::client::MemoryBufferAdaptor> make_output_buffer()
  {
    auto buf = std::make_shared<fluid::client::MemoryBufferAdaptor>(1, 1, 0.);
    params.template set<N>(
        fluid::client::BufferUnderlyingType(buf), nullptr);
    return buf;
  }

  fluid::client::Result run()
  {
    // Raw offline clients (via ClientWrapper) have a process<T>() template;
    // NRT adaptors over RT clients (NRTSliceAdaptor & co, used directly as
    // Client here) have a plain process(FluidContext&).
    if constexpr(requires { client.process(context); })
      return client.process(context);
    else
      return client.template process<float>(context);
  }
};

}
