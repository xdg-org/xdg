#include "xdg/embree/ray_tracer.h"
#include "xdg/error.h"
#include "xdg/geometry_data.h"
#include "xdg/ray.h"
#include "xdg/tetrahedron_contain.h"


namespace xdg {

void error(void* dum, RTCError code, const char* str) {
  if (code != RTC_ERROR_NONE)
    fatal_error("Embree error: {}", str);
}

EmbreeRayTracer::EmbreeRayTracer()
{
  device_ = rtcNewDevice(nullptr);
  rtcSetDeviceErrorFunction(device_, (RTCErrorFunction)error, nullptr);
}

EmbreeRayTracer::~EmbreeRayTracer()
{
  rtcReleaseDevice(device_);
}

void EmbreeRayTracer::init()
{

}

RTCScene EmbreeRayTracer::create_embree_scene() {
  RTCScene rtcscene = rtcNewScene(device_);
  rtcSetSceneFlags(rtcscene, RTC_SCENE_FLAG_ROBUST);
  rtcSetSceneBuildQuality(rtcscene, RTC_BUILD_QUALITY_HIGH);
  return rtcscene;
}

std::pair<SurfaceTreeID, ElementTreeID>
EmbreeRayTracer::register_volume(const std::shared_ptr<MeshManager>& mesh_manager,
                                 MeshID volume_id)
{
  // set up ray tracing tree for boundary faces of the volume
  TreeID faces_tree = create_surface_tree(mesh_manager, volume_id);
  // set up point location tree for any volumetric elements
  TreeID element_tree = create_element_tree(mesh_manager, volume_id);
  return {faces_tree, element_tree};
}

SurfaceTreeID
EmbreeRayTracer::create_surface_tree(const std::shared_ptr<MeshManager>& mesh_manager,
                           MeshID volume_id)
{
  SurfaceTreeID tree = next_surface_tree_id();
  surface_trees_.push_back(tree);
  auto volume_scene = this->create_embree_scene();
  auto volume_surfaces = mesh_manager->get_volume_surfaces(volume_id);

  // allocate total storage for all the primtives in a volume
  size_t vol_face_count = 0;
  for (auto& surface_id : volume_surfaces) {
    if (!surface_to_geometry_map_.count(surface_id)) {
      vol_face_count += mesh_manager->get_surface_faces(surface_id).size();
    }
  }

  this->primitive_ref_storage_[volume_scene].resize(vol_face_count);
  auto& triangle_storage = this->primitive_ref_storage_[volume_scene];
  PrimitiveRef* tri_ref_ptr = triangle_storage.data();
  auto bump = bounding_box_bump(mesh_manager, volume_id);
  int storage_offset = 0;

  for (auto& surface : volume_surfaces) {
    RTCGeometry surface_geometry;
    std::shared_ptr<SurfaceUserData> surface_data;

    // Check if this surface already has cached geometry information
    if (!surface_to_geometry_map_.count(surface))
    { // First visit: Create new RTCGeometry and user data
      std::tie(surface_geometry, surface_data) = register_surface(mesh_manager, surface, volume_scene, storage_offset);
      surface_data->box_bump = bump; // set the box dilation value
    }
    else { // Second Visit: Recover existing RTCGeometry and user data
      surface_geometry = surface_to_geometry_map_[surface];
      surface_data = surface_user_data_map_.at(surface_geometry);
      // set the box dilation value to the larger of the two box bump values for
      // the volumes on either side of this surface
      surface_data->box_bump = std::max(surface_data->box_bump, bump);
      rtcAttachGeometry(volume_scene, surface_geometry);
    }

    // Set the correct parent TreeID
    auto [forward_parent, reverse_parent] = mesh_manager->surface_senses(surface);
    if (volume_id == forward_parent) {
      surface_data->forward_vol = tree;
    } else if (volume_id == reverse_parent) {
      surface_data->reverse_vol = tree;
    } else {
      fatal_error("Volume {} is not a parent of surface {}", volume_id, surface);
    }
  }

  rtcCommitScene(volume_scene);
  surface_volume_tree_to_scene_map_[tree] = volume_scene;
  return tree;
}

std::pair<RTCGeometry, std::shared_ptr<SurfaceUserData>>
EmbreeRayTracer::register_surface(const std::shared_ptr<MeshManager>& mesh_manager,
                                  MeshID surface,
                                  RTCScene& volume_scene,
                                  int& storage_offset)
{
  auto& triangle_storage = this->primitive_ref_storage_[volume_scene];
  PrimitiveRef* tri_ref_ptr = triangle_storage.data();
  auto surface_faces = mesh_manager->get_surface_faces(surface);
  size_t surf_face_count = surface_faces.size();

  // fill primitive refs
  for (size_t i = 0; i < surf_face_count; ++i) {
    triangle_storage[storage_offset + i].primitive_id = surface_faces[i];
  }

  // create new RTCGeometry for the surface
  auto surface_geometry = rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_USER);
  rtcSetGeometryUserPrimitiveCount(surface_geometry, surf_face_count);
  rtcAttachGeometry(volume_scene, surface_geometry);
  surface_to_geometry_map_[surface] = surface_geometry;

  // create new SurfaceUserData for the surface
  auto surface_data = std::make_shared<SurfaceUserData>();
  surface_data->surface_id = surface;
  surface_data->mesh_manager = mesh_manager.get();
  surface_data->prim_ref_buffer = tri_ref_ptr + storage_offset;
  surface_user_data_map_[surface_geometry] = surface_data;
  rtcSetGeometryUserData(surface_geometry, surface_data.get());

  // Set RTC callbacks
  rtcSetGeometryBoundsFunction(surface_geometry, (RTCBoundsFunction)&TriangleBoundsFunc, nullptr);
  rtcSetGeometryIntersectFunction(surface_geometry, (RTCIntersectFunctionN)&TriangleIntersectionFunc);
  rtcSetGeometryOccludedFunction(surface_geometry, (RTCOccludedFunctionN)&TriangleOcclusionFunc);
  rtcCommitGeometry(surface_geometry);

  // increment storage offset by number of faces in this surface
  storage_offset += surf_face_count;

  return {surface_geometry, surface_data};
}

ElementTreeID
EmbreeRayTracer::create_element_tree(const std::shared_ptr<MeshManager>& mesh_manager,
                                     MeshID volume)
{
  auto volume_elements = mesh_manager->get_volume_elements(volume);
  if (volume_elements.size() == 0) return TREE_NONE;

  // create a new geometry
  RTCScene volume_element_scene = create_embree_scene();
  // create primitive references for the volumetric elements
  this->primitive_ref_storage_[volume_element_scene].resize(volume_elements.size());
  auto& volume_element_storage = this->primitive_ref_storage_[volume_element_scene];
  for (int i = 0; i < volume_elements.size(); ++i) {
    auto& primitive_ref = volume_element_storage[i];
    primitive_ref.primitive_id = volume_elements[i];
  }

  RTCGeometry element_geometry = rtcNewGeometry(device_, RTC_GEOMETRY_TYPE_USER);
  rtcSetGeometryUserPrimitiveCount(element_geometry, volume_elements.size());
  unsigned int embree_geometry = rtcAttachGeometry(volume_element_scene, element_geometry);
  std::shared_ptr<VolumeElementsUserData> volume_elements_data = std::make_shared<VolumeElementsUserData>();
  volume_elements_data->volume_id = volume;
  volume_elements_data->mesh_manager = mesh_manager.get();
  volume_elements_data->prim_ref_buffer = volume_element_storage.data();
  this->volume_user_data_map_[element_geometry] = volume_elements_data;

  rtcSetGeometryUserData(element_geometry, volume_elements_data.get());

  rtcSetGeometryBoundsFunction(element_geometry, (RTCBoundsFunction)&VolumeElementBoundsFunc, nullptr);
  rtcSetGeometryIntersectFunction(element_geometry, (RTCIntersectFunctionN)&TetrahedronIntersectionFunc);
  rtcSetGeometryOccludedFunction(element_geometry, (RTCOccludedFunctionN)&TetrahedronOcclusionFunc);

  rtcCommitGeometry(element_geometry);
  rtcCommitScene(volume_element_scene);

  ElementTreeID tree = next_element_tree_id();
  element_trees_.push_back(tree);
  element_volume_tree_to_scene_map_[tree] = volume_element_scene;
  return tree;
}

void EmbreeRayTracer::create_global_surface_tree()
{
  if (global_surface_scene_ != nullptr) {
    rtcReleaseScene(global_surface_scene_);
  }
  global_surface_scene_ = create_embree_scene();

  for(auto& [geom, surface_data] : surface_user_data_map_) {
      rtcAttachGeometry(global_surface_scene_, geom);
  }

  rtcCommitScene(global_surface_scene_);
  SurfaceTreeID tree = next_surface_tree_id();
  surface_trees_.push_back(tree);
  surface_volume_tree_to_scene_map_[tree] = global_surface_scene_;
  global_surface_tree_ = tree;
}

void EmbreeRayTracer::create_global_element_tree()
{
  if (global_element_scene_ != nullptr) {
    rtcReleaseScene(global_element_scene_);
  }
  global_element_scene_ = create_embree_scene();

  for (auto& [vol_geom, data] : volume_user_data_map_) {
    rtcAttachGeometry(global_element_scene_, vol_geom);
  }
  rtcCommitScene(global_element_scene_);

  ElementTreeID tree = next_element_tree_id();
  element_trees_.push_back(tree);
  element_volume_tree_to_scene_map_[tree] = global_element_scene_;
  global_element_tree_ = tree;
}

MeshID EmbreeRayTracer::find_element(const Position& point) const
{
  return find_element(global_element_tree_, point);
}


MeshID EmbreeRayTracer::find_element(ElementTreeID tree,
                                     const Position& point) const
{

  if (!element_volume_tree_to_scene_map_.count(tree)) {
    warning(fmt::format("Tree {} does not have a point location tree", tree));
    return ID_NONE;
  }

  RTCScene scene = element_volume_tree_to_scene_map_.at(tree);

  RTCElementDualRay ray;
  ray.set_org(point);
  ray.set_dir({1.0, 0.0, 0.0});
  ray.set_tfar(0.0);
  ray.set_tnear(0.0);

  // fire an occlusion ray
  {
    rtcOccluded1(scene, (RTCRay*)&ray);
  }

  if (ray.dtfar != -INFTY) return ID_NONE;

  return ray.element;
}

bool EmbreeRayTracer::point_in_volume(SurfaceTreeID tree,
                                const Position& point,
                                const Direction* direction,
                                const std::vector<MeshID>* exclude_primitives) const
{
  RTCScene scene = surface_volume_tree_to_scene_map_.at(tree);
  RTCDualRayHit rayhit; // embree specfic rayhit struct (payload?)
  rayhit.ray.set_org(point);
  if (direction != nullptr) rayhit.ray.set_dir(*direction);
  else rayhit.ray.set_dir({1. / std::sqrt(2.0), 1 / std::sqrt(2.0), 0.0});
  rayhit.ray.rf_type = RayFireType::VOLUME;
  rayhit.ray.orientation = HitOrientation::ANY;
  rayhit.ray.set_tfar(INFTY);
  rayhit.ray.set_tnear(0.0);
  rayhit.ray.volume_tree = tree;

  if (exclude_primitives != nullptr) rayhit.ray.exclude_primitives = exclude_primitives;

  {
    rtcIntersect1(scene, (RTCRayHit*)&rayhit);
  }

  // if the ray hit nothing, the point is outside of the volume
  if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID) return false;

  // use the hit triangle normal to determine if the intersection is
  // exiting or entering
  return rayhit.ray.ddir.dot(rayhit.hit.dNg) > 0.0;
}

std::pair<double, MeshID>
EmbreeRayTracer::ray_fire(SurfaceTreeID tree,
                    const Position& origin,
                    const Direction& direction,
                    const double dist_limit,
                    HitOrientation orientation,
                    std::vector<MeshID>* const exclude_primitves)
{
  RTCScene scene = surface_volume_tree_to_scene_map_.at(tree);
  RTCDualRayHit rayhit;
  // set ray data
  rayhit.ray.set_org(origin);
  rayhit.ray.set_dir(direction);
  rayhit.ray.set_tfar(dist_limit);
  rayhit.ray.set_tnear(0.0);
  rayhit.ray.rf_type = RayFireType::VOLUME;
  rayhit.ray.orientation = orientation;
  rayhit.ray.mask = -1; // no mask
  rayhit.ray.volume_tree = tree;

  if (exclude_primitves != nullptr) rayhit.ray.exclude_primitives = exclude_primitves;

  // fire the ray
  {
    rtcIntersect1(scene, (RTCRayHit*)&rayhit);
    // TODO: I don't quite understand this...
    rayhit.hit.Ng_x *= -1.0;
    rayhit.hit.Ng_y *= -1.0;
    rayhit.hit.Ng_z *= -1.0;
  }

  if (rayhit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
    return {INFTY, ID_NONE};
  else

    if (exclude_primitves) exclude_primitves->push_back(rayhit.hit.primitive_ref->primitive_id);
    return {rayhit.ray.dtfar, rayhit.hit.surface};
}

std::pair<double, MeshID> EmbreeRayTracer::closest(SurfaceTreeID tree,
                                                   const Position& point)
{
  RTCScene scene = surface_volume_tree_to_scene_map_.at(tree);
  RTCDPointQuery query;
  query.set_point(point);

  RTCPointQueryContext context;
  rtcInitPointQueryContext(&context);

  rtcPointQuery(scene, &query, &context, (RTCPointQueryFunction)&TriangleClosestFunc, &scene);

  if (query.geomID == RTC_INVALID_GEOMETRY_ID) {
    return {INFTY, ID_NONE};
  }

  return {query.dradius, query.primitive_ref->primitive_id};
}

bool EmbreeRayTracer::occluded(SurfaceTreeID tree,
                         const Position& origin,
                         const Direction& direction,
                         double& distance) const
{
  RTCScene scene = surface_volume_tree_to_scene_map_.at(tree);
  RTCSurfaceDualRay ray;
  ray.set_org(origin);
  ray.set_dir(direction);
  ray.set_tfar(INFTY);
  ray.set_tnear(0.0);
  ray.rf_type = RayFireType::FIND_VOLUME;
  ray.orientation = HitOrientation::ANY;
  ray.flags = 0;
  ray.mask = -1; // no mask

  // fire the ray
  {
    rtcOccluded1(scene, (RTCRay*)&ray);
  }

  distance = ray.dtfar;
  return distance != INFTY;
}

} // namespace xdg