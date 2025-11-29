
#include "xdg/util/linalg.h"
#include "xdg/vec3da.h"

#include "xdg/geometry/contain.h"

namespace xdg
{
  bool plucker_tet_containment_test(const Position& point,
                                  const Position& v0,
                                  const Position& v1,
                                  const Position& v2,
                                  const Position& v3) {
    using namespace linalg::aliases;
    // Create matrix T = [v1 - v0, v2 - v0, v3 - v0]
    Vec3da e0 = v1 - v0;
    Vec3da e1 = v2 - v0;
    Vec3da e2 = v3 - v0;
    double3x3 T = { {e0.x, e0.y, e0.z},
                   {e1.x, e1.y, e1.z},
                   {e2.x, e2.y, e2.z}};

    // Vector from v0 to point
    Vec3da rhs = point - v0;

    // Solve T * [λ1, λ2, λ3] = rhs
    double3 lambda123 = mul(inverse(T),{rhs.x, rhs.y, rhs.z});

    // Compute λ0
    double lambda0 = 1.0f - (lambda123.x + lambda123.y + lambda123.z);

    // Final barycentric coordinate vector
    double4 bary = { lambda0, lambda123.x, lambda123.y, lambda123.z };

    // Check all λ_i in [0, 1]
    for (int i = 0; i < 4; ++i) {
        if (bary[i] < -PLUCKER_ZERO_TOL || bary[i] > 1.0f + PLUCKER_ZERO_TOL)
            return false;
    }
    return true;
}
} // namespace xdg