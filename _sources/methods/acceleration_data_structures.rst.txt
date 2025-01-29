

Acceleration Data Structures
============================

XDG relies primarily on the bounding volume hierarchy (BVH) data structure for
accelerating ray tracing operations. The BVH is a hierarchical data structure that
organizes primitives (mesh elements) in a scene into a tree of bounding volumes.
The BVH is used to accelerate ray intersection tests by allowing the ray to
quickly traverse the tree and only test intersections with primitives that are
likely to be hit.

Based on the XDG design philosophy (see :ref:`design_philosophy`), the BVH is
constructed by leveraging state-of-the-art ray tracing libraries. On CPUs, XDG
relies on the Embree ray tracing kernels for BVH construction and traversal. On
GPUs, XDG relies on :term:`GPRT` as a vendor-agnostic interface for ray tracing
pipelines that can leverage the GPU's hardware acceleration for BVH construction
or modern software-based implementations of BVH traversal.

Mixed Precision Ray Tracing
===========================

The paper "Hardware-Accelerated Ray Tracing of CAD-Based Geometry for Monte
Carlo Radiation Transport" discusses the use of mixed-precision algorithms
to efficiently handle complex CAD-based geometries in Monte Carlo radiation
transport simulations. The key contributions of the paper include leveraging
modern ray tracing kernels to significantly speed up the ray tracing process,
which is critical for handling the high computational demands of Monte Carlo
methods [1]_.

By integrating the techniques discussed in the paper, XDG achieves
faster BVH construction and traversal, leading to more efficient simulations.
This is particularly beneficial for applications involving complex geometries
and large-scale simulations, where traditional CPU-based methods may fall short
in terms of performance.

.. [1] P. Shriwise, P. Wilson, A. Davis, P. Romano, "Hardware-Accelerated Ray
       Tracing of CAD-Based Geometry for Monte Carlo Radiation Transport," in
       *IEEE Computing in Science and Engineering*, vol. 24, no. 2, pp. 52-61,
       February 2022, doi: 10.1109/MCSE.2022.3154656.