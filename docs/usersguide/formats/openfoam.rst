.. _openfoam_format:

OpenFOAM® File Format
=====================

XDG reads OpenFOAM® meshes directly from a ``polyMesh`` directory. Unlike the
other mesh backends this one links no external library, so OpenFOAM does not
need to be installed to use it.

.. note::

   OPENFOAM® is a registered trade mark of OpenCFD Limited, producer and
   distributor of the OpenFOAM software via www.openfoam.com.

   This offering is not approved or endorsed by OpenCFD Limited, producer and
   distributor of the OpenFOAM software via www.openfoam.com, and owner of the
   OPENFOAM® and OpenCFD® trade marks.

Either a case directory or a ``polyMesh`` directory may be given. The following
are equivalent::

    mesh_manager->load_file("/path/to/case");
    mesh_manager->load_file("/path/to/case/constant/polyMesh");

Given a case directory, the mesh in ``constant/polyMesh`` is read. A mesh that a
utility such as ``refineMesh`` wrote into a time directory is not picked up;
pass that ``polyMesh`` directory directly, for example ``/path/to/case/1/polyMesh``.

Both ASCII and binary meshes are read, in either 32-bit or 64-bit label builds.
Compressed (``.gz``) meshes are not supported. Decompress the files with
``gunzip``, or set ``writeCompression off;`` in ``system/controlDict`` and run
``foamFormatConvert -constant``, which rewrites the mesh uncompressed.

Why polyMesh maps cleanly onto XDG
----------------------------------

A ``polyMesh`` already stores the object XDG builds its topology from: a face
with a cell on each side, wound so that its normal runs from the ``owner`` cell
into the ``neighbour`` cell. Volumes, surfaces and senses therefore follow from
the file rather than having to be discovered, as they do for a mesh library that
stores only nodes and elements.

Volumes and materials
---------------------

Each ``cellZone`` becomes a volume, and the zone's name becomes the volume's
material. The first ``mat:`` in the name is removed, as for :term:`libMesh`
subdomain names, so ``fuel`` and ``mat:fuel`` are equivalent.

Cells belonging to no ``cellZone`` are collected into a single unnamed volume
which is treated as void, matching how an unnamed subdomain is handled for
:term:`libMesh` meshes. A mesh with no ``cellZones`` at all therefore loads as
one void volume, which is useful for geometry checks but not for transport.

Cell zones are created with the ``topoSet`` utility. They must not overlap:
each cell carries a single material, so a cell named by two zones is an error. A
zone that selects no cells describes no geometry; it is ignored, with a note.

Surfaces and boundary conditions
--------------------------------

Surfaces come from two places:

- **Boundary patches.** A patch that touches more than one volume is split into
  one surface per volume, since a patch is a user-facing grouping and is under
  no obligation to follow ``cellZone`` boundaries.
- **Cell zone interfaces.** Internal faces whose owner and neighbour fall in
  different volumes are grouped by volume pair. These carry a transmission
  boundary condition.

A boundary condition is set only by a ``boundary:<condition>`` name, for
example ``boundary:vacuum``, as with DAGMC group names: either the patch's own
name, or a patch group it belongs to. A group is the lighter touch, since it is
added in ``constant/polyMesh/boundary`` alone, while renaming a patch means
renaming it in every field file too::

    walls
    {
        type            wall;
        inGroups        List<word> 2(wall boundary:vacuum);
        nFaces          1200;
        startFace       45000;
    }

Ordinary patch and group names such as ``inlet``, ``walls`` or ``defaultFaces``
label the CFD model rather than state a transport condition, so they set none,
and those surfaces read as ``transmission``.

Patches of type ``symmetry``, ``symmetryPlane``, ``empty`` and ``wedge`` default
to a ``reflecting`` boundary condition. All four describe a plane the geometry is
invariant across, which is the same statement a reflecting boundary makes in
transport. A ``boundary:`` name or group always takes precedence over this.

A note lists the patches that get ``reflecting`` this way, and it is worth
reading: a CFD symmetry plane is a mirror for transport only if the transport
model shares that symmetry. A half-channel CFD mesh inside a full-core model,
for example, should transmit there, so give the patch a
``boundary:transmission`` group.

Two-dimensional and axisymmetric cases
--------------------------------------

OpenFOAM marks the collapsed direction of a two-dimensional case with ``empty``
patches, and the sides of an axisymmetric case with ``wedge`` patches. Both are
loaded rather than refused: the geometry really is invariant along that
direction, so reflecting boundaries on those patches record the infinite extent
the case stands for, the same construction a two-dimensional neutronics model
uses. Whether that matters depends on the application: an application that
uses XDG only as a tally mesh, as OpenMC does, sees a one-cell-thick slab.

A note is printed when this happens, since it is a substantial reading of what
the mesh means. To override it, give the patch a ``boundary:`` name or group.

This reading applies only when the mesh is one cell thick between those patches.
blockMesh also gives type ``empty`` to its default patch, for faces left out of
its boundary list, and on a three-dimensional mesh such a patch is just an
exterior surface: it gets no boundary condition, with a note saying why.

Axisymmetric cases carry a caveat: the cells that meet the axis of an OpenFOAM
wedge are usually degenerate, collapsing to prisms, and are rejected as
unsupported element types. An annular wedge that does not reach the axis loads
without trouble.

Units
-----

OpenFOAM works in meters, and XDG uses the coordinates as written. An
application that works in other units applies its own factor: in OpenMC, set
``length_multiplier=100`` on the unstructured mesh to work in centimeters.

Current restrictions
--------------------

  - meshes of linear tetrahedra and linear hexahedra

    Polyhedral and split-hexahedral cells are rejected. These are common in
    ``snappyHexMesh`` output, which produces them at refinement interfaces.

  - a single mesh region

    Multi-region cases store one mesh per region under
    ``constant/<region>/polyMesh``, with duplicated points at the region
    interfaces. XDG needs a single mesh, so either load one region on its own or
    build a single mesh whose materials are ``cellZones``.

  - no walls of zero thickness

    Two boundary faces in the same place, as ``createBaffles`` leaves them or
    as ``mergeMeshes`` leaves parts that touch without ``stitchMesh``, would stop
    every track that reaches them, so the mesh is refused. Merge baffles back
    with ``mergeOrSplitBaffles`` and join touching parts with ``stitchMesh``.
  - a single element type per volume, and a single face type per surface

  - two patch types have no correct reading and are rejected:

    ``cyclic`` and its variants
        Periodic boundaries, which XDG has no equivalent for. The geometry would
        be treated as ending at the patch rather than continuing through it.

    ``processor`` and its variants
        Partition boundaries in a decomposed case. Rebuild the single-domain
        mesh with ``reconstructParMesh`` first.
