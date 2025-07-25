// Copyright (C) 2019-2025 Jorgen S. Dokken and Garth N. Wells
//
// This file is part of DOLFINx (https://www.fenicsproject.org)
//
// SPDX-License-Identifier:    LGPL-3.0-or-later

#include "cells.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <dolfinx/common/log.h>
#include <dolfinx/mesh/cell_types.h>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

using namespace dolfinx;

namespace
{
//----------------------------------------------------------------------------
std::uint16_t vec_pop(std::vector<std::uint16_t>& v, int i)
{
  auto pos = (i < 0) ? v.end() + i : v.begin() + i;
  std::uint16_t value = *pos;
  v.erase(pos);
  return value;
}
//----------------------------------------------------------------------------
std::vector<std::uint16_t>
vtk_triangle_remainders(std::vector<std::uint16_t> remainders)
{
  std::vector<std::uint16_t> map;
  map.reserve(remainders.size());

  while (!remainders.empty())
  {
    if (remainders.size() == 1)
    {
      map.push_back(vec_pop(remainders, 0));
      break;
    }

    int degree
        = io::cells::cell_degree(mesh::CellType::triangle, remainders.size());

    map.push_back(vec_pop(remainders, 0));
    map.push_back(vec_pop(remainders, degree - 1));
    map.push_back(vec_pop(remainders, -1));

    for (int i = 0; i < degree - 1; ++i)
      map.push_back(vec_pop(remainders, 0));

    for (int i = 1, k = degree * (degree - 1) / 2; i < degree;
         k -= degree - i, ++i)
    {
      map.push_back(vec_pop(remainders, -k));
    }

    for (int i = 1, k = 1; i < degree; k += i, ++i)
      map.push_back(vec_pop(remainders, -k));
  }

  return map;
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> vtk_triangle(int num_nodes)
{
  std::vector<std::uint16_t> map;
  map.reserve(num_nodes);

  // Vertices
  map.insert(map.begin(), {0, 1, 2});

  int degree = io::cells::cell_degree(mesh::CellType::triangle, num_nodes);
  for (int k = 1; k < degree; ++k)
    map.push_back(3 + 2 * (degree - 1) + k - 1);
  for (int k = 1; k < degree; ++k)
    map.push_back(3 + k - 1);
  for (int k = 1; k < degree; ++k)
    map.push_back(2 * degree - (k - 1));

  if (degree < 3)
    return map;

  // Interior VTK is ordered as a lower-order triangle, while DOLFINx
  // orders them lexicographically
  std::vector<std::uint16_t> rem(num_nodes - map.size());
  std::iota(rem.begin(), rem.end(), 3 * degree);
  std::ranges::copy(vtk_triangle_remainders(rem), std::back_inserter(map));

  return map;
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t>
vtk_tetrahedron_remainders(std::vector<std::uint16_t> remainders)
{
  std::vector<std::uint16_t> map;
  map.reserve(remainders.size());

  while (!remainders.empty())
  {
    if (remainders.size() == 1)
    {
      map.push_back(vec_pop(remainders, 0));
      break;
    }

    const int deg
        = io::cells::cell_degree(mesh::CellType::tetrahedron, remainders.size())
          + 1;
    map.push_back(vec_pop(remainders, 0));
    map.push_back(vec_pop(remainders, deg - 2));
    map.push_back(vec_pop(remainders, deg * (deg + 1) / 2 - 3));
    map.push_back(vec_pop(remainders, -1));

    if (deg > 2)
    {
      for (int i = 0; i < deg - 2; ++i)
        map.push_back(vec_pop(remainders, 0));

      {
        int d = deg - 2;
        for (int i = 0; i < deg - 2; ++i)
        {
          map.push_back(vec_pop(remainders, d));
          d += (deg - 3 - i);
        }
      }

      {
        int d = (deg - 2) * (deg - 1) / 2 - 1;
        for (int i = 0; i < deg - 2; ++i)
        {
          map.push_back(vec_pop(remainders, d));
          d -= (2 + i);
        }
      }

      {
        int d = (deg - 3) * (deg - 2) / 2;
        for (int i = 0; i < deg - 2; ++i)
        {
          map.push_back(vec_pop(remainders, d));
          d += ((deg - i) * (deg - i - 1) / 2 - 1);
        }
      }

      {
        int d = (deg - 3) * (deg - 2) / 2 + deg - 3;
        for (int i = 0; i < deg - 2; ++i)
        {
          map.push_back(vec_pop(remainders, d));
          d += ((deg - 2 - i) * (deg - 1 - i) / 2 + deg - 4 - i);
        }
      }

      {
        int d = (deg - 3) * (deg - 2) / 2 + deg - 3 + (deg - 2) * (deg - 1) / 2
                - 1;
        for (int i = 0; i < deg - 2; ++i)
        {
          map.push_back(vec_pop(remainders, d));
          d += ((deg - 3 - i) * (deg - 2 - i) / 2 + deg - i - 5);
        }
      }
    }

    if (deg > 3)
    {
      {
        std::vector<std::uint16_t> dofs;
        int d = (deg - 3) * (deg - 2) / 2;
        for (int i = 0; i < deg - 3; ++i)
        {
          for (int ii = 0; ii < deg - 3 - i; ++ii)
            dofs.push_back(vec_pop(remainders, d));
          d += ((deg - 2 - i) * (deg - 1 - i) / 2 - 1);
        }

        std::ranges::copy(vtk_triangle_remainders(dofs),
                          std::back_inserter(map));
      }

      {
        std::vector<std::uint16_t> dofs;
        int start = deg * deg - 4 * deg + 2;
        int sub_i_start = deg - 3;
        for (int i = 0; i < deg - 3; ++i)
        {
          int d = start;
          int sub_i = sub_i_start;
          for (int ii = 0; ii < deg - 3 - i; ++ii)
          {
            dofs.push_back(vec_pop(remainders, d));
            d += sub_i * (sub_i + 1) / 2 - 2 - i;
            sub_i -= 1;
          }

          start -= (2 + i);
        }

        std::ranges::copy(vtk_triangle_remainders(dofs),
                          std::back_inserter(map));
      }

      {
        std::vector<std::uint16_t> dofs;
        int start = (deg - 3) * (deg - 2) / 2;
        int sub_i_start = deg - 3;
        for (int i = 0; i < deg - 3; ++i)
        {
          int d = start;
          int sub_i = sub_i_start;
          for (int ii = 0; ii < deg - 3 - i; ++ii)
          {
            dofs.push_back(vec_pop(remainders, d));
            d += sub_i * (sub_i + 1) / 2 - 1 - 2 * i;
            sub_i -= 1;
          }

          start += (deg - 4 - i);
        }

        std::ranges::copy(vtk_triangle_remainders(dofs),
                          std::back_inserter(map));
      }

      {
        std::vector<std::uint16_t> dofs;
        int add_start = deg - 4;
        for (int i = 0; i < deg - 3; ++i)
        {
          int d = 0;
          int add = add_start;
          for (int ii = 0; ii < deg - 3 - i; ++ii)
          {
            dofs.push_back(vec_pop(remainders, d));
            d += add;
            add -= 1;
          }

          add_start -= 1;
        }

        std::ranges::copy(vtk_triangle_remainders(dofs),
                          std::back_inserter(map));
      }
    }
  }

  return map;
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> vtk_tetrahedron(int num_nodes)
{
  const int degree
      = io::cells::cell_degree(mesh::CellType::tetrahedron, num_nodes);

  std::vector<std::uint16_t> map;
  map.reserve(num_nodes);

  // Vertices
  map.insert(map.begin(), {0, 1, 2, 3});

  if (degree < 2)
    return map;

  int base = 4;
  const int edge_dofs = degree - 1;
  for (int edge : {5, 2, 4, 3, 1, 0})
  {
    if (edge == 4)
    {
      for (int i = 0; i < edge_dofs; ++i)
        map.push_back(base + edge_dofs * (edge + 1) - 1 - i);
    }
    else
    {
      for (int i = 0; i < edge_dofs; ++i)
        map.push_back(base + edge_dofs * edge + i);
    }
  }

  if (degree < 3)
    return map;

  base += 6 * edge_dofs;
  const int n_face_dofs = (degree - 1) * (degree - 2) / 2;
  for (int face : {2, 0, 1, 3})
  {
    std::vector<std::uint16_t> face_dofs;
    face_dofs.reserve(n_face_dofs);
    if (face == 2)
    {
      for (int i = 0; i < n_face_dofs; ++i)
        face_dofs.push_back(base + n_face_dofs * face + i);
    }
    else if (face == 0)
    {
      for (int i = degree - 3; i >= 0; --i)
      {
        int d = i;
        for (int ii = 0; ii <= i; ++ii)
        {
          face_dofs.push_back(base + n_face_dofs * face + d);
          d += degree - 3 - ii;
        }
      }
    }
    else
    {
      for (int i = 0; i < degree - 2; ++i)
      {
        int d = i;
        for (int ii = 0; ii < degree - 2 - i; ++ii)
        {
          face_dofs.push_back(base + n_face_dofs * face + d);
          d += degree - 2 - ii;
        }
      }
    }

    std::ranges::copy(vtk_triangle_remainders(face_dofs),
                      std::back_inserter(map));
  }

  if (degree < 4)
    return map;

  base += 4 * n_face_dofs;
  std::vector<std::uint16_t> remainders((degree - 1) * (degree - 2)
                                        * (degree - 3) / 6);
  std::iota(remainders.begin(), remainders.end(), base);
  std::ranges::copy(vtk_tetrahedron_remainders(remainders),
                    std::back_inserter(map));

  return map;
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> vtk_wedge(int num_nodes)
{
  switch (num_nodes)
  {
  case 6:
    return {0, 1, 2, 3, 4, 5};
  case 15:
    return {0, 1, 2, 3, 4, 5, 6, 9, 7, 12, 14, 13, 8, 10, 11};
  default:
    throw std::runtime_error("Unknown wedge layout");
  }
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> vtk_pyramid(int num_nodes)
{
  switch (num_nodes)
  {
  case 5:
    return {0, 1, 3, 2, 4};
  case 13:
    return {0, 1, 3, 2, 4, 5, 8, 10, 6, 7, 9, 12, 11};
  default:
    throw std::runtime_error("Unknown pyramid layout");
  }
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> vtk_quadrilateral(int num_nodes)
{
  std::vector<std::uint16_t> map;
  map.reserve(num_nodes);

  // Vertices
  map.insert(map.begin(), {0, 1, 3, 2});

  const int n
      = io::cells::cell_degree(mesh::CellType::quadrilateral, num_nodes);

  // Edges
  const int edge_nodes = n - 1;
  for (int k = 0; k < edge_nodes; ++k)
    map.push_back(4 + k);
  for (int k = 0; k < edge_nodes; ++k)
    map.push_back(4 + 2 * edge_nodes + k);
  for (int k = 0; k < edge_nodes; ++k)
    map.push_back(4 + 3 * edge_nodes + k);
  for (int k = 0; k < edge_nodes; ++k)
    map.push_back(4 + edge_nodes + k);

  // Face
  for (int k = 0; k < edge_nodes * edge_nodes; ++k)
    map.push_back(4 + edge_nodes * 4 + k);

  return map;
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> vtk_hexahedron(int num_nodes)
{
  if (num_nodes == 20)
  {
    std::vector<std::uint16_t> map;
    map.insert(map.begin(), {0, 1, 3, 2, 4, 5, 7, 6});
    for (int e : {0, 3, 5, 1, 8, 10, 11, 9, 2, 4, 7, 6})
      map.push_back(8 + e);
    return map;
  }

  std::uint16_t n
      = io::cells::cell_degree(mesh::CellType::hexahedron, num_nodes);

  std::vector<std::uint16_t> map;
  map.reserve(num_nodes);

  // Vertices
  map.insert(map.begin(), {0, 1, 3, 2, 4, 5, 7, 6});

  // Edges
  int base = 8;
  const int edge_nodes = n - 1;
  for (int e : {0, 3, 5, 1, 8, 10, 11, 9, 2, 4, 7, 6})
    for (int i = 0; i < edge_nodes; ++i)
      map.push_back(base + edge_nodes * e + i);

  base += 12 * edge_nodes;
  const int face_nodes = edge_nodes * edge_nodes;
  for (int f : {2, 3, 1, 4, 0, 5})
    for (int i = 0; i < face_nodes; ++i)
      map.push_back(base + face_nodes * f + i);

  // Volume
  base += 6 * face_nodes;
  for (int i = 0; i < face_nodes * edge_nodes; ++i)
    map.push_back(base + i);

  return map;
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> gmsh_triangle(int num_nodes)
{
  switch (num_nodes)
  {
  case 3:
    return {0, 1, 2};
  case 6:
    return {0, 1, 2, 5, 3, 4};
  case 10:
    return {0, 1, 2, 7, 8, 3, 4, 6, 5, 9};
  default:
    throw std::runtime_error("Higher order Gmsh triangle not supported");
  }
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> gmsh_tetrahedron(int num_nodes)
{
  switch (num_nodes)
  {
  case 4:
    return {0, 1, 2, 3};
  case 10:
    return {0, 1, 2, 3, 9, 6, 8, 7, 4, 5};
  case 20:
    return {0,  1,  2, 3, 14, 15, 8,  9,  13, 12,
            11, 10, 5, 4, 7,  6,  19, 18, 17, 16};
  default:
    throw std::runtime_error("Higher order Gmsh tetrahedron not supported");
  }
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> gmsh_hexahedron(int num_nodes)
{
  switch (num_nodes)
  {
  case 8:
    return {0, 1, 3, 2, 4, 5, 7, 6};
  case 27:
    return {0,  1,  3,  2,  4,  5,  7,  6,  8,  9,  10, 11, 12, 13,
            15, 14, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};
  default:
    throw std::runtime_error("Higher order Gmsh hexahedron not supported");
  }
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> gmsh_quadrilateral(int num_nodes)
{
  switch (num_nodes)
  {
  case 4:
    return {0, 1, 3, 2};
  case 9:
    return {0, 1, 3, 2, 4, 6, 7, 5, 8};
  case 16:
    return {0, 1, 3, 2, 4, 5, 8, 9, 11, 10, 7, 6, 12, 13, 15, 14};
  default:
    throw std::runtime_error("Higher order Gmsh quadrilateral not supported");
  }
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> gmsh_prism(int num_nodes)
{
  switch (num_nodes)
  {
  case 6:
    return {0, 1, 2, 3, 4, 5};
  case 15:
    return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
  default:
    throw std::runtime_error("Higher order Gmsh prism not supported");
  }
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> gmsh_pyramid(int num_nodes)
{
  switch (num_nodes)
  {
  case 5:
    return {0, 1, 3, 2, 4};
  case 13:
    return {0, 1, 3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 11};
  default:
    throw std::runtime_error("Higher order Gmsh pyramid not supported");
  }
}
} // namespace
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> io::cells::perm_vtk(mesh::CellType type,
                                               int num_nodes)
{
  std::vector<std::uint16_t> map;
  switch (type)
  {
  case mesh::CellType::point:
    map = {0};
    break;
  case mesh::CellType::interval:
    map.resize(num_nodes);
    std::iota(map.begin(), map.end(), 0);
    break;
  case mesh::CellType::triangle:
    map = vtk_triangle(num_nodes);
    break;
  case mesh::CellType::tetrahedron:
    map = vtk_tetrahedron(num_nodes);
    break;
  case mesh::CellType::quadrilateral:
    map = vtk_quadrilateral(num_nodes);
    break;
  case mesh::CellType::hexahedron:
    map = vtk_hexahedron(num_nodes);
    break;
  case mesh::CellType::prism:
    map = vtk_wedge(num_nodes);
    break;
  case mesh::CellType::pyramid:
    map = vtk_pyramid(num_nodes);
    break;
  default:
    throw std::runtime_error("Unknown cell type.");
  }

  return cells::transpose(map);
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t> io::cells::perm_gmsh(mesh::CellType type,
                                                int num_nodes)
{
  std::vector<std::uint16_t> map;
  switch (type)
  {
  case mesh::CellType::point:
    map = {0};
    break;
  case mesh::CellType::interval:
    map.resize(num_nodes);
    std::iota(map.begin(), map.end(), 0);
    break;
  case mesh::CellType::triangle:
    map = gmsh_triangle(num_nodes);
    break;
  case mesh::CellType::tetrahedron:
    map = gmsh_tetrahedron(num_nodes);
    break;
  case mesh::CellType::quadrilateral:
    map = gmsh_quadrilateral(num_nodes);
    break;
  case mesh::CellType::hexahedron:
    map = gmsh_hexahedron(num_nodes);
    break;
  case mesh::CellType::prism:
    map = gmsh_prism(num_nodes);
    break;
  case mesh::CellType::pyramid:
    map = gmsh_pyramid(num_nodes);
    break;
  default:
    throw std::runtime_error("Unknown cell type.");
  }

  return cells::transpose(map);
}
//-----------------------------------------------------------------------------
int io::cells::cell_degree(mesh::CellType type, int num_nodes)
{
  switch (type)
  {
  case mesh::CellType::point:
    return 1;
  case mesh::CellType::interval:
    return num_nodes - 1;
  case mesh::CellType::triangle:
  {
    const int n = (std::sqrt(1 + 8 * num_nodes) - 1) / 2;
    if (2 * num_nodes != n * (n + 1))
    {
      throw std::runtime_error("Unknown triangle layout. Number of nodes: "
                               + std::to_string(num_nodes));
    }
    return n - 1;
  }
  case mesh::CellType::tetrahedron:
  {
    int n = 0;
    while (n * (n + 1) * (n + 2) < 6 * num_nodes)
      ++n;
    if (n * (n + 1) * (n + 2) != 6 * num_nodes)
    {
      throw std::runtime_error("Unknown tetrahedron layout. Number of nodes: "
                               + std::to_string(num_nodes));
    }
    return n - 1;
  }
  case mesh::CellType::quadrilateral:
  {
    const int n = std::sqrt(num_nodes);
    if (num_nodes != n * n)
    {
      throw std::runtime_error("Unknown quadrilateral layout. Number of nodes: "
                               + std::to_string(num_nodes));
    }
    return n - 1;
  }
  case mesh::CellType::hexahedron:
  {
    const int n = std::cbrt(num_nodes);
    if (num_nodes != n * n * n)
    {
      throw std::runtime_error("Unknown hexahedron layout. Number of nodes: "
                               + std::to_string(num_nodes));
    }
    return n - 1;
  }
  case mesh::CellType::prism:
    switch (num_nodes)
    {
    case 6:
      return 1;
    case 15:
      return 2;
    default:
      throw std::runtime_error("Unknown prism layout. Number of nodes: "
                               + std::to_string(num_nodes));
    }
  case mesh::CellType::pyramid:
    switch (num_nodes)
    {
    case 5:
      return 1;
    case 13:
      return 2;
    default:
      throw std::runtime_error("Unknown pyramid layout. Number of nodes: "
                               + std::to_string(num_nodes));
    }
  default:
    throw std::runtime_error("Unknown cell type.");
  }
}
//-----------------------------------------------------------------------------
std::vector<std::uint16_t>
io::cells::transpose(std::span<const std::uint16_t> map)
{
  std::vector<std::uint16_t> transpose(map.size());
  for (std::size_t i = 0; i < map.size(); ++i)
    transpose[map[i]] = i;
  return transpose;
}
//-----------------------------------------------------------------------------
std::vector<std::int64_t>
io::cells::apply_permutation(std::span<const std::int64_t> cells,
                             std::array<std::size_t, 2> shape,
                             std::span<const std::uint16_t> p)
{
  assert(cells.size() == shape[0] * shape[1]);
  assert(shape[1] == p.size());

  spdlog::info("IO permuting cells");
  std::vector<std::int64_t> cells_new(cells.size());
  for (std::size_t c = 0; c < shape[0]; ++c)
  {
    auto cell = cells.subspan(c * shape[1], shape[1]);
    std::span cell_new(cells_new.data() + c * shape[1], shape[1]);
    for (std::size_t i = 0; i < shape[1]; ++i)
      cell_new[i] = cell[p[i]];
  }
  return cells_new;
}
//-----------------------------------------------------------------------------
std::int8_t io::cells::get_vtk_cell_type(mesh::CellType cell, int dim)
{
  if (cell == mesh::CellType::prism and dim == 2)
    throw std::runtime_error("More work needed for prism cell");

  // Get cell type
  mesh::CellType cell_type = mesh::cell_entity_type(cell, dim, 0);

  // Determine VTK cell type (arbitrary Lagrange elements)
  // https://vtk.org/doc/nightly/html/vtkCellType_8h_source.html
  switch (cell_type)
  {
  case mesh::CellType::point:
    return 1;
  case mesh::CellType::interval:
    return 68;
  case mesh::CellType::triangle:
    return 69;
  case mesh::CellType::quadrilateral:
    return 70;
  case mesh::CellType::tetrahedron:
    return 71;
  case mesh::CellType::hexahedron:
    return 72;
  case mesh::CellType::pyramid:
    return 14;
  case mesh::CellType::prism:
    return 73;
  default:
    throw std::runtime_error("Unknown cell type");
  }
}
//----------------------------------------------------------------------------
