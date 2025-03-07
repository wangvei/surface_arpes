#pragma once
#include "params.hpp"
#include "poisson_solver_base.hpp"

#include <es_fe/geometry.hpp>
#include <es_fe/io/matlab_writer1.hpp>
#include <es_fe/math.hpp>
#include <es_fe/mesh/mesh1.hpp>
#include <es_la/dense.hpp>
#include <es_la/sparse.hpp>
#include <es_la/io.hpp>
#include <es_util/algorithm.hpp>
#include <es_util/numeric.hpp>
#include <es_util/phys.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

template<class Density_predictor>
class Poisson_solver final : public Poisson_solver_base
{
private:
	using Element = Poisson_element;

public:
	Poisson_solver(const es_fe::Mesh1& mesh, const Params& params) :
		Poisson_solver_base(mesh, params), density_predictor_(params, solution_view())
	{}

	void init()
	{
		Poisson_solver_base::init();
		solution_ = 0; // Local charge neutrality at the beginning
	}

	void solve()
	{
		density_predictor_.before_solve();
		Poisson_solver_base::solve();
	}

	Density_predictor& density_predictor()
	{
		return density_predictor_;
	}

private:
	virtual void assemble() override
	{
		matrix_.zero();
		rhs_ = 0;

		for (const auto& face : mesh().edges())
			assemble_on_edge(face);
	}

	void assemble_on_edge(const es_fe::Mesh1::Edge_view& edge)
	{
		using Stiff_quadr = es_fe::Quadr<2 * (Element::order - 1), 1>;
		using Mass_quadr = es_fe::Quadr<2 * Element::order, 1>;

		const auto grads = es_fe::gradients<Element, Stiff_quadr>(es_fe::inv_jacobian(edge));
		const auto dofs = system().dof_mapper().dofs(edge);
		const auto density = density_predictor_.template get<Mass_quadr>(dofs, edge);

		const auto stiffness_matrix = es_fe::stiffness_matrix<Element, Stiff_quadr>(grads, p_.eps);
		const auto mass_matrix = es_fe::mass_matrix<Element, Mass_quadr>(density.second, es_util::math::four_pi);
		const auto load_vector = es_fe::load_vector<Element, Mass_quadr>(density.first, es_util::math::four_pi);

		const auto length = es_fe::length(edge);
		for (es_fe::Local_index c = 0; c < dofs.size(); ++c)
		{
			if (dofs[c].is_free)
			{
				for (es_fe::Local_index r = 0; r <= c; ++r)
					if (dofs[r].is_free)
					{
						const auto [ir, ic] = es_util::sorted(dofs[r].index, dofs[c].index);
						matrix_(ir, ic) += length * (stiffness_matrix(r, c) - mass_matrix(r, c));
					}

				rhs_[dofs[c].index] += length * load_vector[c];
			}

			for (es_fe::Local_index r = 0; r < dofs.size(); ++r)
				if (dofs[r].is_free)
				{
					const auto ir = dofs[r].index;
					const auto ic = dofs[c].index;
					rhs_[ir] -= length * stiffness_matrix(r, c) * solution_[ic];
				}
		}
	}

public:
	void write_mat(const std::string& file_name)
	{
		using namespace es_util::au::literals;

		es_la::Vector_xd ec(*mesh().n_vertices(), 0);
		es_la::Vector_xd n(*mesh().n_edges(), 0);

		for (es_fe::Vertex_index vertex{0}; vertex < mesh().n_vertices(); ++vertex)
		{
			const auto dofs = system().dof_mapper().vertex_dofs(vertex);
			ec[*vertex] = -es_util::au::to_evolt(solution_[dofs[0].index]);
		}

		for (auto& edge : mesh().edges())
		{
			using Edge_mid_pt = es_fe::Quadr<1, 1>;

			const auto dofs = system().dof_mapper().dofs(edge);
			const auto density = density_predictor_.template get<Edge_mid_pt>(dofs, edge);
			n[**edge] = es_util::au::to_per_cm3(density.first[0]);
		}

		es_fe::Matlab_writer1 m(file_name, mesh(), 1_nm);
		m.write_vertex_field("ec", ec);
		m.write_edge_field("n", n);

		const auto fermi_level = charge_neutral_fermi_level(p_);
		m.write_scalar("f", es_util::au::to_evolt(fermi_level));
	}

	void write_la(const std::string& file_name)
	{
		es_la::La_file_writer lfw(file_name);
		lfw.write("solution", solution_);
	}

	void read_la(const std::string& file_name)
	{
		es_la::La_file_reader lfw(file_name);
		lfw.read(solution_);
	}

private:
	Density_predictor density_predictor_;
};
