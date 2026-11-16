// stl includes
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

// testing includes
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

// xdg includes
#include "xdg/constants.h"
#include "xdg/xdg.h"

#include "util.h"

using namespace xdg;

// Compares the OpenFOAM backend against MOAB over the same geometry.
//
// The polyMesh fixtures are converted from the Exodus meshes the other
// backends use, by tools/exodus_to_polymesh.py in the test files repository.
// The conversion preserves point coordinates exactly, so any disagreement here
// is a difference in how the two backends read or traverse the geometry rather
// than a difference in the geometry itself. That makes this the only check in
// the suite that holds the OpenFOAM backend against a second implementation.
//
// Element numbering differs between the backends, so nothing is compared by
// element ID. Everything below is compared on geometry: measured volume, ray
// fire distances, total track length, and the multiset of segment lengths
// along a track.

namespace {

struct BackendPair {
  std::shared_ptr<XDG> moab;
  std::shared_ptr<XDG> openfoam;
};

BackendPair open_pair(const std::string& moab_file, const std::string& openfoam_dir)
{
  return {test::make_xdg_backend_fixture(MeshLibrary::MOAB, RTLibrary::EMBREE, moab_file).xdg,
          test::make_xdg_backend_fixture(MeshLibrary::OPENFOAM, RTLibrary::EMBREE, openfoam_dir).xdg};
}

//! A deterministic spray of parallel rays across a mesh
//!
//! Offsets are deliberately not multiples of the cell size. A ray lying exactly
//! in the plane of a cell face grazes the interface, and which of the two
//! adjacent cells is credited with the traversal is then a tie-break rather
//! than a geometric fact, so the two backends may legitimately decompose it
//! differently. Total length still agrees, which the last test here checks
//! directly.
std::vector<std::pair<Position, Position>> ray_spray(const BoundingBox& box)
{
  const double lo_x = box.min_x - 5.0;
  const double hi_x = box.max_x + 5.0;
  const double span_y = box.max_y - box.min_y;
  const double span_z = box.max_z - box.min_z;

  std::vector<std::pair<Position, Position>> rays;
  for (int i = 1; i < 8; i++) {
    for (int j = 1; j < 8; j++) {
      const double y = box.min_y + span_y * (i / 8.0 + 0.0137);
      const double z = box.min_z + span_z * (j / 8.0 - 0.0219);
      rays.push_back({{lo_x, y, z}, {hi_x, y, z}});
    }
  }
  return rays;
}

//! Rays between random points around the mesh, so in random directions
//!
//! The parallel spray above meets cell faces at a handful of angles; these
//! cross them at every angle. The seed is fixed, so the rays are the same on
//! every run.
std::vector<std::pair<Position, Position>> random_rays(const BoundingBox& box, int n)
{
  std::mt19937 rng(42);
  auto around = [&](double lo, double hi) {
    const double pad = 0.2 * (hi - lo);
    return std::uniform_real_distribution<double>(lo - pad, hi + pad)(rng);
  };
  std::vector<std::pair<Position, Position>> rays;
  for (int i = 0; i < n; i++) {
    const Position a {around(box.min_x, box.max_x), around(box.min_y, box.max_y),
                      around(box.min_z, box.max_z)};
    const Position b {around(box.min_x, box.max_x), around(box.min_y, box.max_y),
                      around(box.min_z, box.max_z)};
    rays.push_back({a, b});
  }
  return rays;
}

double total_length(const std::vector<std::pair<MeshID, double>>& segments)
{
  double total = 0.0;
  for (const auto& [element, length] : segments) total += length;
  return total;
}

std::vector<double> sorted_lengths(const std::vector<std::pair<MeshID, double>>& segments)
{
  std::vector<double> lengths;
  lengths.reserve(segments.size());
  for (const auto& [element, length] : segments) lengths.push_back(length);
  std::sort(lengths.begin(), lengths.end());
  return lengths;
}

//! Run the full geometric comparison over one pair of equivalent meshes
void compare(const BackendPair& pair, int expected_elements)
{
  const auto& moab = pair.moab;
  const auto& openfoam = pair.openfoam;

  // the same mesh, read two ways
  REQUIRE(moab->mesh_manager()->num_volume_elements() == expected_elements);
  REQUIRE(openfoam->mesh_manager()->num_volume_elements() == expected_elements);
  REQUIRE(moab->mesh_manager()->num_vertices() ==
          openfoam->mesh_manager()->num_vertices());

  REQUIRE_THAT(openfoam->measure_volume(1),
               Catch::Matchers::WithinRel(moab->measure_volume(1), 1e-12));

  const BoundingBox box = moab->mesh_manager()->global_bounding_box();
  auto rays = ray_spray(box);
  const auto random = random_rays(box, 64);
  rays.insert(rays.end(), random.begin(), random.end());

  int fired = 0;
  int tracked = 0;
  for (const auto& [start, end] : rays) {
    Direction u = end - start;
    u.normalize();

    const auto moab_hit = moab->ray_fire(1, start, u);
    const auto openfoam_hit = openfoam->ray_fire(1, start, u);
    REQUIRE((moab_hit.second == ID_NONE) == (openfoam_hit.second == ID_NONE));
    if (moab_hit.second != ID_NONE) {
      REQUIRE_THAT(openfoam_hit.first,
                   Catch::Matchers::WithinAbs(moab_hit.first, 1e-9));
      fired++;
    }

    const auto moab_segments = moab->segments(start, end);
    const auto openfoam_segments = openfoam->segments(start, end);
    if (moab_segments.empty()) continue;

    REQUIRE_THAT(total_length(openfoam_segments),
                 Catch::Matchers::WithinAbs(total_length(moab_segments), 1e-9));

    // away from the cell faces the decomposition itself should match, element
    // for element, even though the numbering does not
    REQUIRE(openfoam_segments.size() == moab_segments.size());
    const auto moab_lengths = sorted_lengths(moab_segments);
    const auto openfoam_lengths = sorted_lengths(openfoam_segments);
    for (size_t k = 0; k < moab_lengths.size(); k++) {
      REQUIRE_THAT(openfoam_lengths[k],
                   Catch::Matchers::WithinAbs(moab_lengths[k], 1e-9));
    }
    tracked++;
  }

  // the spray has to actually have hit something for any of this to mean much
  REQUIRE(fired > 40);
  REQUIRE(tracked > 40);
}

//! The volume each element belongs to, leaving out the implicit complement
std::map<MeshID, MeshID> volume_of_elements(const std::shared_ptr<XDG>& xdg)
{
  const auto& mm = xdg->mesh_manager();
  std::map<MeshID, MeshID> volume_of;
  for (const MeshID volume : mm->volumes()) {
    if (volume == mm->implicit_complement()) continue;
    for (const MeshID element : mm->get_volume_elements(volume)) {
      volume_of[element] = volume;
    }
  }
  return volume_of;
}

//! Total area of the surfaces separating volumes \p a and \p b
double interface_area(const std::shared_ptr<XDG>& xdg, MeshID a, MeshID b)
{
  const auto& mm = xdg->mesh_manager();
  double area = 0.0;
  for (const MeshID surface : mm->surfaces()) {
    const auto [forward, reverse] = mm->surface_senses(surface);
    if ((forward == a && reverse == b) || (forward == b && reverse == a)) {
      area += xdg->measure_surface_area(surface);
    }
  }
  return area;
}

} // namespace

TEST_CASE("Cross-check OpenFOAM against MOAB on a hexahedral mesh")
{
  // an ASCII polyMesh, so this also covers the plain faceList reader path
  openfoam_fixture("polymesh-regularized-hex");
  compare(open_pair("regularized_hex_mesh.h5m", "polymesh-regularized-hex"), 1000);
}

TEST_CASE("Cross-check OpenFOAM against MOAB on a tetrahedral mesh")
{
  // a binary polyMesh, so this also covers the faceCompactList reader path
  openfoam_fixture("polymesh-regularized-tet");
  compare(open_pair("regularized_tet_mesh.h5m", "polymesh-regularized-tet"), 12000);
}

TEST_CASE("A ray grazing a cell face agrees on length if not on decomposition")
{
  // The hexahedral fixture spans -10 to 10 in cells two units wide, so y = 0
  // lies exactly in the plane of a cell face. A ray along it runs down the
  // interface, and crediting the traversal to one side or the other is a
  // tie-break rather than a geometric fact. The backends may therefore report
  // a different number of segments -- but the total distance through the mesh
  // is a property of the geometry, and must still agree.
  openfoam_fixture("polymesh-regularized-hex");
  const auto pair = open_pair("regularized_hex_mesh.h5m", "polymesh-regularized-hex");

  const Position start {-30.0, 0.0, 2.1};
  const Position end {30.0, 0.0, 2.1};

  const auto moab_segments = pair.moab->segments(start, end);
  const auto openfoam_segments = pair.openfoam->segments(start, end);

  REQUIRE_FALSE(moab_segments.empty());
  REQUIRE_FALSE(openfoam_segments.empty());
  REQUIRE_THAT(total_length(openfoam_segments),
               Catch::Matchers::WithinAbs(total_length(moab_segments), 1e-9));
  REQUIRE_THAT(total_length(moab_segments),
               Catch::Matchers::WithinAbs(20.0, 1e-9));
}

TEST_CASE("Cross-check OpenFOAM against MOAB across two volumes and their interface")
{
  // A binary polyMesh whose two cellZones were converted from the Exodus
  // element blocks: a shell spanning |x|, |y| <= 5 around a column spanning
  // |x|, |y| <= 4, both running the full height in z. The single-volume
  // fixtures cannot exercise zones, interface surfaces or the faces flipped
  // along an interface; this one holds all three against MOAB.
  openfoam_fixture("polymesh-cube-multiblock");
  const auto pair = open_pair("cube-w-multiblock-sideset.h5m", "polymesh-cube-multiblock");
  compare(pair, 13000);

  // the zones come out as the same two volumes, in the same order
  for (const MeshID volume : {1, 2}) {
    CAPTURE(volume);
    REQUIRE(pair.openfoam->mesh_manager()->num_volume_elements(volume) ==
            pair.moab->mesh_manager()->num_volume_elements(volume));
    REQUIRE_THAT(pair.openfoam->measure_volume(volume),
                 Catch::Matchers::WithinRel(pair.moab->measure_volume(volume), 1e-12));
  }
  REQUIRE_THAT(pair.openfoam->measure_volume(1), Catch::Matchers::WithinRel(360.0, 1e-12));
  REQUIRE_THAT(pair.openfoam->measure_volume(2), Catch::Matchers::WithinRel(640.0, 1e-12));

  // the interface between them: four column walls of 8 by 10
  const double interface = interface_area(pair.moab, 1, 2);
  REQUIRE_THAT(interface, Catch::Matchers::WithinRel(320.0, 1e-12));
  REQUIRE_THAT(interface_area(pair.openfoam, 1, 2),
               Catch::Matchers::WithinRel(interface, 1e-12));

  // track length attributed volume by volume, which only agrees if every cell
  // landed in the right zone
  const auto moab_volume_of = volume_of_elements(pair.moab);
  const auto openfoam_volume_of = volume_of_elements(pair.openfoam);
  int crossed = 0;
  for (const auto& [start, end] : ray_spray(pair.moab->mesh_manager()->global_bounding_box())) {
    std::map<MeshID, double> moab_length;
    std::map<MeshID, double> openfoam_length;
    for (const auto& [element, length] : pair.moab->segments(start, end)) {
      moab_length[moab_volume_of.at(element)] += length;
    }
    for (const auto& [element, length] : pair.openfoam->segments(start, end)) {
      openfoam_length[openfoam_volume_of.at(element)] += length;
    }
    for (const MeshID volume : {1, 2}) {
      REQUIRE_THAT(openfoam_length[volume],
                   Catch::Matchers::WithinAbs(moab_length[volume], 1e-9));
    }
    if (moab_length[1] > 0.0 && moab_length[2] > 0.0) crossed++;
  }
  REQUIRE(crossed > 20);

  // fired from inside the column, rays leave through the interface sideways
  // and through the boundary at the ends, at the same distance either way
  const Position inside_column {0.1, 0.2, 0.3};
  const std::vector<Direction> directions {
    {1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 1.0, 0.0},
    {0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, -1.0}};
  for (const auto& u : directions) {
    CAPTURE(u.x, u.y, u.z);
    const auto moab_hit = pair.moab->ray_fire(2, inside_column, u);
    const auto openfoam_hit = pair.openfoam->ray_fire(2, inside_column, u);
    REQUIRE(moab_hit.second != ID_NONE);
    REQUIRE(openfoam_hit.second != ID_NONE);
    REQUIRE_THAT(openfoam_hit.first, Catch::Matchers::WithinAbs(moab_hit.first, 1e-9));
  }

  // and a point is placed in the same volume by both
  const Direction up {0.0, 0.0, 1.0};
  REQUIRE(pair.openfoam->find_volume(inside_column, up) == 2);
  REQUIRE(pair.moab->find_volume(inside_column, up) == 2);
  const Position inside_shell {4.5, 0.1, 0.2};
  REQUIRE(pair.openfoam->find_volume(inside_shell, up) == 1);
  REQUIRE(pair.moab->find_volume(inside_shell, up) == 1);
}
