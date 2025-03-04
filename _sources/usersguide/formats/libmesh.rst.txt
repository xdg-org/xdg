.. _libmesh_format:

libMesh File Format
===================

XDG supports ray tracing of the subdomain interfaces (element faces that are the
boundary between subdomains or are on the boundary of the mesh) in a
:term:`libMesh` mesh. The input mesh is expected to be volumetric. The faces of
elements on the boundary of these subdomains are treated as surfaces in the XDG
geometry by shelling the volumetric elements of the subdomain. The element faces
that lie between subdomain boundaries (i.e. faces at the interface of two
different subdomains) are discovered automatically and surfaces are created in
the XDG topology with a transmission boundary condition.

Sidesets with names that match supported boundary conditions
("boundary:reflective", "boundary:vacuum", etc.) are treated as such in XDG's
metadata system. Such user-defined sidesets can partially replace discovered
interface surfaces. It is expected that user-defined sidesets with names that
XDG will recognize contain faces between no more than two subdomains.

Current restrictions:

  - meshes of linear tetrahedra
