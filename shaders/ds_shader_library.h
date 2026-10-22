#pragma once

// The deko3d shader library (gow2_shaders.dslib): each original Xenos container with its SPIR-V. Built on the PC
// by ds_empaquetar, read by gow2/src/gow2_shaders.cpp on the console and by dksh/ds_dksh.

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace ds {

// The original container also identifies constants and interface. The physical
// code alone is not enough. It is looked up before the driver creates its object.
struct Shader {
  std::vector<uint8_t> original;
  std::vector<uint32_t> spirv;  // empty with CargarIndice: LeerSpirv reads it
  uint64_t huella = 0;
  bool vertices = false;
  uint64_t spirv_posicion = 0;  // CargarIndice: where its SPIR-V is in the file
  uint32_t spirv_palabras = 0;
};

class BibliotecaShaders {
 public:
  // Transactional load: an invalid file does not replace the library.
  // Pointers returned by Buscar last until the next successful load.
  void Cargar(std::span<const uint8_t> archivo);
  void Cargar(const std::filesystem::path& archivo);
  // Only the containers (the console with every shader: ~470 MB of SPIR-V stays in the file): each shader's SPIR-V
  // is read with LeerSpirv when it is first needed. No integrity hash over the whole file (it would read all of it);
  // each container's own hash and the SPIR-V header are still checked.
  void CargarIndice(const std::filesystem::path& archivo);
  // The SPIR-V of shaders()[i] (CargarIndice), validated; empty if it cannot be read. Thread-safe.
  std::vector<uint32_t> LeerSpirv(size_t i) const;
  const Shader* Buscar(std::span<const uint8_t> original) const;
  const std::vector<Shader>& shaders() const { return shaders_; }

 private:
  std::vector<Shader> shaders_;
  std::filesystem::path ruta_;  // CargarIndice's file
};

// Versioned local format, little-endian, with integrity checks.
// It contains private data derived from the game; it is not embedded in the executable.
std::vector<uint8_t> EmpaquetarShaders(std::vector<Shader> shaders);

}  // namespace ds
