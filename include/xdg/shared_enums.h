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

}

#endif // XDG_SHARED_ENUMS_H