// Copyright (C) 2007-2009 Anders Logg.
// Licensed under the GNU LGPL Version 2.1.
//
// Modified by Garth N. Wells, 2008.
// Modified by Martin Alnes, 2008.
//
// First added:  2007-04-02
// Last changed: 2009-09-29

#ifndef __FORM_H
#define __FORM_H

#include <vector>
#include <boost/shared_ptr.hpp>
#include <dolfin/common/types.h>

// Forward declaration
namespace ufc
{
  class form;
}

namespace dolfin
{

  class FunctionSpace;
  class Coefficient;
  class Mesh;

  /// Base class for UFC code generated by FFC for DOLFIN with option -l

  class Form
  {
  public:

    /// Constructor
    Form();

    /// Create form of given rank with given number of coefficients
    Form(dolfin::uint rank, dolfin::uint num_coefficients);

    // FIXME: Pointers need to be const here to work with SWIG. Is there a fix for this?

    /// Create form from given Constructor used in the python interface
    Form(const ufc::form& ufc_form,
         const std::vector<FunctionSpace*>& function_spaces,
         const std::vector<Coefficient*>& coefficients);

    /// Destructor
    virtual ~Form();

    /// Return rank of form (bilinear form = 2, linear form = 1, functional = 0, etc)
    uint rank() const;

    /// Return number of coefficients
    uint num_coefficients() const;

    /// Return mesh
    const Mesh& mesh() const;

    /// Return function space for given argument
    const boost::shared_ptr<const FunctionSpace> function_space(uint i) const;

    /// Return function spaces for arguments
    std::vector<const FunctionSpace*> function_spaces() const;

    /// Return given coefficient
    const Coefficient& coefficient(uint i) const;

    /// Return coefficients
    std::vector<const Coefficient*> coefficients() const;

    /// Return the number of the coefficient with this name
    virtual dolfin::uint coefficient_number(const std::string & name) const;

    /// Return the name of the coefficient with this number
    virtual std::string coefficient_name(dolfin::uint i) const;

    /// Return UFC form
    const ufc::form& ufc_form() const;

    /// Check function spaces and coefficients
    void check() const;

    /// Friends
    friend class CoefficientAssigner;
    friend class LinearPDE;
    friend class NonlinearPDE;
    friend class VariationalProblem;

  protected:

    // Function spaces (one for each argument)
    std::vector<boost::shared_ptr<const FunctionSpace> > _function_spaces;

    // Coefficients
    std::vector<boost::shared_ptr<const Coefficient> > _coefficients;

    // The UFC form
    boost::shared_ptr<const ufc::form> _ufc_form;

  };

}

#endif
