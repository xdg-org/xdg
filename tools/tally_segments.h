#include <memory>
#include <string>
#include <vector>
#include <numeric>
#include <omp.h>

#include <indicators/block_progress_bar.hpp>

#include "xdg/error.h"
#include "xdg/util/progress_bars.h"
#include "xdg/vec3da.h"
#include "xdg/timer.h"

#include "xdg/xdg.h"


using namespace xdg;

struct TallyContext {
  std::shared_ptr<XDG> xdg_;
  int n_threads_ {1};
  int n_tracks_ {0};
  bool check_tracks_ {false};
  bool verbose_ {false};
  bool quiet_ {false};
};

Position sample_box_location(const BoundingBox& bbox) {
  return bbox.lower_left() + bbox.width() * Vec3da(drand48(), drand48(), drand48());
}

void tally_segments(const TallyContext& context) {
  const auto& xdg = context.xdg_;

  // get the bounding box of the mesh
  BoundingBox bbox = xdg->mesh_manager()->global_bounding_box();
  std::cout << fmt::format("Mesh Bounding Box: {}", bbox) << "\n";

  using namespace indicators;
  auto prog_bar = block_progress_bar(fmt::format("Running {} tally tracks", context.n_tracks_));

  omp_set_num_threads(context.n_threads_);
  std::cout << fmt::format("Using {} threads", context.n_threads_) << "\n";

  int n_tracks_run = 0;

  Timer timer;
  timer.start();
  #pragma omp parallel shared(n_tracks_run)
  {
    #pragma omp for
    for (int i = 0; i < context.n_tracks_; i++) {
      // sample a location within the bounding box
      Position r1 = sample_box_location(bbox);
      if (!bbox.contains(r1)) fatal_error(fmt::format("Point {} is not within the mesh bounding box", r1));

      Position r2 = sample_box_location(bbox);
      if (!bbox.contains(r2)) fatal_error(fmt::format("Point {} is not within the mesh bounding box", r2));

      auto segments = xdg->segments(r1, r2);
      #pragma omp atomic
      n_tracks_run++;

      if (context.quiet_) continue;

      if (context.verbose_) {
          std::cout << fmt::format("Track {}: {} segments", i, segments.size()) << "\n";
      } else {
          prog_bar.set_progress(100.0 * (double)n_tracks_run / (double)context.n_tracks_);
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
  }
  timer.stop();

  if (!context.quiet_) prog_bar.mark_as_completed();

  std::cout << fmt::format("Time elapsed: {} s", timer.elapsed()) << "\n";
}
