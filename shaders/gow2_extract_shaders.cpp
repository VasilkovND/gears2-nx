// gow2_extract_shaders: pulls every Xenos shader container out of Gears of War 2's cooked packages, offline.
//
// Unreal Engine 3 keeps compiled shaders in shader caches serialized inside its packages (GearGame/CookedXenon/*.xxx:
// the global and reference shader caches, and one per map with its materials), as the bytes D3DDevice_Create*Shader
// receives: 2008-format containers (flags 0x102A1100 PS, 0x102A1101 VS, then virtualSize and physicalSize: the
// container is their sum). The packages are compressed, so each is expanded first, the way the Xbox 360 build of UE3
// stores them (big-endian):
//   - fully compressed (most of the startup packages): tag 0x9E2A83C1, block size, compressed and uncompressed totals,
//     one (compressed, uncompressed) size pair per block, then the blocks;
//   - chunk compressed (maps): a plain package summary (tag, version 575 in the low 16 bits, ...) whose
//     CompressedChunks list (uncompressed offset and size, compressed offset and size) points at streams of the first
//     kind.
// Each block is an XMemCompress LZX stream (window 2^17): frames prefixed by FF + u16 uncompressed + u16 compressed
// size, or a bare u16 compressed size (a 0x8000-byte frame); libmspack's lzxd reads the plain bitstream once those
// prefixes are stripped. The format notes come from gears-of-war-2-pc-port's tools/ue3 (BSD-3-Clause).
// Files that are neither are scanned as they are. Each container is written once as {vs,ps}_<xxh3>.bin, the key the
// shader library uses.
//
// Usage: gow2_extract_shaders <out dir> <file or folder> ...       (folders are walked; every file is tried)
#define XXH_INLINE_ALL
#include <xxhash.h>

extern "C" {
#include "mspack.h"
#include "lzx.h"
}

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr uint32_t kTag = 0x9E2A83C1u;
constexpr uint32_t kPackageVersion = 575;

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// --- LZX through libmspack, with in-memory "files" ---------------------------------------------------------------
struct Source {
  const uint8_t* buf;
  size_t size, pos, rest;  // rest: bytes left in the current frame
};
struct Sink {
  uint8_t* buf;
  size_t size, pos;
};

int ReadFrames(mspack_file* file, void* buffer, int bytes) {
  auto* f = reinterpret_cast<Source*>(file);
  if (f->rest == 0) {
    if (f->pos >= f->size) return 0;
    if (f->buf[f->pos] == 0xFF) {
      if (f->pos + 5 > f->size) return 0;
      f->rest = size_t(f->buf[f->pos + 3]) << 8 | f->buf[f->pos + 4];
      f->pos += 5;
    } else {
      if (f->pos + 2 > f->size) return 0;
      f->rest = size_t(f->buf[f->pos]) << 8 | f->buf[f->pos + 1];
      f->pos += 2;
    }
    if (f->rest > f->size - f->pos) f->rest = f->size - f->pos;
    if (f->rest == 0) return 0;
  }
  const size_t n = std::min(size_t(bytes), f->rest);
  std::memcpy(buffer, f->buf + f->pos, n);
  f->pos += n;
  f->rest -= n;
  return int(n);
}
int Write(mspack_file* file, void* buffer, int bytes) {
  auto* f = reinterpret_cast<Sink*>(file);
  const size_t n = std::min(size_t(bytes), f->size - f->pos);
  std::memcpy(f->buf + f->pos, buffer, n);
  f->pos += n;
  return int(n);
}
void* Alloc(mspack_system*, size_t bytes) { return std::calloc(bytes, 1); }
void Free(void* p) { std::free(p); }
void Copy(void* src, void* dst, size_t bytes) { std::memmove(dst, src, bytes); }
void Message(mspack_file*, const char*, ...) {}

mspack_system System() {
  mspack_system s{};
  s.read = ReadFrames;
  s.write = Write;
  s.alloc = Alloc;
  s.free = Free;
  s.copy = Copy;
  s.message = Message;
  return s;
}

// One fully compressed stream at d[0..n) -> out (appended). False if malformed.
bool Expand(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
  if (n < 16 || be32(d) != kTag) return false;
  const uint32_t block = be32(d + 4), total_u = be32(d + 12);
  if (block == 0 || total_u > (1u << 30)) return false;
  const uint32_t blocks = (total_u + block - 1) / block;
  size_t table = 16, data = table + size_t(blocks) * 8;
  if (data > n) return false;
  const size_t base = out.size();
  out.resize(base + total_u);
  size_t at = base;
  mspack_system sys = System();
  for (uint32_t b = 0; b < blocks; ++b) {
    const uint32_t csize = be32(d + table + b * 8), usize = be32(d + table + b * 8 + 4);
    if (data + csize > n || at + usize > out.size()) return false;
    Source src{d + data, csize, 0, 0};
    Sink dst{out.data() + at, usize, 0};
    lzxd_stream* lzx = lzxd_init(&sys, reinterpret_cast<mspack_file*>(&src), reinterpret_cast<mspack_file*>(&dst), 17, 0,
                                 0x40000, usize, 0);
    if (!lzx) return false;
    const int r = lzxd_decompress(lzx, usize);
    lzxd_free(lzx);
    if (r != MSPACK_ERR_OK || dst.pos != usize) return false;
    data += csize;
    at += usize;
  }
  out.resize(at);
  return true;
}

// A package summary with CompressedChunks: each chunk expanded at its uncompressed offset. False if not one, or bad.
bool ExpandChunked(const uint8_t* d, size_t n, std::vector<uint8_t>& out, bool& had_chunks) {
  had_chunks = false;
  size_t p = 12;  // tag, version, header size
  auto u32 = [&](uint32_t& v) {
    if (p + 4 > n) return false;
    v = be32(d + p);
    p += 4;
    return true;
  };
  uint32_t v;
  if (p + 4 > n) return false;
  const int32_t len = int32_t(be32(d + p));  // folder name (FString)
  p += 4;
  p += len > 0 ? size_t(len) : size_t(-int64_t(len)) * 2;
  for (int i = 0; i < 8; ++i)
    if (!u32(v)) return false;
  p += 16;  // guid
  uint32_t generations;
  if (!u32(generations) || generations > 1000) return false;
  p += size_t(generations) * 12;
  uint32_t flags, count;
  if (!u32(v) || !u32(v) || !u32(flags) || !u32(count) || count > 100000 || p + size_t(count) * 16 > n) return false;
  if (count == 0) return true;
  had_chunks = true;
  struct Chunk {
    uint32_t uo, us, co, cs;
  };
  std::vector<Chunk> chunks(count);
  size_t total = 0;
  for (auto& c : chunks) {
    c = {be32(d + p), be32(d + p + 4), be32(d + p + 8), be32(d + p + 12)};
    p += 16;
    total = std::max(total, size_t(c.uo) + c.us);
  }
  if (total > (1u << 30)) return false;
  out.assign(total, 0);
  std::memcpy(out.data(), d, std::min<size_t>(chunks[0].uo, n));
  std::vector<uint8_t> part;
  for (const auto& c : chunks) {
    if (size_t(c.co) + c.cs > n) return false;
    part.clear();
    if (!Expand(d + c.co, c.cs, part) || part.size() != c.us) return false;
    std::memcpy(out.data() + c.uo, part.data(), c.us);
  }
  return true;
}

struct Stats {
  size_t files = 0, compressed = 0, chunked = 0, plain = 0, bad = 0, magic = 0, found = 0, written = 0;
} stats;
std::unordered_set<uint64_t> seen;
std::filesystem::path out_dir;

void ScanContainers(const uint8_t* d, size_t n) {
  for (size_t i = 0; i + 36 <= n; ++i) {
    if (d[i] != 0x10 || d[i + 1] != 0x2A || d[i + 2] != 0x11) continue;
    ++stats.magic;
    if (d[i + 3] > 1) continue;
    const uint64_t size = uint64_t(be32(d + i + 4)) + be32(d + i + 8);
    if (size < 36 || size > (4u << 20) || i + size > n) continue;
    const uint32_t shader_offset = be32(d + i + 24);
    if (shader_offset >= size) continue;
    ++stats.found;
    const uint64_t h = XXH3_64bits(d + i, size);
    if (!seen.insert(h).second) continue;
    char name[64];
    std::snprintf(name, sizeof(name), "%s_%016llx.bin", d[i + 3] ? "vs" : "ps", (unsigned long long)h);
    if (FILE* f = std::fopen((out_dir / name).string().c_str(), "wb")) {
      std::fwrite(d + i, 1, size, f);
      std::fclose(f);
      ++stats.written;
    }
  }
}

void ScanFile(const std::filesystem::path& path) {
  std::vector<uint8_t> raw(std::filesystem::file_size(path));
  FILE* f = std::fopen(path.string().c_str(), "rb");
  if (!f) {
    std::fprintf(stderr, "%s: cannot open\n", path.string().c_str());
    ++stats.bad;
    return;
  }
  const size_t got = std::fread(raw.data(), 1, raw.size(), f);
  std::fclose(f);
  raw.resize(got);
  ++stats.files;
  std::vector<uint8_t> plain;
  const size_t before = stats.found;
  if (raw.size() >= 16 && be32(raw.data()) == kTag) {
    if ((be32(raw.data() + 4) & 0xFFFF) == kPackageVersion) {
      bool had_chunks = false;
      if (!ExpandChunked(raw.data(), raw.size(), plain, had_chunks)) {
        std::fprintf(stderr, "%s: bad compressed chunks\n", path.string().c_str());
        ++stats.bad;
        ScanContainers(raw.data(), raw.size());
        return;
      }
      if (had_chunks) {
        ++stats.chunked;
        ScanContainers(plain.data(), plain.size());
      } else {
        ++stats.plain;
        ScanContainers(raw.data(), raw.size());
      }
    } else if (Expand(raw.data(), raw.size(), plain)) {
      ++stats.compressed;
      ScanContainers(plain.data(), plain.size());
    } else {
      std::fprintf(stderr, "%s: bad compressed stream\n", path.string().c_str());
      ++stats.bad;
      ScanContainers(raw.data(), raw.size());
    }
  } else {
    ++stats.plain;
    ScanContainers(raw.data(), raw.size());
  }
  if (stats.found > before) {
    std::printf("%s: %zu containers\n", path.filename().string().c_str(), stats.found - before);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <out dir> <GearGame/CookedXenon or files> ...\n", argv[0]);
    return 1;
  }
  out_dir = argv[1];
  std::filesystem::create_directories(out_dir);
  for (const auto& entry : std::filesystem::directory_iterator(out_dir)) {
    // Already extracted: keep their keys so reruns add only new ones.
    unsigned long long h;
    if (std::sscanf(entry.path().filename().string().c_str() + 3, "%16llx", &h) == 1) seen.insert(h);
  }
  for (int a = 2; a < argc; ++a) {
    const std::filesystem::path arg = argv[a];
    if (std::filesystem::is_directory(arg)) {
      std::vector<std::filesystem::path> files;
      for (const auto& e : std::filesystem::recursive_directory_iterator(arg))
        if (e.is_regular_file()) files.push_back(e.path());
      std::sort(files.begin(), files.end());
      for (const auto& p : files) ScanFile(p);
    } else {
      ScanFile(arg);
    }
  }
  std::printf("files %zu (compressed %zu, chunked %zu, plain %zu, bad %zu); magic %zu, containers found %zu, new files %zu, "
              "total unique %zu\n",
              stats.files, stats.compressed, stats.chunked, stats.plain, stats.bad, stats.magic, stats.found, stats.written,
              seen.size());
  return stats.bad ? 2 : 0;
}
