// Copyright (C) 2018-2025 Garth N. Wells
//
// This file is part of DOLFINx (https://www.fenicsproject.org)
//
// SPDX-License-Identifier:    LGPL-3.0-or-later

#pragma once

#include "Function.h"
#include "FunctionSpace.h"
#include "assemble_expression_impl.h"
#include "assemble_matrix_impl.h"
#include "assemble_scalar_impl.h"
#include "assemble_vector_impl.h"
#include "pack.h"
#include "traits.h"
#include "utils.h"
#include <algorithm>
#include <basix/mdspan.hpp>
#include <cstdint>
#include <dolfinx/common/types.h>
#include <memory>
#include <optional>
#include <span>
#include <vector>

/// @file assembler.h
/// @brief Functions supporting assembly of finite element fem::Form and
/// fem::Expression.

namespace dolfinx::fem
{
template <dolfinx::scalar T, std::floating_point U>
class DirichletBC;
template <dolfinx::scalar T, std::floating_point U>
class Expression;
template <dolfinx::scalar T, std::floating_point U>
class Form;
template <std::floating_point T>
class FunctionSpace;

/// @brief Evaluate an Expression on cells or facets.
///
/// This function accepts packed coefficient data, which allows it be
/// called without re-packing all coefficient data at each evaluation.
///
/// @tparam T Scalar type.
/// @tparam U Geometry type
/// @param[in,out] values Array to fill with computed values. Shape is
/// `(num_entities, num_points, value_size, num_argument_dofs)` and
/// storage is row-major.
/// @param[in] e Expression to evaluate.
/// @param[in] coeffs Packed coefficients for the Expressions. Typically
/// computed using fem::pack_coefficients.
/// @param[in] constants Packed constant data. Typically computed using
/// fem::pack_constants.
/// @param[in] entities Mesh entities to evaluate the expression over.
/// For cells it is a list of cell indices. For facets is is a list of
/// (cell index, local facet index) index pairs, i.e. `entities=[cell0,
/// facet_local0, cell1, facet_local1, ...]`.
/// @param[in] mesh Mesh that the Expression is evaluated on.
/// @param[in] element Argument element and argument space dimension.
template <dolfinx::scalar T, std::floating_point U>
void tabulate_expression(
    std::span<T> values, const fem::Expression<T, U>& e,
    md::mdspan<const T, md::dextents<std::size_t, 2>> coeffs,
    std::span<const T> constants, const mesh::Mesh<U>& mesh,
    fem::MDSpan2 auto entities,
    std::optional<
        std::pair<std::reference_wrapper<const FiniteElement<U>>, std::size_t>>
        element)
{
  auto [X, Xshape] = e.X();
  impl::tabulate_expression(values, e.kernel(), Xshape, e.value_size(), coeffs,
                            constants, mesh, entities, element);
}

/// @brief Evaluate an Expression on cells or facets.
///
/// @tparam T Scalar type.
/// @tparam U Geometry type
/// @param[in,out] values Array to fill with computed values. Row major
/// storage. Sizing should be `(num_cells, num_points * value_size *
/// num_all_argument_dofs columns)`. facet index) tuples. Array is
/// flattened per entity.
/// @param[in] e Expression to evaluate.
/// @param[in] mesh Mesh to compute `e` on.
/// @param[in] entities Mesh entities to evaluate the expression over.
/// For expressions executed on cells, rank is 1 and size is the number
/// of cells. For expressions executed on facets rank is 2, and shape is
/// `(num_facets, 2)`, where `entities[i, 0]` is the cell index and
/// `entities[i, 1]` is the local index of the facet relative to the
/// cell.
template <dolfinx::scalar T, std::floating_point U>
void tabulate_expression(std::span<T> values, const fem::Expression<T, U>& e,
                         const mesh::Mesh<U>& mesh, fem::MDSpan2 auto entities)
{
  std::optional<
      std::pair<std::reference_wrapper<const FiniteElement<U>>, std::size_t>>
      element = std::nullopt;
  if (auto V = e.argument_space(); V)
  {
    std::size_t num_argument_dofs
        = V->dofmap()->element_dof_layout().num_dofs() * V->dofmap()->bs();
    assert(V->element());
    element = {std::cref(*V->element()), num_argument_dofs};
  }

  std::vector<int> coffsets = e.coefficient_offsets();
  const std::vector<std::shared_ptr<const Function<T, U>>>& coefficients
      = e.coefficients();
  std::vector<T> coeffs(entities.extent(0) * coffsets.back());
  int cstride = coffsets.back();
  {
    std::vector<std::reference_wrapper<const Function<T, U>>> c;
    std::ranges::transform(coefficients, std::back_inserter(c),
                           [](auto c) -> const Function<T, U>& { return *c; });
    fem::pack_coefficients(c, coffsets, entities, std::span(coeffs));
  }
  std::vector<T> constants = fem::pack_constants(e);

  tabulate_expression<T, U>(
      values, e, md::mdspan(coeffs.data(), entities.size(), cstride),
      std::span<const T>(constants), mesh, entities, element);
}

// -- Helper functions -----------------------------------------------------

/// @brief Create a map of `std::span`s from a map of `std::vector`s
template <dolfinx::scalar T>
std::map<std::pair<IntegralType, int>, std::pair<std::span<const T>, int>>
make_coefficients_span(const std::map<std::pair<IntegralType, int>,
                                      std::pair<std::vector<T>, int>>& coeffs)
{
  using Key = typename std::remove_reference_t<decltype(coeffs)>::key_type;
  std::map<Key, std::pair<std::span<const T>, int>> c;
  std::ranges::transform(
      coeffs, std::inserter(c, c.end()),
      [](auto& e) -> typename decltype(c)::value_type
      { return {e.first, {e.second.first, e.second.second}}; });
  return c;
}

// -- Scalar ----------------------------------------------------------------

/// @brief Assemble functional into scalar.
///
/// The caller supplies the form constants and coefficients for this
/// version, which has efficiency benefits if the data can be re-used
/// for multiple calls.
/// @note Caller is responsible for accumulation across processes.
/// @param[in] M The form (functional) to assemble
/// @param[in] constants The constants that appear in `M`
/// @param[in] coefficients The coefficients that appear in `M`
/// @return The contribution to the form (functional) from the local
/// process
template <dolfinx::scalar T, std::floating_point U>
T assemble_scalar(
    const Form<T, U>& M, std::span<const T> constants,
    const std::map<std::pair<IntegralType, int>,
                   std::pair<std::span<const T>, int>>& coefficients)
{
  using mdspanx3_t
      = md::mdspan<const scalar_value_t<T>,
                   md::extents<std::size_t, md::dynamic_extent, 3>>;

  std::shared_ptr<const mesh::Mesh<U>> mesh = M.mesh();
  assert(mesh);
  std::span x = mesh->geometry().x();
  if constexpr (std::is_same_v<U, scalar_value_t<T>>)
  {
    return impl::assemble_scalar(M, mesh->geometry().dofmap(),
                                 mdspanx3_t(x.data(), x.size() / 3, 3),
                                 constants, coefficients);
  }
  else
  {
    std::vector<scalar_value_t<T>> _x(x.begin(), x.end());
    return impl::assemble_scalar(M, mesh->geometry().dofmap(),
                                 mdspanx3_t(_x.data(), _x.size() / 3, 3),
                                 constants, coefficients);
  }
}

/// @brief Assemble functional into scalar.
///
/// @note Caller is responsible for accumulation across processes.
///
/// @param[in] M The form (functional) to assemble.
/// @return The contribution to the form (functional) from the local
/// process.
template <dolfinx::scalar T, std::floating_point U>
T assemble_scalar(const Form<T, U>& M)
{
  const std::vector<T> constants = pack_constants(M);
  auto coefficients = allocate_coefficient_storage(M);
  pack_coefficients(M, coefficients);
  return assemble_scalar(M, std::span(constants),
                         make_coefficients_span(coefficients));
}

// -- Vectors ----------------------------------------------------------------

/// @brief Assemble linear form into a vector.
///
/// The caller supplies the form constants and coefficients for this
/// version, which has efficiency benefits if the data can be re-used
/// for multiple calls.
/// @param[in,out] b The vector to be assembled. It will not be zeroed
/// before assembly.
/// @param[in] L The linear forms to assemble into b.
/// @param[in] constants The constants that appear in `L`.
/// @param[in] coefficients The coefficients that appear in `L`.
template <dolfinx::scalar T, std::floating_point U>
void assemble_vector(
    std::span<T> b, const Form<T, U>& L, std::span<const T> constants,
    const std::map<std::pair<IntegralType, int>,
                   std::pair<std::span<const T>, int>>& coefficients)
{
  impl::assemble_vector(b, L, constants, coefficients);
}

/// @brief Assemble linear form into a vector.
/// @param[in,out] b Vector to be assembled. It will not be zeroed
/// before assembly.
/// @param[in] L Linear forms to assemble into b.
template <dolfinx::scalar T, std::floating_point U>
void assemble_vector(std::span<T> b, const Form<T, U>& L)
{
  auto coefficients = allocate_coefficient_storage(L);
  pack_coefficients(L, coefficients);
  const std::vector<T> constants = pack_constants(L);
  assemble_vector(b, L, std::span(constants),
                  make_coefficients_span(coefficients));
}

/// @brief Modify the right-hand side vector to account for constraints
/// (Dirichlet boundary condition constraints). This modification is
/// known as 'lifting'.
///
/// Consider the discrete algebraic system
/// \f[
/// \begin{bmatrix}
/// A_{0} & A_{1}
/// \end{bmatrix}
/// \begin{bmatrix}
/// u_{0} \\ u_{1}
/// \end{bmatrix}
/// = b,
/// \f]
/// where \f$A_{i}\f$ is a matrix. Partitioning each vector \f$u_{i}\f$
/// into 'unknown' (\f$u_{i}^{(0)}\f$) and prescribed
/// (\f$u_{i}^{(1)}\f$) groups,
/// \f[
/// \begin{bmatrix}
/// A_{0}^{(0)} & A_{0}^{(1)} & A_{1}^{(0)} & A_{1}^{(1)}
/// \end{bmatrix}
/// \begin{bmatrix}
/// u_{0}^{(0)} \\ u_{0}^{(1)} \\ u_{1}^{(0)} \\ u_{1}^{(1)}
/// \end{bmatrix}
/// = b.
/// \f]
/// If \f$u_{i}^{(1)} = \alpha(g_{i} - x_{i})\f$, where \f$g_{i}\f$ is
/// the Dirichlet boundary condition value, \f$x_{i}\f$ is provided and
/// \f$\alpha\f$ is a constant, then
/// \f[
/// \begin{bmatrix}
/// A_{0}^{(0)} & A_{0}^{(1)} & A_{1}^{(0)} & A_{1}^{(1)}
/// \end{bmatrix}
/// \begin{bmatrix}
/// u_{0}^{(0)} \\ \alpha(g_{0} - x_{0}) \\ u_{1}^{(0)} \\ \alpha(g_{1} - x_{1})
/// \end{bmatrix}
/// = b.
/// \f]
/// Rearranging,
/// \f[
/// \begin{bmatrix}
/// A_{0}^{(0)} & A_{1}^{(0)}
/// \end{bmatrix}
/// \begin{bmatrix}
/// u_{0}^{(0)} \\ u_{1}^{(0)}
/// \end{bmatrix}
/// = b - \alpha A_{0}^{(1)} (g_{0} - x_{0}) - \alpha A_{1}^{(1)} (g_{1} -
/// x_{1}).
/// \f]
///
/// The modified \f$b\f$ vector is
/// \f[
///  b \leftarrow b - \alpha A_{0}^{(1)} (g_{0} - x_{0}) - \alpha A_{1}^{(1)}
///  (g_{1} - x_{1})
/// \f]
/// More generally,
/// \f[
///  b \leftarrow b - \alpha A_{i}^{(1)} (g_{i} - x_{i}).
/// \f]
///
/// @note Ghost contributions are not accumulated (not sent to owner).
/// Caller is responsible for reverse-scatter to update the ghosts.
///
/// @note Boundary condition values are *not* set in `b` by this
/// function. Use DirichletBC::set to set values in `b`.
///
/// @param[in,out] b The vector to modify inplace.
/// @param[in] a List of bilinear forms, where `a[i]` is the form that
/// generates the matrix \f$A_{i}\f$. All forms in `a` must share the
/// same test function space. The trial function spaces can differ.
/// @param[in] constants Constant data appearing in the forms `a`.
/// @param[in] coeffs Coefficient data appearing in the forms `a`.
/// @param[in] x0 The vector \f$x_{i}\f$ above. If empty it is set to
/// zero.
/// @param[in] bcs1 Boundary conditions that provide the \f$g_{i}\f$
/// values. `bcs1[i]` is the list of boundary conditions on \f$u_{i}\f$.
/// @param[in] alpha Scalar used in the modification of `b`.
template <dolfinx::scalar T, std::floating_point U>
void apply_lifting(
    std::span<T> b,
    std::vector<std::optional<std::reference_wrapper<const Form<T, U>>>> a,
    const std::vector<std::span<const T>>& constants,
    const std::vector<std::map<std::pair<IntegralType, int>,
                               std::pair<std::span<const T>, int>>>& coeffs,
    const std::vector<
        std::vector<std::reference_wrapper<const DirichletBC<T, U>>>>& bcs1,
    const std::vector<std::span<const T>>& x0, T alpha)
{
  // If all forms are null, there is nothing to do
  if (std::ranges::all_of(a, [](auto ai) { return !ai; }))
    return;

  impl::apply_lifting<T>(b, a, constants, coeffs, bcs1, x0, alpha);
}

/// @brief Modify the right-hand side vector to account for constraints
/// (Dirichlet boundary conditions constraints). This modification is
/// known as 'lifting'.
///
/// See apply_lifting() for a detailed explanation of the lifting. The
/// difference between this function and apply_lifting() is that
/// apply_lifting() requires packed form constant and coefficient data
/// to be passed to the function, whereas this function packs the
/// constant and coefficient form data and then calls apply_lifting().
///
/// @note Ghost contributions are not accumulated (not sent to owner).
/// Caller is responsible for reverse-scatter to update the ghosts.
///
/// @note Boundary condition values are *not* set in `b` by this
/// function. Use DirichletBC::set to set values in `b`.
///
/// @param[in,out] b The vector to modify inplace.
/// @param[in] a List of bilinear forms, where `a[i]` is the form that
/// generates the matrix \f$A_{i}\f$ (see apply_lifting()). All forms in
/// `a` must share the same test function space. The trial function
/// spaces can differ.
/// @param[in] x0 The vector \f$x_{i}\f$ described in apply_lifting().
/// If empty it is set to zero.
/// @param[in] bcs1 Boundary conditions that provide the \f$g_{i}\f$
/// values described in apply_lifting(). `bcs1[i]` is the list of
/// boundary conditions on \f$u_{i}\f$.
/// @param[in] alpha Scalar used in the modification of `b` (see
/// described in apply_lifting()).
template <dolfinx::scalar T, std::floating_point U>
void apply_lifting(
    std::span<T> b,
    std::vector<std::optional<std::reference_wrapper<const Form<T, U>>>> a,
    const std::vector<
        std::vector<std::reference_wrapper<const DirichletBC<T, U>>>>& bcs1,
    const std::vector<std::span<const T>>& x0, T alpha)
{
  std::vector<
      std::map<std::pair<IntegralType, int>, std::pair<std::vector<T>, int>>>
      coeffs;
  std::vector<std::vector<T>> constants;
  for (auto _a : a)
  {
    if (_a)
    {
      auto coefficients = allocate_coefficient_storage(_a->get());
      pack_coefficients(_a->get(), coefficients);
      coeffs.push_back(coefficients);
      constants.push_back(pack_constants(_a->get()));
    }
    else
    {
      coeffs.emplace_back();
      constants.emplace_back();
    }
  }

  std::vector<std::span<const T>> _constants(constants.begin(),
                                             constants.end());
  std::vector<std::map<std::pair<IntegralType, int>,
                       std::pair<std::span<const T>, int>>>
      _coeffs;
  std::ranges::transform(coeffs, std::back_inserter(_coeffs),
                         [](auto& c) { return make_coefficients_span(c); });

  apply_lifting(b, a, _constants, _coeffs, bcs1, x0, alpha);
}

// -- Matrices ---------------------------------------------------------------

/// @brief Assemble bilinear form into a matrix. Matrix must already be
/// initialised. Does not zero or finalise the matrix.
/// @param[in] mat_add The function for adding values into the matrix.
/// @param[in] a The bilinear form to assemble.
/// @param[in] constants Constants that appear in `a`.
/// @param[in] coefficients Coefficients that appear in `a`.
/// @param[in] dof_marker0 Boundary condition markers for the rows. If
/// bc[i] is true then rows i in A will be zeroed. The index i is a
/// local index.
/// @param[in] dof_marker1 Boundary condition markers for the columns.
/// If bc[i] is true then rows i in A will be zeroed. The index i is a
/// local index.
template <dolfinx::scalar T, std::floating_point U>
void assemble_matrix(
    la::MatSet<T> auto mat_add, const Form<T, U>& a,
    std::span<const T> constants,
    const std::map<std::pair<IntegralType, int>,
                   std::pair<std::span<const T>, int>>& coefficients,
    std::span<const std::int8_t> dof_marker0,
    std::span<const std::int8_t> dof_marker1)

{
  using mdspanx3_t
      = md::mdspan<const scalar_value_t<T>,
                   md::extents<std::size_t, md::dynamic_extent, 3>>;

  std::shared_ptr<const mesh::Mesh<U>> mesh = a.mesh();
  assert(mesh);
  std::span x = mesh->geometry().x();
  if constexpr (std::is_same_v<U, scalar_value_t<T>>)
  {
    impl::assemble_matrix(mat_add, a, mdspanx3_t(x.data(), x.size() / 3, 3),
                          constants, coefficients, dof_marker0, dof_marker1);
  }
  else
  {
    std::vector<scalar_value_t<T>> _x(x.begin(), x.end());
    impl::assemble_matrix(mat_add, a, mdspanx3_t(_x.data(), _x.size() / 3, 3),
                          constants, coefficients, dof_marker0, dof_marker1);
  }
}

/// @brief Assemble bilinear form into a matrix
/// @param[in] mat_add The function for adding values into the matrix.
/// @param[in] a The bilinear from to assemble.
/// @param[in] constants Constants that appear in `a`.
/// @param[in] coefficients Coefficients that appear in `a`.
/// @param[in] bcs Boundary conditions to apply. For boundary condition
/// dofs the row and column are zeroed. The diagonal  entry is not set.
template <dolfinx::scalar T, std::floating_point U>
void assemble_matrix(
    auto mat_add, const Form<T, U>& a, std::span<const T> constants,
    const std::map<std::pair<IntegralType, int>,
                   std::pair<std::span<const T>, int>>& coefficients,
    const std::vector<std::reference_wrapper<const DirichletBC<T, U>>>& bcs)
{
  // Index maps for dof ranges
  // NOTE: For mixed-topology meshes, there will be multiple DOF maps,
  // but the index maps are the same.
  auto map0 = a.function_spaces().at(0)->dofmaps(0)->index_map;
  auto map1 = a.function_spaces().at(1)->dofmaps(0)->index_map;
  auto bs0 = a.function_spaces().at(0)->dofmaps(0)->index_map_bs();
  auto bs1 = a.function_spaces().at(1)->dofmaps(0)->index_map_bs();

  // Build dof markers
  std::vector<std::int8_t> dof_marker0, dof_marker1;
  assert(map0);
  std::int32_t dim0 = bs0 * (map0->size_local() + map0->num_ghosts());
  assert(map1);
  std::int32_t dim1 = bs1 * (map1->size_local() + map1->num_ghosts());
  for (std::size_t k = 0; k < bcs.size(); ++k)
  {
    assert(bcs[k].get().function_space());
    if (a.function_spaces().at(0)->contains(*bcs[k].get().function_space()))
    {
      dof_marker0.resize(dim0, false);
      bcs[k].get().mark_dofs(dof_marker0);
    }

    if (a.function_spaces().at(1)->contains(*bcs[k].get().function_space()))
    {
      dof_marker1.resize(dim1, false);
      bcs[k].get().mark_dofs(dof_marker1);
    }
  }

  // Assemble
  assemble_matrix(mat_add, a, constants, coefficients, dof_marker0,
                  dof_marker1);
}

/// @brief Assemble bilinear form into a matrix.
/// @param[in] mat_add The function for adding values into the matrix.
/// @param[in] a The bilinear from to assemble.
/// @param[in] bcs Boundary conditions to apply. For boundary condition
/// dofs the row and column are zeroed. The diagonal  entry is not set.
template <dolfinx::scalar T, std::floating_point U>
void assemble_matrix(
    auto mat_add, const Form<T, U>& a,
    const std::vector<std::reference_wrapper<const DirichletBC<T, U>>>& bcs)
{
  // Prepare constants and coefficients
  const std::vector<T> constants = pack_constants(a);
  auto coefficients = allocate_coefficient_storage(a);
  pack_coefficients(a, coefficients);

  // Assemble
  assemble_matrix(mat_add, a, std::span(constants),
                  make_coefficients_span(coefficients), bcs);
}

/// @brief Assemble bilinear form into a matrix. Matrix must already be
/// initialised. Does not zero or finalise the matrix.
///
/// @param[in] mat_add The function for adding values into the matrix.
/// @param[in] a The bilinear form to assemble.
/// @param[in] dof_marker0 Boundary condition markers for the rows. If
/// `bc[i]` is `true` then rows `i` in A` `will be zeroed. The index `i`
/// is a local index.
/// @param[in] dof_marker1 Boundary condition markers for the columns.
/// If `bc[i]` is `true` then rows `i` in `A` will be zeroed. The index
/// `i` is a local index.
template <dolfinx::scalar T, std::floating_point U>
void assemble_matrix(auto mat_add, const Form<T, U>& a,
                     std::span<const std::int8_t> dof_marker0,
                     std::span<const std::int8_t> dof_marker1)

{
  // Prepare constants and coefficients
  const std::vector<T> constants = pack_constants(a);
  auto coefficients = allocate_coefficient_storage(a);
  pack_coefficients(a, coefficients);

  // Assemble
  assemble_matrix(mat_add, a, std::span(constants),
                  make_coefficients_span(coefficients), dof_marker0,
                  dof_marker1);
}

/// @brief Sets a value to the diagonal of a matrix for specified rows.
///
/// This function is typically called after assembly. The assembly
/// function zeroes Dirichlet rows and columns. For block matrices, this
/// function should normally be called only on the diagonal blocks, i.e.
/// blocks for which the test and trial spaces are the same.
///
/// @param[in] set_fn The function for setting values to a matrix.
/// @param[in] rows Row blocks, in local indices, for which to add a
/// value to the diagonal.
/// @param[in] diagonal Value to add to the diagonal for the specified
/// rows.
template <dolfinx::scalar T>
void set_diagonal(auto set_fn, std::span<const std::int32_t> rows,
                  T diagonal = 1.0)
{
  for (std::size_t i = 0; i < rows.size(); ++i)
  {
    std::span diag_span(&diagonal, 1);
    set_fn(rows.subspan(i, 1), rows.subspan(i, 1), diag_span);
  }
}

/// @brief Sets a value to the diagonal of the matrix for rows with a
/// Dirichlet boundary conditions applied.
///
/// This function is typically called after assembly. The assembly
/// function zeroes Dirichlet rows and columns. This function adds the
/// value only to rows that are locally owned, and therefore does not
/// create a need for parallel communication. For block matrices, this
/// function should normally be called only on the diagonal blocks, i.e.
/// blocks for which the test and trial spaces are the same.
/// @param[in] set_fn The function for setting values to a matrix.
/// @param[in] V The function space for the rows and columns of the
/// matrix. It is used to extract only the Dirichlet boundary conditions
/// that are define on V or subspaces of V.
/// @param[in] bcs The Dirichlet boundary conditions.
/// @param[in] diagonal Value to add to the diagonal for rows with a
/// boundary condition applied.
template <dolfinx::scalar T, std::floating_point U>
void set_diagonal(
    auto set_fn, const FunctionSpace<U>& V,
    const std::vector<std::reference_wrapper<const DirichletBC<T, U>>>& bcs,
    T diagonal = 1.0)
{
  for (auto& bc : bcs)
  {
    if (V.contains(*bc.get().function_space()))
    {
      const auto [dofs, range] = bc.get().dof_indices();
      set_diagonal(set_fn, dofs.first(range), diagonal);
    }
  }
}

} // namespace dolfinx::fem
