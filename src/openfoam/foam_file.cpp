#include "xdg/openfoam/foam_file.h"

#include <cstdlib>
#include <vector>

#include "xdg/error.h"

namespace xdg {

namespace {

//! Split \p text on any of the characters in \p separators, dropping empties
std::vector<std::string> split(const std::string& text, const std::string& separators)
{
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find_first_of(separators, start);
    const std::string part = text.substr(start, end == std::string::npos ? std::string::npos
                                                                         : end - start);
    if (!part.empty()) parts.push_back(part);
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return parts;
}

//! Parse an `arch` value such as `LSB;label=32;scalar=64`
FoamArch parse_arch(const std::string& value, const FoamTokenStream& ts)
{
  FoamArch arch;
  for (const auto& field : split(value, ";")) {
    if (field == "LSB") {
      arch.little_endian = true;
    } else if (field == "MSB") {
      arch.little_endian = false;
    } else if (field.rfind("label=", 0) == 0) {
      arch.label_bits = std::atoi(field.c_str() + 6);
    } else if (field.rfind("scalar=", 0) == 0) {
      arch.scalar_bits = std::atoi(field.c_str() + 7);
    }
  }

  if (arch.label_bits != 32 && arch.label_bits != 64) {
    ts.error(fmt::format("Unsupported label width of {} bits", arch.label_bits));
  }
  if (arch.scalar_bits != 32 && arch.scalar_bits != 64) {
    ts.error(fmt::format("Unsupported scalar width of {} bits", arch.scalar_bits));
  }
  return arch;
}

//! Parse a `note` value such as `nPoints:12  nCells:2  nFaces:11  nInternalFaces:1`
FoamNoteCounts parse_note(const std::string& value)
{
  FoamNoteCounts counts;
  for (const auto& field : split(value, " \t")) {
    const size_t colon = field.find(':');
    if (colon == std::string::npos) continue;

    const std::string key = field.substr(0, colon);
    const int64_t count = std::atoll(field.c_str() + colon + 1);

    if (key == "nPoints") counts.n_points = count;
    else if (key == "nCells") counts.n_cells = count;
    else if (key == "nFaces") counts.n_faces = count;
    else if (key == "nInternalFaces") counts.n_internal_faces = count;
  }
  return counts;
}

} // namespace

FoamHeader read_foam_header(FoamTokenStream& ts)
{
  FoamHeader header;

  const std::string opening = ts.next();
  if (opening != "FoamFile") {
    ts.error(fmt::format("Expected a 'FoamFile' header, found '{}'", opening));
  }
  ts.expect("{");

  while (true) {
    const std::string key = ts.next();
    if (key == "}") break;

    // nested dictionaries such as `meta { names 2 ( fuel clad ); }` duplicate
    // information carried in the payload, so they are skipped
    if (ts.peek() == "{") {
      ts.skip_dict_block();
      continue;
    }

    std::vector<std::string> values;
    while (true) {
      const std::string token = ts.next();
      if (token == ";") break;
      values.push_back(token);
    }
    if (values.empty()) continue;

    if (key == "format") {
      if (values[0] == "ascii") {
        header.format = FoamFormat::ASCII;
      } else if (values[0] == "binary") {
        header.format = FoamFormat::BINARY;
      } else {
        ts.error(fmt::format("Unrecognized format '{}'", values[0]));
      }
    } else if (key == "arch") {
      header.arch = parse_arch(values[0], ts);
    } else if (key == "class") {
      header.class_name = values[0];
    } else if (key == "object") {
      header.object = values[0];
    } else if (key == "note") {
      header.counts = parse_note(values[0]);
    }
  }

  if (!header.arch.little_endian) {
    ts.error("Big-endian OpenFOAM files are not supported. Rewrite the mesh on "
             "a little-endian machine, or convert it to ASCII: set "
             "'writeFormat ascii;' in system/controlDict and run "
             "'foamFormatConvert -constant'.");
  }

  return header;
}

} // namespace xdg
