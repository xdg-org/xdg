.. _moab_format:


MOAB (.h5m) File Format
=======================

The file format for :term:`MOAB` meshes comes from the file specification for
:term:`DAGMC` meshes. These files are in the native `.h5m` format, which is an
HDF5 file containing the mesh itself along with associated metadata. Because XDG supports
volumetric elements, it extends the original DAGMC file specification to allow for
volumetric elements to be present in the file.

Instructions for building :term:`DAGMC` models for :term:`XDG` using Cubit can be found here: `OpenMC preparation instructions`_.

.. _OpenMC preparation instructions: https://svalinn.github.io/DAGMC/usersguide/codes/openmc.html


The `.h5m` file is a generic file format supported by :term:`MOAB`. These files
may contain meshes intended for any number of purposes with various element
types. Some basics regarding :term:`MOAB` constructs is required to navigate
following specification, namely :term:`EntitySet`'s' and :term:`Tag`'s. See the
:ref:`glossary` for more information on these items.

Geometric EntitySets¶
---------------------

For an `.h5m` file to be used with :term:`DAGMC`, :term:`EntitySet`'s that are
"tagged" with specific information must be present. Only :term:`EntitySet`'s that are
tagged with the required information will be "seen" by the :term:`DAGMC`
interface. These tags are used to identify the geometric entities (volumes,
surfaces, curves, and vertices) as well as their relationships to each other.
Tags on geometric :term:`EntitySet`'s are also used to establish topological
relationships. For example, the `GEOM_SENSE_2` tag (described in the
`geom_tags`_ table) is used to relate surfaces to the volumes on either side
of the surface.The `GEOM_DIM` tag is used to indicate the dimensionality of the
geometric `EntitySet`. See the `geom_dim_table`_ for valid entries for this tag.

Metdata EntitySets
------------------

To apply a :term:`DAGMC` geometry in transport, certain properties need to be
associated with the geometry. Examples of these properties include:

  - Material assignments
  - Boundary conditions
  - Temperatures
  - Tallies

Please refer to the transport code documentation for any properties that may be specific
to the transport code you intended to use.

.. _geom_tags:

Geometry EntitySet Tags
---------------------

.. table:: Geometric EntitySet Tag Descriptions

+-----------------------+------------------+------------+------+-------------+--------------------------------------------------------------------------------------------------------------+
| Tag Name              | Type             | Real Type  | Size | Tagged On   | Purpose                                                                                                      |
+=======================+==================+============+======+=============+==============================================================================================================+
| `GLOBAL_ID`           | `MB_TYPE_INT`    | `int`      | 1    | `EntitySet` | Value of an ID associated with a geometric `EntitySet`.                                                      |
+-----------------------+------------------+------------+------+-------------+--------------------------------------------------------------------------------------------------------------+
| `GEOM_SENSE_2`        | `EntityHandle`   | `uint64_t` | 2    | `EntitySet` | Relates a surface to the two volumes on either side of the surface. An entry in the first position           |
|                       |                  |            |      |             | indicates that the surface has a sense that is forward with respect to                                       |
|                       |                  |            |      |             | the volume `EntityHandle` in that position. An entry in the second position                                  |
|                       |                  |            |      |             | indicates that the surface has a sense reversed with respect to the volume `EntityHandle` in that position.  |
|                       |                  |            |      |             | Only relevant for `EntitySet`s that represent a surface.                                                     |
+-----------------------+------------------+------------+------+-------------+--------------------------------------------------------------------------------------------------------------+
| `GEOM_SENSE_N_ENTS`   | `EntityHandle`   | `uint64_t` | N    | `EntitySet` | Relates a curve to any topologically adjacent surface `EntitySet`s.                                          |
+-----------------------+------------------+------------+------+-------------+--------------------------------------------------------------------------------------------------------------+
| `GEOM_SENSE_N_SENSES` | `MB_TYPE_INT`    | `int`      | N    | `EntitySet` | Curve sense data correllated with the `GEOM_SENSE_N_ENTS` information.                                       |
|                       |                  |            |      |             | Values are `1` for a forward senses and `-1` for reversed senses.                                            |
+-----------------------+------------------+------------+------+-------------+--------------------------------------------------------------------------------------------------------------+
| `CATEGORY`            | `MB_TYPE_OPAQUE` | `char`     | 32   | `EntitySet` | The geometric category of an `EntitySet`. One of "Vertex", "Curve", "Surface", "Volume", or "Group"          |
+-----------------------+------------------+------------+------+-------------+--------------------------------------------------------------------------------------------------------------+
| `GEOM_DIM`            | `MB_TYPE_INT`    | `int`      | 1    | `EntitySet` | The dimensionality of a geometric `EntitySet`. See table below for meaning of values.                        |
+-----------------------+------------------+------------+------+-------------+--------------------------------------------------------------------------------------------------------------+
| `NAME`                | `MB_TYPE_OPAQUE` | `char`     | 32   | `EntitySet` | A name assigned to an `EntitySet`. Use to indicate material assignments,                                     |
|                       |                  |            |      |             | boundary conditions, temperatures, and the implicit complement on                                            |
|                       |                  |            |      |             | `EntitySet`'s with a `CATEGORY` tag whose value is "Group"                                                   |
+-----------------------+------------------+------------+------+-------------+--------------------------------------------------------------------------------------------------------------+

.. _geom_dim_table:

Dimensionality Values of the `GEOM_DIM` Tag
--------------------------------------

.. table:: Dimensionality Values of the `GEOM_DIM` Tag

+-----------------+----------------------+
| Geometry Object | Dimensionality [*]_  |
+=================+======================+
| Vertex          | 0                    |
+-----------------+----------------------+
| Curve           | 1                    |
+-----------------+----------------------+
| Surface         | 2                    |
+-----------------+----------------------+
| Volume          | 3                    |
+-----------------+----------------------+

.. [*] The value of the `GEOM_DIM` tag on the geometric `EntitySet`.


Topology¶
~~~~~~~~~~

Every mesh-based geometry contains :term:`EntitySet`'s that are either
volumes or surfaces. There are two types of relationships that can
relate entities to other entities. The first is called a parent-child
relationship. Volumes are parents to surfaces that make up that volume; surfaces
are parents to curves; and curves are parents to the geometric vertices.

The second type of relationship is the set relationship, which is different from
a parent-child relationship. Each surface and curve is an :term:`EntitySet`. The
surface :term:`EntitySet`'s contain the triangles and their vertices for that surface.
The curve :term:`EntitySet`'s contain edges and their vertices.

For MOAB files, transport meshes do not require volumetric elements in the case
that the user intends to use surface tracking. In this case, the volume
:term:`EntitySet`'s are present, but do not contain any mesh entities. The surface
:term:`EntitySet`'s contain the triangles and their vertices for that surface. The
curve :term:`EntitySet`'s contain edges and their vertices.

Extension for Volumetric Elements¶
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If volumetric elements are present, they are contained in the volume
:term:`EntitySet`'s and, if specified, transport can be performed by walking the
element of the volume. Currently, it is expected that if volumetric elements are
present for one volume, they are present for all volumes. It is also expected
that the boundary triangles of the volumetric elements correspond to the
triangles of the child surface :term:`EntitySet`'s of the volume.

*Note: Curves and vertices are not required for transport, but may be present
in the mesh file depending on it's point of origin.*

Sense tags¶
~~~~~~~~~~~~

The parent volumes of each surface are specified using the `GEOM_SENSE_2` tag on
the surface :term:`EntitySet`. The first entry in the tag corresponds to the volume
for which the surface has a forward sense, meaning that the normals of the
triangles contained in the surface :term:`EntitySet` point outward with respect to
that volume. The second entry corresponds to the volume for which the surface has a
reverse sense, meaning that the normals of the triangles point inward with
respect to that volume.  If a a surface is at the boudnary of the problem, one of the
entries in the `GEOM_SENSE_2` tag will be `0`, indicating that there is no
volume on that side of the surface. Durint initialization, XDG will create a
special "outside" volume to represent the space outside of the geometry, named the
:term:`implicit complement`.