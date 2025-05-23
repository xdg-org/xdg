#include <memory>
#include <string>
#include <vector>
#include <numeric>

#include <indicators/block_progress_bar.hpp>

#include "xdg/error.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

using namespace xdg;

struct TallyContext {
  std::shared_ptr<XDG> xdg_;
  int n_tracks_ {0};
  bool check_tracks_ {false};
  bool verbose_ {false};
};

Position sample_box_location(const BoundingBox& bbox) {
  return bbox.lower_left() + bbox.width() * Vec3da(drand48(), drand48(), drand48());
}


void tally_segments(const TallyContext& context) {
  const auto& xdg = context.xdg_;

  // get the bounding box of the mesh
  BoundingBox bbox = xdg->mesh_manager()->global_bounding_box();
  std::cout << fmt::format("Mesh Bounding Box: {}", bbox) << std::endl;

  using namespace indicators;
  BlockProgressBar prog_bar{
    option::BarWidth{50},
    option::Start{"["},
    option::End{"]"},
    option::PostfixText{fmt::format("Running {} tally tracks", context.n_tracks_)},
    option::ForegroundColor{Color::green},
    option::ShowPercentage{true},
    option::FontStyles{std::vector<FontStyle>{FontStyle::bold}}
  };

  for (int i = 0; i < context.n_tracks_; i++) {
    // sample a location within the bounding box
    Position r1 = sample_box_location(bbox);
    if (!bbox.contains(r1)) fatal_error(fmt::format("Point {} is not within the mesh bounding box", r1));

    Position r2 = sample_box_location(bbox);
    if (!bbox.contains(r2)) fatal_error(fmt::format("Point {} is not within the mesh bounding box", r2));

    auto segments = xdg->segments(r1, r2);
    if (context.verbose_) {
      std::cout << fmt::format("Track {}: {} segments", i, segments.size()) << std::endl;
    } else {
      prog_bar.set_progress(100.0 * (double)i / (double)context.n_tracks_);
    } 

    if (context.check_tracks_) {
      double track_length = (r2 - r1).length();
      double segement_sum = std::accumulate(segments.begin(), segments.end(), 0.0, [](double v, const auto& s) { return v + s.second; });
      double diff = fabs(track_length - segement_sum);
      if (diff > TINY_BIT) {
        fatal_error(fmt::format("Track length check failed.\n Start: {}\n End: {}\n Diff: {}", r1, r2, diff));
      }
    }
  }
  prog_bar.mark_as_completed();
}
