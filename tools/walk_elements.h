#include <memory>
#include <string>
#include <vector>
#include <numeric>
#include <functional>
#include <omp.h>

#include "xdg/error.h"
#include "xdg/vec3da.h"
#include "xdg/timer.h"
#include "xdg/bbox.h"

#include "xdg/xdg.h"

using namespace xdg;

struct WalkElementsContext {
  std::shared_ptr<XDG> xdg_;
  int n_threads_ {1};
  double mean_free_path_;
  size_t n_particles_;
  bool verbose_;
  bool quiet_;
  std::function<void(size_t, int, double, int)> on_particle_complete_ {};
};

void walk_elements(const WalkElementsContext& context) {
  auto xdg = context.xdg_;
  double mean_free_path = context.mean_free_path_;

  // get the bounding box of the mesh
  BoundingBox bbox = xdg->mesh_manager()->global_bounding_box();
  std::cout << fmt::format("Mesh Bounding Box: {}", bbox) << "\n";

  double total_distance = 0.0;

  int n_particles_run = 0;

  // set random seeds for consistency
  seed_rand_double(12345);

  #ifdef XDG_OPENMP
    omp_set_num_threads(context.n_threads_);
    std::cout << fmt::format("Using {} threads", context.n_threads_) << "\n";
  #else
    if (context.n_threads_ != 1) {
      std::cout << "Warning: OpenMP not enabled; running in single-threaded mode\n";
    }
  #endif

  double max_distance = bbox.max_chord_length();

  Timer timer;
  timer.start();
  #pragma omp parallel shared(n_particles_run)
  {
    // Each thread needs its own random number state
    double thread_total_distance = 0.0;

    #pragma omp for
    for (int i = 0; i < context.n_particles_; i++) {
      int n_events = 0;
      double distance = 0.0;
      MeshID element = ID_NONE;
      Position r;

      // sample a location within the model
      while (element == ID_NONE) {
        r = bbox.sample_location();
        element = xdg->find_element(r);
      }

      Direction u = rand_dir();
      u.normalize();
      std::vector<MeshID> primitives;
      while (element != ID_NONE) {
        // determine the distace to the next element
        auto [next_element, exit_distance] = xdg->next_element(element, r, u);
        if (exit_distance > max_distance) {
          fatal_error(fmt::format("Particle {}, exit distance is too large: {}. Location {}", i, exit_distance, r));
        }
        // determine the distance to the next collision
        double collision_distance = -std::log(1.0 - drand48()) * mean_free_path;

        if (collision_distance < exit_distance) {
          r += u * collision_distance;
          distance += collision_distance;
          // simulate an isotropic collision
          u = rand_dir();
        } else {
          r += u * exit_distance;
          distance += exit_distance;
          // if the next element isn't present, move on to the next particle
          element = next_element;
        }

        // attempt to trace the mesh boundary for re-entrance
        while (element == ID_NONE) {
          auto ray_hit = xdg->ray_fire(xdg->mesh_manager()->implicit_complement(), r, u, INFTY, HitOrientation::EXITING, &primitives);
          // if there is no re-entry point, move on to the next particle
          if (ray_hit.second == ID_NONE) break;

          // move ray up to surface
          r += u * ray_hit.first;
          distance += ray_hit.first;
          // TODO: if we intersected a corner, we might not find an element
          element = xdg->find_element(r+ u*TINY_BIT);
        }
        primitives.clear();
        n_events++;
      }
      thread_total_distance += distance;
      #pragma omp atomic
      n_particles_run++;
      if (context.on_particle_complete_) {
        #pragma omp critical
        context.on_particle_complete_(i, n_events, distance, n_particles_run);
      }
    }

    #pragma omp atomic
    total_distance += thread_total_distance;
  }
  timer.stop();

  std::cout << fmt::format("Time elapsed: {} s", timer.elapsed()) << "\n";

  if (context.verbose_ && !context.quiet_) {
    std::cout << fmt::format("Average distance: {}", total_distance/context.n_particles_) << "\n";
  }
}
