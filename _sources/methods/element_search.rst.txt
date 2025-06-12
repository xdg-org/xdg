

Element Search
==============

The underlying data structure for element search used by Embree is a Bouding
Volume Hierarchy (BVH), which is a tree of bounding boxes that is used to
quickly determine if a ray intersects a primitive. This data structure is
typically used to accelerate ray queries for the nearest intersection against
sets of 2D primitives.

This data structure can also be used to accelerate queries for the containment
of a point within a set of 3D primitives (elements in the parlance of XDG).

To perform an element search, we first need to build the BVH. This is done by
providing Embree with a a custom user geometry for the 3D elements with the
ability to call back into the relevant `MeshManager` to get the bounding box of
the each element. Because Embree operates in single precision, the bounding boxes
of elements are dilated conservatively and provided to embree in single precision.

When a point containment query is performed, a ray is constructed from the point
and a length of zero, resulting in positive ray-box intersections only if the
point resides within the bounding box of the element. At the botton of the
hierarchy, the elements are identified and the primitive reference is returned.
Thus far computation occurs in single precision.

A double-precision evaluation is performed to determine if the point resides within
the element. This is done by computing the barycentric coordinates of the point
with respect to the element. If the point resides within the element, the primitive
reference is returned.

Because the point containment query departs from a the types of ray queries
performed in the common surface ray tracing required by particle transport (in
which one is interested in the nearest intersection along the particle's
trajectory), we here use a call to Embree's `rtcOccluded` function as it will
return the first positive containment result for the query. Given that a point
should not be contained by more than one element, this call is sufficient and
will result in fewer element containment checks in double-precision.






