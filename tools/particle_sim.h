#include <memory>
#include <string>
#include <vector>

#include "xdg/error.h"
#include "xdg/mesh_manager_interface.h"
#include "xdg/vec3da.h"
#include "xdg/xdg.h"

using namespace xdg;

struct SimulationData {
  std::shared_ptr<XDG> xdg_;
  double mfp_ {1.0};
  uint32_t n_particles_ {100};
  uint32_t max_events_ {1000};
  bool verbose_particles_ {false};
  bool implicit_complement_is_graveyard_ {false};
  std::unordered_map<MeshID, double> cell_tracks;
};

struct Particle {

Particle(std::shared_ptr<XDG> xdg, uint32_t id, uint32_t max_events, bool verbose=true, bool ipc_graveyard=false)
: verbose_(verbose), xdg_(xdg), id_(id), max_events_(max_events), ipc_graveyard_(ipc_graveyard) {}

template<typename... Params>
void log (const std::string& msg, const Params&... fmt_args) {
  if (!verbose_) return;
  write_message(msg, fmt_args...);
}

void initialize() {
  // TODO: replace with sampling
  r_ = {0.0, 0.0, 0.0};
  u_ = {1.0, 0.0, 0.0};

  volume_ = xdg_->find_volume(r_, u_);
  log("Particle {} initialized in volume {}", id_, volume_);
}

void surf_dist() {
  surface_intersection_ = xdg_->ray_fire(volume_, r_, u_, INFTY, HitOrientation::EXITING, &history_);
  if (surface_intersection_.first == 0.0) {
    fatal_error("Particle {} stuck at position ({}, {}, {}) on surfacce {}", id_, r_.x, r_.y, r_.z, surface_intersection_.second);
    alive_ = false;
    return;
  }
  if (surface_intersection_.second == ID_NONE) {
    fatal_error("Particle {} lost in volume {}", id_, volume_);
    alive_ = false;
    return;
  }
  log("Intersected surface {} at distance {} ", surface_intersection_.second, surface_intersection_.first);
}

void sample_collision_distance(double mfp) {
  collision_distance_ = -std::log(1.0 - drand48()) * mfp;
}

void collide() {
  n_events_++;
  log("Event {} for particle {}", n_events_, id_);
  u_ = rand_dir();
  log("Particle {} collides with material at position ({}, {}, {}), new direction is ({}, {}, {})", id_, r_.x, r_.y, r_.z, u_.z, u_.y, u_.z);
  history_.clear();
}

void advance(std::unordered_map<MeshID, double>& cell_tracks)
{
  log("Comparing surface intersection distance {} to collision distance {}", surface_intersection_.first, collision_distance_);
  if (collision_distance_ < surface_intersection_.first) {
    r_ += collision_distance_ * u_;
    cell_tracks[volume_] += collision_distance_;
    log("Particle {} collides with material at position ({}, {}, {}) ", id_, r_.x, r_.y, r_.z);
  } else {
    r_ += surface_intersection_.first * u_;
    cell_tracks[volume_] += surface_intersection_.first;
    log("Particle {} advances to surface {} at position ({}, {}, {}) ", id_, surface_intersection_.second, r_.x, r_.y, r_.z);
  }
}

void cross_surface()
{
  n_events_++;
  log("Event {} for particle {}", n_events_, id_);
  auto boundary_condition = xdg_->mesh_manager()->get_surface_property(surface_intersection_.second, PropertyType::BOUNDARY_CONDITION);
  // check for the surface boundary condition
  if (boundary_condition.value == "reflecting" || boundary_condition.value == "reflective") {
    log("Particle {} reflects off surface {}", id_, surface_intersection_.second);
    log("Direction before reflection: ({}, {}, {})", u_.x, u_.y, u_.z);

    Direction normal = xdg_->surface_normal(surface_intersection_.second, r_, &history_);
    log("Normal to surface: ({}, {}, {})", normal.x, normal.y, normal.z);

    double proj = dot(normal, u_);
    double mag = normal.length();
    normal = normal * (2.0 * proj/mag);
    u_ = u_ - normal;
    u_ = u_.normalize();
    log("Direction after reflection: ({}, {}, {})", u_.x, u_.y, u_.z);
    // reset to last intersection
    if (history_.size() > 0) {
      log("Resetting particle history to last intersection");
      history_ = {history_.back()};
    }
  } else if (boundary_condition.value == "vacuum") {
    log("Particle {} encounters vacuum boundary at surface {}", id_, surface_intersection_.second);
    alive_ = false;
  } else {
    volume_ = xdg_->mesh_manager()->next_volume(volume_, surface_intersection_.second);
    log("Particle {} enters volume {}", id_, volume_);
    if (ipc_graveyard_ && volume_ == xdg_->mesh_manager()->implicit_complement()) volume_ = ID_NONE;
    if (volume_ == ID_NONE) {
      alive_ = false;
      return;
    }
  }
}

// Data Members
bool verbose_ {true};
std::shared_ptr<XDG> xdg_;
uint32_t id_ {0};
int32_t max_events_ {1000};
bool ipc_graveyard_ {false};

Position r_;
Direction u_;
MeshID volume_ {ID_NONE};
std::vector<MeshID> history_ {};
std::pair<double, MeshID> surface_intersection_ {INFTY, ID_NONE};
double collision_distance_ {INFTY};
int32_t n_events_ {0};
bool alive_ {true};
};

void transport_particles(SimulationData& sim_data) {
  // Problem Setup
  srand48(42);
  for (uint32_t i = 0; i < sim_data.n_particles_; i++) {
    Particle p {sim_data.xdg_, i, sim_data.max_events_, sim_data.verbose_particles_, sim_data.implicit_complement_is_graveyard_};
    p.initialize();
    while (p.alive_) {
      p.surf_dist();
      p.sample_collision_distance(sim_data.mfp_);
      p.advance(sim_data.cell_tracks);
      if (p.collision_distance_ < p.surface_intersection_.first) {
        p.collide();
      } else {
        p.cross_surface();
      }
    }
  }
}