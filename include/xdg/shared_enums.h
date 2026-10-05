#ifndef XDG_SHARED_ENUMS_H
#define XDG_SHARED_ENUMS_H

namespace xdg {

  enum PointInVolume : int { 
    OUTSIDE = 0, 
    INSIDE = 1 
  };

  enum HitOrientation : int {
    ANY = -1,
    EXITING = 0,
    ENTERING = 1,
  };

  enum SurfaceBoundaryCondition : int {
    UNSET = -1,
    TRANSMISSION = 0, // Cross into the next volume
    VACUUM = 1,       // Kill particle on crossing
    REFLECTIVE = 2    // Reflect particle using the surface normal
  };

}

#endif // XDG_SHARED_ENUMS_H
