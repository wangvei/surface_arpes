#pragma once
#include "params.hpp"
#include "poisson_solver_base.hpp"
#include "tools.hpp"

#include <es_la/dense.hpp>
#include <es_util/numeric.hpp>

#include <cstddef>
#include <utility>

// Calculator of electron density using the Thomas-Fermi (quasi-classical) approximation
class Classical_density_predictor
{
private:
	using Poisson_solution_view = Poisson_solver_base::Solution_view;

public:
	Classical_density_predictor(const Params& params, Poisson_solution_view phi) : p_(params), phi_(phi)
	{
		effective_dos_ = effective_3d_dos(p_);
		fermi_level_ = charge_neutral_fermi_level(p_);
	}

	void before_solve()
	{}

	template<class Quadr, class Dofs>
	auto get(const Dofs& dofs, const es_fe::Mesh1::Edge_view&) const
	{
		std::pair<es_la::Vector_d<Quadr::size>, es_la::Vector_d<Quadr::size>> density;

		const auto phis = es_fe::at_quadr<Quadr>(phi_, dofs);
		for (std::size_t iq = 0; iq < Quadr::size; ++iq)
		{
			const auto z = (phis[iq] + fermi_level_) / p_.temp;
			density.first[iq] = p_.dopant_conc - effective_dos_ * es_util::fd_int_half(z);
			density.second[iq] = -effective_dos_ / p_.temp * es_util::fd_int_minus_half(z);
		}

		return density;
	}

private:
	const Params& p_;
	double effective_dos_;
	double fermi_level_;

	const Poisson_solution_view phi_;
};
