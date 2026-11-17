#ifndef _XDG_OPENFOAM_FOAM_FILE
#define _XDG_OPENFOAM_FOAM_FILE

#include <cstdint>
#include <string>

#include "xdg/openfoam/foam_token_stream.h"

namespace xdg {

//! \brief Storage format of a FoamFile payload
enum class FoamFormat {
  ASCII,
  BINARY
};

//! \brief Machine representation declared by the FoamFile `arch` entry
//!
//! OpenFOAM records this as e.g. `arch "LSB;label=32;scalar=64"`, so binary
//! payloads never have to be read with guessed integer widths. Files written
//! by older versions may omit it, in which case these defaults apply.
struct FoamArch {
  bool little_endian {true};
  int label_bits {32};
  int scalar_bits {64};
};

//! \brief Mesh counts advertised by the FoamFile `note` entry
//!
//! Present on the polyMesh files that carry them (`owner`, `neighbour`), and
//! used both to reserve storage and to cross-check what was actually parsed.
//! A value of -1 means the count was not declared.
struct FoamNoteCounts {
  int64_t n_points {-1};
  int64_t n_cells {-1};
  int64_t n_faces {-1};
  int64_t n_internal_faces {-1};
};

//! \brief The parsed contents of a FoamFile header dictionary
struct FoamHeader {
  FoamFormat format {FoamFormat::ASCII};
  FoamArch arch;
  std::string class_name;
  std::string object;
  FoamNoteCounts counts;

  //! \brief Size in bytes of one label in a binary payload
  int label_bytes() const { return arch.label_bits / 8; }

  //! \brief Size in bytes of one scalar in a binary payload
  int scalar_bytes() const { return arch.scalar_bits / 8; }
};

//! \brief Read and consume the leading `FoamFile { ... }` dictionary
//! \param ts A stream positioned at the start of an OpenFOAM file
//! \return The parsed header
FoamHeader read_foam_header(FoamTokenStream& ts);

} // namespace xdg

#endif // include guard
