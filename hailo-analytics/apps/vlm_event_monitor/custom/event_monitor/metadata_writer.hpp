#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace vlm_event_monitor
{

// Per-cycle debug metadata dump for cross-verifying VLM output against a
// native (non-VLM) network. Lives at `/var/volatile/vlm-event-metadata/`
// (tmpfs — keep_last cap auto-evicts oldest cycles to bound RAM use).
//
// Threading: the runner calls these methods on the inference queue's
// worker thread, synchronously, after a cycle's result banner has been
// printed. No internal mutex needed beyond the atomic seq counter.
class MetadataWriter
{
  public:
    MetadataWriter(std::string root_dir, uint32_t keep_last);

    // Creates root_dir if needed, evicts oldest cycle dirs until at most
    // `keep_last - 1` exist, then creates and returns a new
    // "<YYYY-MM-DD_HH-MM-SS>-<seq>" sub-dir. Returns the full path on
    // success, empty string on error (already logged).
    std::string begin_cycle();

    // Encodes a packed-RGB byte buffer (height*width*3 bytes) to
    // "<cycle_dir>/frame_<idx>.jpg" via libjpeg-turbo at quality 90.
    bool save_frame_jpeg(const std::string &cycle_dir, size_t idx, const std::vector<uint8_t> &rgb, uint32_t height,
                         uint32_t width);

    // In-memory variant: encodes a packed-RGB byte buffer (height*width*3
    // bytes) to a JPEG byte vector at quality 90 via libjpeg-turbo's
    // jpeg_mem_dest. Returns an empty vector on failure (logged). Used by
    // the SSE-inline debug bundle path so the runner can hand the
    // bundle to the broadcaster without a disk round-trip.
    std::vector<uint8_t> encode_rgb_to_jpeg(const std::vector<uint8_t> &rgb, uint32_t height, uint32_t width);

    // Writes the supplied JSON text to "<cycle_dir>/metadata.json".
    bool save_metadata_json(const std::string &cycle_dir, const std::string &json_str);

    const std::string &root_dir() const
    {
        return m_root_dir;
    }

  private:
    void evict_oldest_cycles();

    std::string m_root_dir;
    uint32_t m_keep_last;
    std::atomic<uint64_t> m_seq{0};
};

} // namespace vlm_event_monitor
