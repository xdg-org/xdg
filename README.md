
[![CI](https://github.com/xdg-org/xdg/actions/workflows/ci.yml/badge.svg)](https://github.com/xdg-org/xdg/actions/workflows/ci.yml)

Accelerated Discretized Geometry
--------------------------------

An API for interacting with surface discretizations of CAD geometry for particle transport and method of characteristics applications for solving PDEs.

[Documentation can be found here](https://xdg-org.github.io/xdg/)

Currently limited to facet-based geometry composed of triangles or tetrahedra.

XDG stands on the shoulders of the [Direct Accelerated Geometry Monte Carlo
(DAGMC)](https://svalinn.github.io/DAGMC/index.html>) toolkit and it's
development team, from which it draws significant guidance and inspiration. The
intent of this library is to provide expanded DAGMC functionality across
multiple mesh libraries and formats. Various members of the DAGMC development
team are collaborators and contributors to the XDG project.

Benchmarking
------------

XDG is currently being benchmarked against a couple of complex, open source models.

[Benchmarking results for XDG can be found here](https://xdg-org.github.io/xdg-benchmarking/)