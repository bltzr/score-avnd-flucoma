#pragma once

/* Live folder-synchronized buffer pool.
 *
 * The working folder acts as score's equivalent of Max's named buffer~ space:
 * this pool keeps the in-memory side synchronized with the disk side.
 *
 * - First request for <folder>/<name> loads the file and starts watching its
 *   mtime (own polling thread, 500ms — works in GUI and headless alike).
 * - When the file changes on disk, the pool RELOADS it into a fresh
 *   MemoryBufferAdaptor and bumps the entry's version. Buffers are never
 *   mutated in place, so audio threads holding the previous shared_ptr are
 *   unaffected.
 * - When the file disappears, the entry's buffer becomes null (version still
 *   bumps); when it (re)appears, the poll picks it up and loads it.
 * - Writers use write_through() so downstream objects see the new content
 *   immediately, without a poll round-trip and without re-triggering
 *   themselves.
 *
 * Consumers poll `version(folder, name)` per tick (an integer compare) and
 * re-fetch with `get()` when it changed — this single mechanism covers path
 * edits, scenario load, play start and disk changes.
 */

#include "FluidBuffers.hpp"

#include <QFileInfo>

#include <chrono>
#include <map>
#include <mutex>
#include <thread>

namespace FluCoMa
{

class BufferPool
{
public:
  using BufferPtr = std::shared_ptr<fluid::client::MemoryBufferAdaptor>;

  static BufferPool& instance()
  {
    static BufferPool p;
    return p;
  }

  // Current version of a file entry. 0 = never requested. Bumps on every
  // (re)load, disappearance, or write-through.
  uint64_t version(const std::string& folder, const std::string& name)
  {
    if(folder.empty() || name.empty())
      return 0;
    const auto key = resolve_in_folder(folder, name).toStdString();
    if(key.empty())
      return 0;
    std::lock_guard lock{m_mutex};
    auto it = m_entries.find(key);
    return it != m_entries.end() ? it->second.version : 0;
  }

  // Get the buffer for <folder>/<name>, loading it (and starting the watch)
  // on first request. Returns nullptr if the file is absent or unreadable —
  // the entry stays registered and the poll picks the file up when it appears.
  BufferPtr get(const std::string& folder, const std::string& name, double rate)
  {
    if(folder.empty() || name.empty())
      return {};
    const auto key = resolve_in_folder(folder, name).toStdString();
    if(key.empty())
      return {};

    std::lock_guard lock{m_mutex};
    auto it = m_entries.find(key);
    if(it != m_entries.end())
      return it->second.buffer;

    auto& e = m_entries[key];
    e.rate = rate;
    e.buffer = load_path(key, rate);
    e.mtime = mtime_of(key);
    e.version = 1;
    start_poller();
    return e.buffer;
  }

  // Store a buffer we just computed: write the file AND publish the buffer,
  // so consumers don't reload from disk (and see it before the poll runs).
  bool write_through(
      const std::string& folder, const std::string& name,
      const fluid::client::BufferAdaptor& buf, double rate)
  {
    if(!save_buffer(folder, name, buf, rate))
      return false;

    const auto key = resolve_in_folder(folder, name).toStdString();
    std::lock_guard lock{m_mutex};
    auto& e = m_entries[key];
    e.rate = rate;
    // Reload from the file we just wrote: cheap for feature buffers, and
    // guarantees pool content == disk content.
    e.buffer = load_path(key, rate);
    e.mtime = mtime_of(key); // poll won't see our own write as a change
    e.version++;
    start_poller();
    return true;
  }

  ~BufferPool()
  {
    m_stop = true;
    if(m_poller.joinable())
      m_poller.join();
  }

private:
  struct Entry
  {
    BufferPtr buffer;
    uint64_t version{0};
    int64_t mtime{0};
    double rate{48000.};
  };

  static int64_t mtime_of(const std::string& path)
  {
    QFileInfo f{QString::fromStdString(path)};
    return f.exists() ? f.lastModified().toMSecsSinceEpoch() : 0;
  }

  static BufferPtr load_path(const std::string& path, double rate)
  {
    auto slash = path.find_last_of('/');
    if(slash == std::string::npos)
      return {};
    return load_buffer(path.substr(0, slash), path.substr(slash + 1), rate);
  }

  void start_poller()
  {
    if(m_poller.joinable())
      return;
    m_poller = std::thread([this] {
      while(!m_stop)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // snapshot keys+mtimes to stat outside the lock
        std::vector<std::pair<std::string, int64_t>> snapshot;
        {
          std::lock_guard lock{m_mutex};
          snapshot.reserve(m_entries.size());
          for(auto& [k, e] : m_entries)
            snapshot.emplace_back(k, e.mtime);
        }

        for(auto& [key, seen_mtime] : snapshot)
        {
          if(m_stop)
            return;
          const auto now_mtime = mtime_of(key);
          if(now_mtime == seen_mtime)
            continue;

          // Load outside the lock, then publish.
          double rate;
          {
            std::lock_guard lock{m_mutex};
            auto it = m_entries.find(key);
            if(it == m_entries.end() || it->second.mtime != seen_mtime)
              continue; // raced with write_through
            rate = it->second.rate;
          }
          auto buf = now_mtime != 0 ? load_path(key, rate) : BufferPtr{};
          {
            std::lock_guard lock{m_mutex};
            auto it = m_entries.find(key);
            if(it == m_entries.end() || it->second.mtime != seen_mtime)
              continue;
            it->second.buffer = std::move(buf);
            it->second.mtime = now_mtime;
            it->second.version++;
          }
        }
      }
    });
  }

  std::mutex m_mutex;
  std::map<std::string, Entry> m_entries;
  std::thread m_poller;
  std::atomic<bool> m_stop{false};
};

inline BufferPool& pool()
{
  return BufferPool::instance();
}

// Per-consumed-file tracking for objects that auto-rerun when their inputs
// change: remember the version seen last run, poll for changes each tick.
struct file_ref
{
  std::string folder, name;
  uint64_t seen_version{0};

  bool changed()
  {
    const auto v = pool().version(folder, name);
    if(v != seen_version)
    {
      seen_version = v;
      return true;
    }
    return false;
  }

  void refresh(const std::string& f, const std::string& n)
  {
    if(f != folder || n != name)
    {
      folder = f;
      name = n;
      seen_version = 0; // force changed() on next poll
    }
  }
};

}
