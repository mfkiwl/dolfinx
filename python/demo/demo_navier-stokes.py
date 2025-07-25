# ---
# jupyter:
#   jupytext:
#     text_representation:
#       extension: .py
#       format_name: light
#       format_version: '1.5'
#       jupytext_version: 1.13.6
# ---

# # Divergence conforming discontinuous Galerkin method for the Navier--Stokes equations # noqa
#
# This demo ({download}`demo_navier-stokes.py`) illustrates how to
# implement a divergence conforming discontinuous Galerkin method for
# the Navier-Stokes equations in FEniCSx. The method conserves mass
# exactly and uses upwinding. The formulation is based on a combination
# of "A fully divergence-free finite element method for
# magnetohydrodynamic equations" by Hiptmair et al., "A Note on
# Discontinuous Galerkin Divergence-free Solutions of the Navier-Stokes
# Equations" by Cockburn et al, and "On the Divergence Constraint in
# Mixed Finite Element Methods for Incompressible Flows" by John et al.
#
#
# ## Governing equations
#
# We consider the incompressible Navier-Stokes equations in a domain
# $\Omega \subset \mathbb{R}^d$, $d \in \{2, 3\}$, and time interval
# $(0, \infty)$, given by
#
# $$
# \begin{align}
# \partial_t u - \nu \Delta u + (u \cdot \nabla)u + \nabla p &= f
# \text{ in } \Omega_t, \\
# \nabla \cdot u &= 0 \text{ in } \Omega_t,
# \end{align}
# $$
#
# where $u: \Omega_t \to \mathbb{R}^d$ is the velocity field, $p:
# \Omega_t \to \mathbb{R}$ is the pressure field, $f: \Omega_t \to
# \mathbb{R}^d$ is a prescribed force, $\nu \in \mathbb{R}^+$ is the
# kinematic viscosity, and $\Omega_t := \Omega \times (0, \infty)$.
#
# The problem is supplemented with the initial condition
#
# $$
#     u(x, 0) = u_0(x) \text{ in } \Omega
# $$
#
# and boundary condition
#
# $$
#     u = u_D \text{ on } \partial \Omega \times (0, \infty),
# $$
#
# where $u_0: \Omega \to \mathbb{R}^d$ is a prescribed initial velocity
# field which satisfies the divergence free condition. The pressure
# field is only determined up to a constant, so we seek the unique
# pressure field satisfying
#
# $$
#     \int_\Omega p = 0.
# $$
#
# ## Discrete problem
#
# We begin by introducing the function spaces
#
# $$
# \begin{align}
#     V_h^g &:= \left\{v \in H(\text{div}; \Omega);
#     v|_K \in V_h(K) \; \forall K \in \mathcal{T}, v \cdot n = g \cdot n
#     \text{ on } \partial \Omega \right\} \\
#     Q_h &:= \left\{q \in L^2_0(\Omega);
#     q|_K \in Q_h(K) \; \forall K \in \mathcal{T} \right\}.
# \end{align}
# $$
#
# The local spaces $V_h(K)$ and $Q_h(K)$ should satisfy
#
# $$
#     \nabla \cdot V_h(K) \subseteq Q_h(K),
# $$
# in order for mass to be conserved exactly. Suitable choices on
# affine simplex cells include
#
# $$
#     V_h(K) := \mathbb{RT}_k(K) \text{ and }
#     Q_h(K) := \mathbb{P}_k(K),
# $$
#
# or
#
# $$
#     V_h(K) := \mathbb{BDM}_k(K) \text{ and }
#     Q_h(K) := \mathbb{P}_{k-1}(K).
# $$
#
# Let two cells $K^+$ and $K^-$ share a facet $F$. The trace of a
# piecewise smooth vector valued function $\phi$ on F taken approaching
# from inside $K^+$ (resp. $K^-$) is denoted $\phi^{+}$ (resp.
# $\phi^-$). We now introduce the average
# $\renewcommand{\avg}[1]{\left\{\!\!\left\{#1\right\}\!\!\right\}}$
#
# $$
#     \avg{\phi} = \frac{1}{2} \left(\phi^+ + \phi^-\right)
#
# $$
#
# and jump $\renewcommand{\jump}[1]{[\![ #1 ]\!]}$
#
# $$
#     \jump{\phi} = \phi^+ \otimes n^+ + \phi^- \otimes n^-,
# $$
#
# operators, where $n$ denotes the outward unit normal to $\partial K$.
# Finally, let the upwind flux of $\phi$ with respect to a vector field
# $\psi$ be defined as
#
# $$
#     \hat{\phi}^\psi :=
#     \begin{cases}
#         \lim_{\epsilon \downarrow 0} \phi(x - \epsilon \psi(x)), \;
#         x \in \partial K \setminus \Gamma^\psi, \\
#         0, \qquad \qquad \qquad \qquad x \in \partial K \cap \Gamma^\psi,
#     \end{cases}
# $$
#
# where $\Gamma^\psi = \left\{x \in \Gamma; \; \psi(x) \cdot n(x) < 0
# \right\}$.
#
# The semi-discrete version problem (in dimensionless form) is: find
# $(u_h, p_h) \in V_h^{u_D} \times Q_h$ such that
#
# $$
# \begin{align}
#   \int_\Omega \partial_t u_h \cdot v + a_h(u_h, v_h) + c_h(u_h; u_h, v_h)
#   + b_h(v_h, p_h) &= \int_\Omega f \cdot v_h + L_{a_h}(v_h) +
#   L_{c_h}(v_h) \quad \forall v_h \in V_h^0, \\
#   b_h(u_h, q_h) &= 0 \quad \forall q_h \in Q_h,
# \end{align}
# $$
#
# where
# $\renewcommand{\sumK}[0]{\sum_{K \in \mathcal{T}_h}}$
# $\renewcommand{\sumF}[0]{\sum_{F \in \mathcal{F}_h}}$
#
# $$
# \begin{align}
#   a_h(u, v) &= Re^{-1} \left(\sumK \int_K \nabla u : \nabla v
#   - \sumF \int_F \avg{\nabla u} : \jump{v}
#   - \sumF \int_F \avg{\nabla v} : \jump{u} \\
#   + \sumF \int_F \frac{\alpha}{h_K} \jump{u} : \jump{v}\right), \\
#   c_h(w; u, v) &= - \sumK \int_K u \cdot \nabla \cdot (v \otimes w)
#   + \sumK \int_{\partial_K} w \cdot n \hat{u}^{w} \cdot v, \\
#   L_{a_h}(v_h) &= Re^{-1} \left(- \int_{\partial \Omega} u_D \otimes n :
#   \nabla_h v_h + \frac{\alpha}{h} u_D \otimes n : v_h \otimes n \right),
#   \\
#   L_{c_h}(v_h) &= - \int_{\partial \Omega} u_D \cdot n \hat{u}_D \cdot
#   v_h, \\
#   b_h(v, q) &= - \int_K \nabla \cdot v q.
# \end{align}
# $$
#
#
# ## Implementation
#
# We begin by importing the required modules and functions

import importlib.util

if importlib.util.find_spec("petsc4py") is not None:
    import dolfinx

    if not dolfinx.has_petsc:
        print("This demo requires DOLFINx to be compiled with PETSc enabled.")
        exit(0)
else:
    print("This demo requires petsc4py.")
    exit(0)

from mpi4py import MPI

# +
import numpy as np

import ufl
from dolfinx import default_real_type, fem, io, mesh

try:
    from petsc4py import PETSc

    import dolfinx

    if not dolfinx.has_petsc:
        print("This demo requires DOLFINx to be compiled with PETSc enabled.")
        exit(0)
except ModuleNotFoundError:
    print("This demo requires petsc4py.")
    exit(0)

from dolfinx.fem.petsc import LinearProblem

if np.issubdtype(PETSc.ScalarType, np.complexfloating):
    print("Demo should only be executed with DOLFINx real mode")
    exit(0)
# -

# We also define some helper functions that will be used later


# +
def norm_L2(comm, v):
    """Compute the L2(Ω)-norm of v"""
    return np.sqrt(
        comm.allreduce(fem.assemble_scalar(fem.form(ufl.inner(v, v) * ufl.dx)), op=MPI.SUM)
    )


def domain_average(msh, v):
    """Compute the average of a function over the domain"""
    vol = msh.comm.allreduce(
        fem.assemble_scalar(fem.form(fem.Constant(msh, default_real_type(1.0)) * ufl.dx)),
        op=MPI.SUM,
    )
    return (1 / vol) * msh.comm.allreduce(fem.assemble_scalar(fem.form(v * ufl.dx)), op=MPI.SUM)


def u_e_expr(x):
    """Expression for the exact velocity solution to Kovasznay flow"""
    return np.vstack(
        (
            1
            - np.exp((Re / 2 - np.sqrt(Re**2 / 4 + 4 * np.pi**2)) * x[0])
            * np.cos(2 * np.pi * x[1]),
            (Re / 2 - np.sqrt(Re**2 / 4 + 4 * np.pi**2))
            / (2 * np.pi)
            * np.exp((Re / 2 - np.sqrt(Re**2 / 4 + 4 * np.pi**2)) * x[0])
            * np.sin(2 * np.pi * x[1]),
        )
    )


def p_e_expr(x):
    """Expression for the exact pressure solution to Kovasznay flow"""
    return (1 / 2) * (1 - np.exp(2 * (Re / 2 - np.sqrt(Re**2 / 4 + 4 * np.pi**2)) * x[0]))


def f_expr(x):
    """Expression for the applied force"""
    return np.vstack((np.zeros_like(x[0]), np.zeros_like(x[0])))


# -

# We define some simulation parameters

n = 16
num_time_steps = 25
t_end = 10
Re = 25  # Reynolds Number
k = 1  # Polynomial degree

# Next, we create a mesh and the required functions spaces over it.
# Since the velocity uses an $H(\text{div})$-conforming function space,
# we also create a vector valued discontinuous Lagrange space to
# interpolate into for artifact free visualisation.

# +
msh = mesh.create_unit_square(MPI.COMM_WORLD, n, n)

# Function spaces for the velocity and for the pressure
V = fem.functionspace(msh, ("Raviart-Thomas", k + 1))
Q = fem.functionspace(msh, ("Discontinuous Lagrange", k))
VQ = ufl.MixedFunctionSpace(V, Q)

# Function space for visualising the velocity field
gdim = msh.geometry.dim
W = fem.functionspace(msh, ("Discontinuous Lagrange", k + 1, (gdim,)))

# Define trial and test functions

u, p = ufl.TrialFunctions(VQ)
v, q = ufl.TestFunctions(VQ)

delta_t = fem.Constant(msh, default_real_type(t_end / num_time_steps))
alpha = fem.Constant(msh, default_real_type(6.0 * k**2))

h = ufl.CellDiameter(msh)
n = ufl.FacetNormal(msh)


def jump(phi, n):
    return ufl.outer(phi("+"), n("+")) + ufl.outer(phi("-"), n("-"))


# -

# We solve the Stokes problem for the initial condition, omitting the
# convective term:

# +
a = (1.0 / Re) * (
    ufl.inner(ufl.grad(u), ufl.grad(v)) * ufl.dx
    - ufl.inner(ufl.avg(ufl.grad(u)), jump(v, n)) * ufl.dS
    - ufl.inner(jump(u, n), ufl.avg(ufl.grad(v))) * ufl.dS
    + (alpha / ufl.avg(h)) * ufl.inner(jump(u, n), jump(v, n)) * ufl.dS
    - ufl.inner(ufl.grad(u), ufl.outer(v, n)) * ufl.ds
    - ufl.inner(ufl.outer(u, n), ufl.grad(v)) * ufl.ds
    + (alpha / h) * ufl.inner(ufl.outer(u, n), ufl.outer(v, n)) * ufl.ds
)
a -= ufl.inner(p, ufl.div(v)) * ufl.dx
a -= ufl.inner(ufl.div(u), q) * ufl.dx


f = fem.Function(W)
u_D = fem.Function(V)
u_D.interpolate(u_e_expr)
L = ufl.inner(f, v) * ufl.dx + (1 / Re) * (
    -ufl.inner(ufl.outer(u_D, n), ufl.grad(v)) * ufl.ds
    + (alpha / h) * ufl.inner(ufl.outer(u_D, n), ufl.outer(v, n)) * ufl.ds
)
L += ufl.inner(fem.Constant(msh, default_real_type(0.0)), q) * ufl.dx

# Boundary conditions
msh.topology.create_connectivity(msh.topology.dim - 1, msh.topology.dim)
boundary_facets = mesh.exterior_facet_indices(msh.topology)
boundary_vel_dofs = fem.locate_dofs_topological(V, msh.topology.dim - 1, boundary_facets)
bc_u = fem.dirichletbc(u_D, boundary_vel_dofs)
bcs = [bc_u]


# Assemble Stokes problem
solver_options = {
    "ksp_type": "preonly",
    "pc_type": "lu",
    "pc_factor_mat_solver_type": "mumps",
    "mat_mumps_icntl_14": 80,  # Increase MUMPS working memory
    "mat_mumps_icntl_24": 1,  # Option to support solving a singular matrix (pressure nullspace)
    "mat_mumps_icntl_25": 0,  # Option to support solving a singular matrix (pressure nullspace)
    "ksp_error_if_not_converged": 1,
}
u_h = fem.Function(V)
p_h = fem.Function(Q)
p_h.name = "p"
stokes_problem = LinearProblem(
    ufl.extract_blocks(a),
    ufl.extract_blocks(L),
    u=[u_h, p_h],
    bcs=bcs,
    kind="mpi",
    petsc_options=solver_options,
)

try:
    stokes_problem.solve()
except PETSc.Error as e:  # type: ignore
    if e.ierr == 92:
        print("The required PETSc solver/preconditioner is not available. Exiting.")
        print(e)
        exit(0)
    else:
        raise e

# Subtract the average of the pressure since it is only determined up to
# a constant
p_h.x.array[:] -= domain_average(msh, p_h)

u_vis = fem.Function(W)
u_vis.name = "u"
u_vis.interpolate(u_h)

# Write initial condition to file
t = 0.0
try:
    u_file = io.VTXWriter(msh.comm, "u.bp", u_vis)
    p_file = io.VTXWriter(msh.comm, "p.bp", p_h)
    u_file.write(t)
    p_file.write(t)
except AttributeError:
    print("File output requires ADIOS2.")

# Create function to store solution and previous time step
u_n = fem.Function(V)
u_n.x.array[:] = u_h.x.array
# -

# Now we add the time stepping and convective terms

# +
lmbda = ufl.conditional(ufl.gt(ufl.dot(u_n, n), 0), 1, 0)
u_uw = lmbda("+") * u("+") + lmbda("-") * u("-")
a += (
    ufl.inner(u / delta_t, v) * ufl.dx
    - ufl.inner(u, ufl.div(ufl.outer(v, u_n))) * ufl.dx
    + ufl.inner((ufl.dot(u_n, n))("+") * u_uw, v("+")) * ufl.dS
    + ufl.inner((ufl.dot(u_n, n))("-") * u_uw, v("-")) * ufl.dS
    + ufl.inner(ufl.dot(u_n, n) * lmbda * u, v) * ufl.ds
)

L += (
    ufl.inner(u_n / delta_t, v) * ufl.dx
    - ufl.inner(ufl.dot(u_n, n) * (1 - lmbda) * u_D, v) * ufl.ds
)

navier_stokes_problem = LinearProblem(
    ufl.extract_blocks(a),
    ufl.extract_blocks(L),
    u=[u_h, p_h],
    bcs=bcs,
    kind="mpi",
    petsc_options=solver_options,
)

# Time stepping loop
for n in range(num_time_steps):
    t += delta_t.value

    navier_stokes_problem.solve()
    p_h.x.array[:] -= domain_average(msh, p_h)

    u_vis.interpolate(u_h)

    # Write to file
    try:
        u_file.write(t)
        p_file.write(t)
    except NameError:
        pass

    # Update u_n
    u_n.x.array[:] = u_h.x.array

try:
    u_file.close()
    p_file.close()
except NameError:
    pass
# -

# Now we compare the computed solution to the exact solution

# +
# Function spaces for exact velocity and pressure
V_e = fem.functionspace(msh, ("Lagrange", k + 3, (gdim,)))
Q_e = fem.functionspace(msh, ("Lagrange", k + 2))

u_e = fem.Function(V_e)
u_e.interpolate(u_e_expr)

p_e = fem.Function(Q_e)
p_e.interpolate(p_e_expr)

# Compute errors
e_u = norm_L2(msh.comm, u_h - u_e)
e_div_u = norm_L2(msh.comm, ufl.div(u_h))

# This scheme conserves mass exactly, so check this
assert np.isclose(e_div_u, 0.0, atol=float(1.0e5 * np.finfo(default_real_type).eps))
p_e_avg = domain_average(msh, p_e)
e_p = norm_L2(msh.comm, p_h - (p_e - p_e_avg))

if msh.comm.rank == 0:
    print(f"e_u = {e_u}")
    print(f"e_div_u = {e_div_u}")
    print(f"e_p = {e_p}")
# -
