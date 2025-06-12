


Primitive Intersections
=======================

Currently XDG supports ray intersections with the following primitives:

   - linear triangles

Triangle intersection tests are performed using the Pluecker triangle
intersection test. This method uses Pluecker coordinates (formed from the
cross-product of the ray direction and triangle edges) to determine if a ray
intersects a triangle. It is known for its numerical stability and efficiency,
making it suitable for scientific ray tracing applications [1]_.


Assumptions about the normals of primitives are based the cannonical ordering
established by this paper [2]_.


.. [1] T. H. Havran, "Fast Ray-Tetrahedron Intersection Using Plucker Coordinates,"
       Journal of Graphics Tools, vol. 8, no. 1, pp. 25-32, 2003.
       http://dx.doi.org/10.1080/10867651.2003.10487593

.. [2] T. J. Tautges, "Canonical numbering systems for finite-element codes,"
       International Journal for Numerical Methods in Biomedical Engineering,
       vol. 26, no. 8, pp. 1033-1049, 2010. https://doi.org/10.1002/cnm.1237


