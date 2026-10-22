// gears2-nx - CPU time the console has no use for.
//
// The game has three cores on the Switch, and in heavy scenes (explosions) all three were full (275-292 % of 300 % in
// rex_perfil.log) while the GPU sat idle 26-37 ms per frame waiting for commands. The stack sampler (ZL + ZR + R) put
// half of the busiest thread, Unreal's rendering thread at 85 % of a core, in one function:
//
//   sub_82977688  Direct3D's "is the GPU there yet" check: 4 x 8 db16cyc delays, then a look at the GPU's progress.
//                 It answers 1 while still waiting and its six callers simply call it again.
//
//   sub_829783A8  Direct3D's query GetData (occlusion and event queries): 1 (S_FALSE) while the GPU has not written
//                 the result yet, and Unreal's rendering thread polls it in a loop.
//   sub_82955830  the audio hardware (XMA, registers at 0x7FEA1818) check sub_82955768 spins on with db16cyc until it
//                 answers non-zero or its time runs out.
//
// On the Xbox 360 those spins cost a hardware thread out of six; here they take a core from the threads they wait for
// (the game's main thread and the GPU ring). While they answer "still waiting" the thread now sleeps instead.

#include "gow2_pch.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_INT32(gow2_espera_gpu_us, 100, "Gears2",
                     "Microsegundos que duermen las esperas activas del juego (GPU, consultas, audio) cada vez que aun no esta; "
                     "0 = girar como en la Xbox 360");

namespace {
void DormirEsperando() {
#if defined(__SWITCH__)
  const int32_t us = REXCVAR_GET(gow2_espera_gpu_us);
  svcSleepThread(us > 0 ? int64_t(us) * 1000 : 0);
#endif
}
}  // namespace

REX_EXTERN(__imp__sub_82977688);
REX_HOOK_RAW(sub_82977688) {
  __imp__sub_82977688(ctx, base);
  if (ctx.r3.u32 == 1) DormirEsperando();
}

REX_EXTERN(__imp__sub_829783A8);
REX_HOOK_RAW(sub_829783A8) {
  __imp__sub_829783A8(ctx, base);
  if (ctx.r3.u32 == 1) DormirEsperando();  // S_FALSE: not there yet
}

REX_EXTERN(__imp__sub_82955830);
REX_HOOK_RAW(sub_82955830) {
  __imp__sub_82955830(ctx, base);
  if (ctx.r3.u32 == 0) DormirEsperando();
}

// --- Real work done natively ---------------------------------------------------------------------------------------
//
// sub_827FFD00  an audio effect of the game's mixer: up to three filter stages (enabled by fx+52, +72, +96) over 256
//               frames of each channel (planar, 1024 bytes apart), with state kept between calls at fx+116..+136 and
//               shared by the channels, as the PowerPC code does. 8 % of the audio thread in the profile. Ported
//               operation by operation (each fmadds/fnmsubs as one fused multiply-add rounded to single precision);
//               gow2_audio_filtro_nativo = false goes back to the recompiled code.
REXCVAR_DEFINE_BOOL(gow2_audio_filtro_nativo, true, "Gears2",
                    "El filtro de audio del juego (sub_827FFD00) en codigo nativo en vez del recompilado");

namespace {
inline double LeerF(uint8_t* base, uint32_t direccion) {
  return double(std::bit_cast<float>(rex::memory::load_and_swap<uint32_t>(base + direccion)));
}
inline void EscribirF(uint8_t* base, uint32_t direccion, double v) {
  rex::memory::store_and_swap<uint32_t>(base + direccion, std::bit_cast<uint32_t>(float(v)));
}
inline double Simple(double v) { return double(float(v)); }
}  // namespace

REX_EXTERN(sub_827FFE78);
REX_EXTERN(__imp__sub_827FFD00);
REX_HOOK_RAW(sub_827FFD00) {
  if (!REXCVAR_GET(gow2_audio_filtro_nativo)) {
    __imp__sub_827FFD00(ctx, base);
    return;
  }
  const uint32_t fx = ctx.r3.u32, buf = ctx.r4.u32;
  const uint64_t lr = ctx.lr;
  sub_827FFE78(ctx, base);  // r3 = fx, r4 = buf, as the original calls it
  ctx.lr = lr;
  auto U = [&](uint32_t a) { return rex::memory::load_and_swap<uint32_t>(base + a); };
  const bool e1 = U(fx + 52) != 0, e2 = U(fx + 72) != 0, e3 = U(fx + 96) != 0;
  if (e1 || e2 || e3) {
    const uint32_t cab = buf ? buf - 8 : 0;
    const uint32_t canales = base[cab + 21];
    uint32_t col = U(cab + 28);
    const double a = LeerF(base, fx + 68), b = LeerF(base, fx + 64), c = LeerF(base, fx + 76), d = LeerF(base, fx + 92),
                 g = LeerF(base, fx + 88), h = LeerF(base, fx + 112), k = LeerF(base, fx + 108);
    double s132 = LeerF(base, fx + 132), s116 = LeerF(base, fx + 116), s120 = LeerF(base, fx + 120),
           s124 = LeerF(base, fx + 124), s128 = LeerF(base, fx + 128), s136 = LeerF(base, fx + 136);
    for (int n = 0; n < 256; ++n, col += 4) {
      uint32_t p = col;
      for (uint32_t ch = 0; ch < canales; ++ch, p += 1024) {
        double x = LeerF(base, p);
        if (e1) {
          const double t = Simple(-std::fma(a, s132, -x));
          const double u = Simple(std::fma(a, t, s132));
          x = Simple(std::fma(Simple(x - u), b, x));
          s132 = t;
        }
        if (e2) {
          const double m = Simple(x * c), o116 = s116, o120 = s120, o124 = s124, o128 = s128;
          s120 = o116;
          s116 = x;
          s128 = o124;
          const double f3 = Simple(std::fma(d, o116, -m)), f2 = Simple(f3 + o120);
          const double f1 = Simple(-std::fma(o124, d, -f2)), f13 = Simple(std::fma(c, o128, f1));
          s124 = f13;
          x = Simple(std::fma(Simple(x - f13), g, x));
        }
        if (e3) {
          const double t = Simple(-std::fma(h, s136, -x));
          const double u = Simple(std::fma(h, t, s136));
          x = Simple(std::fma(Simple(u + x), k, x));
          s136 = t;
        }
        EscribirF(base, p, x);
      }
    }
    EscribirF(base, fx + 132, s132);
    EscribirF(base, fx + 116, s116);
    EscribirF(base, fx + 120, s120);
    EscribirF(base, fx + 124, s124);
    EscribirF(base, fx + 128, s128);
    EscribirF(base, fx + 136, s136);
  }
  ctx.r3.u64 = 0;
}

// sub_82AB1C28  XMemDecompress (the xcompress library linked into the game): Unreal decompresses every 128 KB block of
//               its packages and streamed textures with it, block by block. The block is an XMemCompress LZX stream
//               (window 2^17): frames prefixed with FF + u16 uncompressed + u16 compressed size, or a bare u16
//               compressed size (a 32 KB frame). Decoded natively with the SDK's libmspack; a block it does not decode
//               to exactly the size asked for goes to the recompiled code, so nothing can come out different.
//               5-7 % of a core while textures stream. gow2_lzx_nativo = false: always the recompiled code.
REXCVAR_DEFINE_BOOL(gow2_lzx_nativo, true, "Gears2",
                    "XMemDecompress (sub_82AB1C28) con libmspack nativo en vez del codigo recompilado");

extern "C" {
#include <mspack.h>
#include <lzx.h>
}

namespace {
struct FuenteLzx {
  const uint8_t* datos;
  size_t tam, pos, resto;  // resto: bytes left in the current frame
};
struct DestinoLzx {
  uint8_t* datos;
  size_t tam, pos;
};
int LeerTramas(mspack_file* archivo, void* buffer, int bytes) {
  auto* f = reinterpret_cast<FuenteLzx*>(archivo);
  if (f->resto == 0) {
    if (f->pos >= f->tam) return 0;
    if (f->datos[f->pos] == 0xFF) {
      if (f->pos + 5 > f->tam) return 0;
      f->resto = size_t(f->datos[f->pos + 3]) << 8 | f->datos[f->pos + 4];
      f->pos += 5;
    } else {
      if (f->pos + 2 > f->tam) return 0;
      f->resto = size_t(f->datos[f->pos]) << 8 | f->datos[f->pos + 1];
      f->pos += 2;
    }
    if (f->resto > f->tam - f->pos) f->resto = f->tam - f->pos;
    if (f->resto == 0) return 0;
  }
  const size_t n = std::min(size_t(bytes), f->resto);
  std::memcpy(buffer, f->datos + f->pos, n);
  f->pos += n;
  f->resto -= n;
  return int(n);
}
int EscribirLzx(mspack_file* archivo, void* buffer, int bytes) {
  auto* f = reinterpret_cast<DestinoLzx*>(archivo);
  const size_t n = std::min(size_t(bytes), f->tam - f->pos);
  std::memcpy(f->datos + f->pos, buffer, n);
  f->pos += n;
  return int(n);
}
void* ReservarLzx(mspack_system*, size_t bytes) { return std::calloc(bytes, 1); }
void LiberarLzx(void* p) { std::free(p); }
void CopiarLzx(void* origen, void* destino, size_t bytes) { std::memmove(destino, origen, bytes); }
void MensajeLzx(mspack_file*, const char*, ...) {}

bool DescomprimirLzx(const uint8_t* origen, size_t tam_origen, uint8_t* destino, size_t tam_destino) {
  if (!tam_origen || !tam_destino) return false;
  mspack_system sistema{};
  sistema.read = LeerTramas;
  sistema.write = EscribirLzx;
  sistema.alloc = ReservarLzx;
  sistema.free = LiberarLzx;
  sistema.copy = CopiarLzx;
  sistema.message = MensajeLzx;
  FuenteLzx fuente{origen, tam_origen, 0, 0};
  DestinoLzx salida{destino, tam_destino, 0};
  lzxd_stream* lzx = lzxd_init(&sistema, reinterpret_cast<mspack_file*>(&fuente),
                               reinterpret_cast<mspack_file*>(&salida), 17, 0, 0x8000, off_t(tam_destino), 0);
  if (!lzx) return false;
  const int r = lzxd_decompress(lzx, off_t(tam_destino));
  lzxd_free(lzx);
  return r == MSPACK_ERR_OK && salida.pos == tam_destino;
}
std::atomic<uint64_t> g_lzx_nativos{0}, g_lzx_recompilados{0};
}  // namespace

REX_EXTERN(__imp__sub_82AB1C28);
REX_HOOK_RAW(sub_82AB1C28) {
  // XMemDecompress(context r3, destination r4, &destination size r5, source r6, source size r7): S_OK = 0.
  const uint32_t destino = ctx.r4.u32, p_tam = ctx.r5.u32, origen = ctx.r6.u32, tam_origen = ctx.r7.u32;
  if (REXCVAR_GET(gow2_lzx_nativo) && destino && p_tam && origen && tam_origen) {
    const uint32_t tam_destino = rex::memory::load_and_swap<uint32_t>(base + p_tam);
    if (DescomprimirLzx(base + origen, tam_origen, base + destino, tam_destino)) {
      const uint64_t n = g_lzx_nativos.fetch_add(1) + 1;
      if ((n & (n - 1)) == 0 && n >= 256) {
        REXLOG_INFO("[lzx] {} bloques descomprimidos en nativo, {} por el codigo recompilado", n,
                    g_lzx_recompilados.load());
      }
      ctx.r3.u64 = 0;
      return;
    }
    g_lzx_recompilados.fetch_add(1);
  }
  __imp__sub_82AB1C28(ctx, base);
}

// sub_8295B610  the audio mixer's ramped accumulate: dst[i] += src[i] * gain over 256 floats, with the gain of each of
//               the four vectors of a 64-byte group starting at start + k * step and every gain advancing by 4 * step
//               per group (16 groups), as the VMX128 code (lvx128 / vmaddfp / stvx128). 4 % of the audio thread.
//               NEON here; gow2_audio_mezcla_nativa = false goes back to the recompiled code.
REXCVAR_DEFINE_BOOL(gow2_audio_mezcla_nativa, true, "Gears2",
                    "La mezcla con rampa del audio del juego (sub_8295B610) en NEON en vez del codigo recompilado");

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

REX_EXTERN(__imp__sub_8295B610);
REX_HOOK_RAW(sub_8295B610) {
#if defined(__aarch64__)
  if (REXCVAR_GET(gow2_audio_mezcla_nativa)) {
    // lvx / stvx: 16-byte aligned (the low 4 bits ignored), big-endian floats.
    auto leer = [&](uint32_t ea) { return vreinterpretq_f32_u8(vrev32q_u8(vld1q_u8(base + (ea & ~0xFu)))); };
    auto escribir = [&](uint32_t ea, float32x4_t v) {
      vst1q_u8(base + (ea & ~0xFu), vrev32q_u8(vreinterpretq_u8_f32(v)));
    };
    const uint32_t d = ctx.r3.u32, s = ctx.r4.u32;
    const float32x4_t p1 = leer(ctx.r6.u32), p2 = vaddq_f32(p1, p1), p3 = vaddq_f32(p2, p1), p4 = vaddq_f32(p2, p2);
    float32x4_t g0 = leer(ctx.r5.u32), g1 = vaddq_f32(g0, p1), g2 = vaddq_f32(g0, p2), g3 = vaddq_f32(g0, p3);
    for (uint32_t b = 0; b < 16; ++b) {
      const uint32_t o = b * 64;
      escribir(d + o, vaddq_f32(vmulq_f32(leer(s + o), g0), leer(d + o)));
      escribir(d + o + 16, vaddq_f32(vmulq_f32(leer(s + o + 16), g1), leer(d + o + 16)));
      escribir(d + o + 32, vaddq_f32(vmulq_f32(leer(s + o + 32), g2), leer(d + o + 32)));
      escribir(d + o + 48, vaddq_f32(vmulq_f32(leer(s + o + 48), g3), leer(d + o + 48)));
      g0 = vaddq_f32(g0, p4);
      g1 = vaddq_f32(g1, p4);
      g2 = vaddq_f32(g2, p4);
      g3 = vaddq_f32(g3, p4);
    }
    return;
  }
#endif
  __imp__sub_8295B610(ctx, base);
}
