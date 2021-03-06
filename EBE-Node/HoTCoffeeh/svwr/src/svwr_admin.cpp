#include<iostream>
#include<sstream>
#include<string>
#include<fstream>
#include<cmath>
#include<iomanip>
#include<vector>
#include<stdio.h>
#include<time.h>
#include<algorithm>

#include<gsl/gsl_sf_bessel.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_rng.h>            // gsl random number generators
#include <gsl/gsl_randist.h>        // gsl random number distributions
#include <gsl/gsl_vector.h>         // gsl vector and matrix definitions
#include <gsl/gsl_blas.h>           // gsl linear algebra stuff
#include <gsl/gsl_multifit_nlin.h>  // gsl multidimensional fitting

#include "svwr.h"
#include "Arsenal.h"
#include "gauss_quadrature.h"

using namespace std;

template <typename T> int sgn(T val)
{
    return (T(0) < val) - (val < T(0));
}

SourceVariances::SourceVariances(ParameterReader* paraRdr_in, particle_info* particle, particle_info* all_particles_in, int Nparticle_in,
					vector<int> chosen_resonances_in, int particle_idx, ofstream& myout)
{
	paraRdr = paraRdr_in;

	USE_PLANE_PSI_ORDER = paraRdr->getVal("use_plane_psi_order");
	INCLUDE_DELTA_F = paraRdr->getVal("include_delta_f");
	GROUPING_PARTICLES = paraRdr->getVal("grouping_particles");
	PARTICLE_DIFF_TOLERANCE = paraRdr->getVal("particle_diff_tolerance");
	IGNORE_LONG_LIVED_RESONANCES = paraRdr->getVal("ignore_long_lived_resonances");
	n_order = paraRdr->getVal("n_order");
	tol = paraRdr->getVal("tolerance");
	flagneg = paraRdr->getVal("flag_negative_S");
	max_lifetime = paraRdr->getVal("max_lifetime");

	n_pT_pts = paraRdr->getVal("SV_npT");
	n_pphi_pts = paraRdr->getVal("SV_npphi");
	nKT = paraRdr->getVal("nKT");
	nKphi = paraRdr->getVal("nKphi");
	KT_min = paraRdr->getVal("KTmin");
	KT_max = paraRdr->getVal("KTmax");

	//set ofstream for output file
	global_out_stream_ptr = &myout;
	
	//particle information (both final-state particle used in HBT and all decay decay_channels)
	particle_name = particle->name;
	particle_mass = particle->mass;
	particle_sign = particle->sign;
	particle_gspin = particle->gspin;
	particle_id = particle_idx;
	target_particle_id = particle_id;
	particle_monval = particle->monval;
	S_prefactor = 1.0/(8.0*(M_PI*M_PI*M_PI))/hbarC/hbarC/hbarC;
	all_particles = all_particles_in;
	for (int icr = 0; icr < (int)chosen_resonances_in.size(); icr++)
		chosen_resonances.push_back(chosen_resonances_in[icr]);
	thermal_pions_only = false;
	Nparticle = Nparticle_in;
	read_in_all_dN_dypTdpTdphi = false;
	output_all_dN_dypTdpTdphi = true;
	//currentfolderindex = -1;
	current_level_of_output = 0;

	//just need this for various dummy momentum calculations
	P_eval = new double [4];
	Pp = new double [4];
	Pm = new double [4];

	v_min = -1.;
	v_max = 1.;
	zeta_min = 0.;
	zeta_max = M_PI;
	
	//default: use delta_f in calculations
	Set_use_delta_f();

	n_weighting_functions = 15;	//AKA, number of SV's

	zvec = new double [4];

//****************************************************************************************************
//OLD CODE FOR READING IN SELECTED decay_channels...
	current_resonance_mass = 0.0;
	current_resonance_mu = 0.0;
	current_resonance_Gamma = 0.0;
	current_resonance_total_br = 0.0;
	current_resonance_decay_masses = new double [2];
	current_resonance_decay_masses[0] = 0.0;
	current_resonance_decay_masses[1] = 0.0;
	previous_resonance_particle_id = -1;
	previous_decay_channel_idx = -1;				//different for each decay channel
	previous_resonance_mass = 0.0;
	previous_resonance_Gamma = 0.0;
	previous_resonance_total_br = 0.0;
	if (chosen_resonances.size() == 0)
	{
		n_decay_channels = 1;
		n_resonance = 0;
		thermal_pions_only = true;
		if (VERBOSE > 0) *global_out_stream_ptr << "Thermal pion(+) only!" << endl;
		decay_channels = new decay_info [n_decay_channels];
		decay_channels[0].resonance_decay_masses = new double [Maxdecaypart];	// Maxdecaypart == 5
	}
	else
	{
		//n_decay_channels is actually total number of decay channels which can generate pions
		//from chosen decay_channels
		n_decay_channels = get_number_of_decay_channels(chosen_resonances, all_particles);
		n_resonance = (int)chosen_resonances.size();
		if (VERBOSE > 0) *global_out_stream_ptr << "Computed n_decay_channels = " << n_decay_channels << endl
							<< "Computed n_resonance = " << n_resonance << endl;
		decay_channels = new decay_info [n_decay_channels];
		int temp_idx = 0;
		for (int icr = 0; icr < n_resonance; icr++)
		{
			particle_info particle_temp = all_particles[chosen_resonances[icr]];
			if (VERBOSE > 0) *global_out_stream_ptr << "Loading resonance: " << particle_temp.name
					<< ", chosen_resonances[" << icr << "] = " << chosen_resonances[icr] << endl;
			for (int idecay = 0; idecay < particle_temp.decays; idecay++)
			{
				if (VERBOSE > 0) *global_out_stream_ptr << "Current temp_idx = " << temp_idx << endl;
				if (temp_idx == n_decay_channels)	//i.e., all contributing decay channels have been loaded
					break;
				decay_channels[temp_idx].resonance_name = particle_temp.name;		// set name of resonance

				//check if effective branching is too small for inclusion in source variances
				bool effective_br_is_too_small = false;
				if (particle_temp.decays_effective_branchratio[idecay] <= 1.e-12)
					effective_br_is_too_small = true;

				decay_channels[temp_idx].resonance_particle_id = chosen_resonances[icr];	// set index of resonance in all_particles
				decay_channels[temp_idx].resonance_idx = icr;					// set index of resonance in chosen_resonances
				decay_channels[temp_idx].resonance_decay_masses = new double [Maxdecaypart];	// Maxdecaypart == 5
				decay_channels[temp_idx].resonance_decay_monvals = new double [Maxdecaypart];	// Maxdecaypart == 5
				decay_channels[temp_idx].resonance_decay_Gammas = new double [Maxdecaypart];	// Maxdecaypart == 5



				//*** SETTING RESONANCE DECAY MASSES DIFFERENTLY FOR NEW ANALYZE SF
				for (int ii = 0; ii < Maxdecaypart; ii++)
				{
					decay_channels[temp_idx].resonance_decay_monvals[ii] = particle_temp.decays_part[idecay][ii];
					if (particle_temp.decays_part[idecay][ii] == 0)
					{
						decay_channels[temp_idx].resonance_decay_masses[ii] = 0.0;
						decay_channels[temp_idx].resonance_decay_Gammas[ii] = 0.0;

					}
					else
					{
						int tempID = lookup_particle_id_from_monval(all_particles, Nparticle, particle_temp.decays_part[idecay][ii]);
						decay_channels[temp_idx].resonance_decay_masses[ii] = all_particles[tempID].mass;
						decay_channels[temp_idx].resonance_decay_Gammas[ii] = all_particles[tempID].width;

					}
				}
				decay_channels[temp_idx].resonance_mu = particle_temp.mu;
				decay_channels[temp_idx].resonance_gspin = particle_temp.gspin;
				decay_channels[temp_idx].resonance_sign = particle_temp.sign;
				decay_channels[temp_idx].resonance_mass = particle_temp.mass;
				decay_channels[temp_idx].nbody = abs(particle_temp.decays_Npart[idecay]);
				decay_channels[temp_idx].resonance_Gamma = particle_temp.width;
				decay_channels[temp_idx].resonance_total_br = particle_temp.decays_effective_branchratio[idecay];
				decay_channels[temp_idx].resonance_direct_br = particle_temp.decays_branchratio[idecay];

				
				//check if particle lifetime is too long for inclusion in source variances
				bool lifetime_is_too_long = false;
				if (decay_channels[temp_idx].resonance_Gamma < hbarC / max_lifetime && IGNORE_LONG_LIVED_RESONANCES)
					lifetime_is_too_long = true;		//i.e., for lifetimes longer than 100 fm/c, skip decay channel

				if (VERBOSE > 0) *global_out_stream_ptr << "Resonance = " << decay_channels[temp_idx].resonance_name << ", decay channel " << idecay + 1
						<< ": mu=" << decay_channels[temp_idx].resonance_mu
						<< ", gs=" << decay_channels[temp_idx].resonance_gspin << ", sign=" << decay_channels[temp_idx].resonance_sign
						<< ", M=" << decay_channels[temp_idx].resonance_mass << ", nbody=" << decay_channels[temp_idx].nbody
						<< ", Gamma=" << decay_channels[temp_idx].resonance_Gamma << ", total br=" << decay_channels[temp_idx].resonance_total_br
						<< ", direct br=" << decay_channels[temp_idx].resonance_direct_br << endl;

				//set daughter particles masses for each decay channel
				//currently assuming no more than nbody = 3

				if (VERBOSE > 0) *global_out_stream_ptr << "Resonance = " << decay_channels[temp_idx].resonance_name << ": ";
				for (int decay_part_idx = 0; decay_part_idx < decay_channels[temp_idx].nbody; decay_part_idx++)
					if (VERBOSE > 0) *global_out_stream_ptr << "m[" << decay_part_idx << "] = "
						<< decay_channels[temp_idx].resonance_decay_masses[decay_part_idx] << "   "
						<< decay_channels[temp_idx].resonance_decay_monvals[decay_part_idx] << "   ";
				if (VERBOSE > 0) *global_out_stream_ptr << endl << endl;

				// if decay channel parent resonance is not too long-lived
				// and decay channel contains at least one target daughter particle,
				// include channel
				decay_channels[temp_idx].include_channel = (!lifetime_is_too_long
											&& !effective_br_is_too_small);

				temp_idx++;
			}
		}
	}
	
	//*****************************************************************
	// Only make dN_dypTdpTdphi_moments large enough to hold all necessary resonance, not decay channels
	//*****************************************************************
	dN_dypTdpTdphi_moments = new double *** [Nparticle];
	ln_dN_dypTdpTdphi_moments = new double *** [Nparticle];
	sign_of_dN_dypTdpTdphi_moments = new double *** [Nparticle];
	for (int ir = 0; ir < Nparticle; ir++)
	{
		dN_dypTdpTdphi_moments[ir] = new double ** [n_weighting_functions];
		ln_dN_dypTdpTdphi_moments[ir] = new double ** [n_weighting_functions];
		sign_of_dN_dypTdpTdphi_moments[ir] = new double ** [n_weighting_functions];
		for (int wfi = 0; wfi < n_weighting_functions; wfi++)
		{
			dN_dypTdpTdphi_moments[ir][wfi] = new double * [n_pT_pts];
			ln_dN_dypTdpTdphi_moments[ir][wfi] = new double * [n_pT_pts];
			sign_of_dN_dypTdpTdphi_moments[ir][wfi] = new double * [n_pT_pts];
			for (int ipT = 0; ipT < n_pT_pts; ipT++)
			{
				dN_dypTdpTdphi_moments[ir][wfi][ipT] = new double [n_pphi_pts];
				ln_dN_dypTdpTdphi_moments[ir][wfi][ipT] = new double [n_pphi_pts];
				sign_of_dN_dypTdpTdphi_moments[ir][wfi][ipT] = new double [n_pphi_pts];
				for (int ipphi = 0; ipphi < n_pphi_pts; ipphi++)
					{
						dN_dypTdpTdphi_moments[ir][wfi][ipT][ipphi] = 0.0;
						ln_dN_dypTdpTdphi_moments[ir][wfi][ipT][ipphi] = 0.0;
						sign_of_dN_dypTdpTdphi_moments[ir][wfi][ipT][ipphi] = 0.0;
					}
			}
		}
	}

	v_pts = new double [n_v_pts];
	v_wts = new double [n_v_pts];
	zeta_pts = new double [n_zeta_pts];
	zeta_wts = new double [n_zeta_pts];

//cerr << "setting gaussian integrations points..." << endl;
	//initialize all gaussian points for resonance integrals
	//syntax: int gauss_quadrature(int order, int kind, double alpha, double beta, double a, double b, double x[], double w[])
	gauss_quadrature(n_zeta_pts, 1, 0.0, 0.0, zeta_min, zeta_max, zeta_pts, zeta_wts);
	gauss_quadrature(n_v_pts, 1, 0.0, 0.0, v_min, v_max, v_pts, v_wts);
	//for (int iv = 0; iv < n_v_pts; iv++)
	//	*global_out_stream_ptr << setw(8) << setprecision(15) << "v_pts[" << iv << "] = " << v_pts[iv] << " and v_wts[" << iv << "] = " << v_wts[iv] << endl;
	//for (int izeta = 0; izeta < n_zeta_pts; izeta++)
	//	*global_out_stream_ptr << setw(8) << setprecision(15) << "zeta_pts[" << izeta << "] = " << zeta_pts[izeta] << " and zeta_wts[" << izeta << "] = " << zeta_wts[izeta] << endl;
//cerr << "finished all that stuff..." << endl;
	

   //single particle spectra for plane angle determination
   /*SP_pT = new double [n_pT_pts];
   SP_pT_weight = new double [n_pT_pts];
   gauss_quadrature(n_pT_pts, 1, 0.0, 0.0, SP_pT_min, SP_pT_max, SP_pT, SP_pT_weight);
   SP_pphi = new double [n_pphi_pts];
   SP_pphi_weight = new double [n_pphi_pts];
   gauss_quadrature(n_pphi_pts, 1, 0.0, 0.0, 0.0, 2.*M_PI, SP_pphi, SP_pphi_weight);*/
   SP_p_y = 0.0e0;

//initialize and set evenly spaced grid of px-py points in transverse plane,
//and corresponding p0 and pz points
	SP_pT = new double [n_pT_pts];
	SP_pphi = new double [n_pphi_pts];
	sin_SP_pphi = new double [n_pphi_pts];
	cos_SP_pphi = new double [n_pphi_pts];
	SP_p0 = new double* [n_pT_pts];
	SP_pz = new double* [n_pT_pts];
	SP_pT_weight = new double [n_pT_pts];
	SP_pphi_weight = new double [n_pphi_pts];
	for(int ipt=0; ipt<n_pT_pts; ipt++)
	{
		SP_p0[ipt] = new double [eta_s_npts];
		SP_pz[ipt] = new double [eta_s_npts];
	}
	gauss_quadrature(n_pT_pts, 5, 0.0, 0.0, 0.0, 13.0, SP_pT, SP_pT_weight);
	gauss_quadrature(n_pphi_pts, 1, 0.0, 0.0, SP_pphi_min, SP_pphi_max, SP_pphi, SP_pphi_weight);

	for(int ipphi=0; ipphi<n_pphi_pts; ipphi++)
	{
		sin_SP_pphi[ipphi] = sin(SP_pphi[ipphi]);
		cos_SP_pphi[ipphi] = cos(SP_pphi[ipphi]);
	}

	dN_dypTdpTdphi = new double* [n_pT_pts];
	cosine_iorder = new double* [n_pT_pts];
	sine_iorder = new double* [n_pT_pts];

	for(int i = 0; i < n_pT_pts; i++)
	{
		dN_dypTdpTdphi[i] = new double [n_pphi_pts];
		cosine_iorder[i] = new double [n_order];
		sine_iorder[i] = new double [n_order];
	}

	dN_dydphi = new double [n_pphi_pts];
	dN_dypTdpT = new double [n_pT_pts];
	pTdN_dydphi = new double [n_pphi_pts];

	for(int i = 0; i < n_pphi_pts; i++)
	{
		dN_dydphi[i] = 0.0e0;
		pTdN_dydphi[i] = 0.0e0;
		for(int j=0; j<n_pT_pts; j++)
			dN_dypTdpTdphi[j][i] = 0.0e0;
	}

	for(int i = 0; i < n_pT_pts; i++)
	{
		dN_dypTdpT[i] = 0.0e0;
		for(int j = 0; j < n_order; j++)
		{
			cosine_iorder[i][j] = 0.0e0;
			sine_iorder[i][j] = 0.0e0;
		}
	}

	plane_angle = new double [n_order];

	//pair momentum
	K_T = new double [nKT];
	double dK_T = (KT_max - KT_min)/(nKT - 1 + 1e-100);
	for(int i = 0; i < nKT; i++)
		K_T[i] = KT_min + i*dK_T;

	//K_y = p_y;
	K_y = 0.;
	ch_K_y = cosh(K_y);
	sh_K_y = sinh(K_y);
	beta_l = sh_K_y/ch_K_y;
	K_phi = new double [nKphi];
	K_phi_weight = new double [nKphi];
	gauss_quadrature(nKphi, 1, 0.0, 0.0, Kphi_min, Kphi_max, K_phi, K_phi_weight);

	//spatial rapidity grid
	eta_s = new double [eta_s_npts];
	eta_s_weight = new double [eta_s_npts];
	gauss_quadrature(eta_s_npts, 1, 0.0, 0.0, eta_s_i, eta_s_f, eta_s, eta_s_weight);
	ch_eta_s = new double [eta_s_npts];
	sh_eta_s = new double [eta_s_npts];
	for (int ieta = 0; ieta < eta_s_npts; ieta++)
	{
		ch_eta_s[ieta] = cosh(eta_s[ieta]);
		sh_eta_s[ieta] = sinh(eta_s[ieta]);
	}

	S_func = new double* [n_pT_pts];
	xs_S = new double* [n_pT_pts];
	xs2_S = new double* [n_pT_pts];
	xo_S = new double* [n_pT_pts];
	xo2_S = new double* [n_pT_pts];
	xl_S = new double* [n_pT_pts];
	xl2_S = new double* [n_pT_pts];
	t_S = new double* [n_pT_pts];
	t2_S = new double* [n_pT_pts];
	xo_xs_S = new double* [n_pT_pts];
	xl_xs_S = new double* [n_pT_pts];
	xs_t_S = new double* [n_pT_pts];
	xo_xl_S = new double* [n_pT_pts];
	xo_t_S = new double* [n_pT_pts];
	xl_t_S = new double* [n_pT_pts];

        x_S = new double* [n_pT_pts];
        x2_S = new double* [n_pT_pts];
        y_S = new double* [n_pT_pts];
        y2_S = new double* [n_pT_pts];
        z_S = new double* [n_pT_pts];
        z2_S = new double* [n_pT_pts];
        t_S = new double* [n_pT_pts];
        t2_S = new double* [n_pT_pts];
        xy_S = new double* [n_pT_pts];
        xz_S = new double* [n_pT_pts];
        xt_S = new double* [n_pT_pts];
        yz_S = new double* [n_pT_pts];
        yt_S = new double* [n_pT_pts];
        zt_S = new double* [n_pT_pts];

	R2_side_grid = new double * [n_pT_pts];
	R2_out_grid = new double* [n_pT_pts];
	R2_outside_grid = new double* [n_pT_pts];
	R2_long_grid = new double* [n_pT_pts];
	R2_sidelong_grid = new double* [n_pT_pts];
	R2_outlong_grid = new double* [n_pT_pts];

	R2_side = new double* [nKT];
	R2_side_C = new double* [nKT];
	R2_side_S = new double* [nKT];
	R2_out = new double* [nKT];
	R2_out_C = new double* [nKT];
	R2_out_S = new double* [nKT];
	R2_long = new double* [nKT];
	R2_long_C = new double* [nKT];
	R2_long_S = new double* [nKT];
	R2_outside = new double* [nKT];
	R2_outside_C = new double* [nKT];
	R2_outside_S = new double* [nKT];
	R2_sidelong = new double* [nKT];
	R2_sidelong_C = new double* [nKT];
	R2_sidelong_S = new double* [nKT];
	R2_outlong = new double* [nKT];
	R2_outlong_C = new double* [nKT];
	R2_outlong_S = new double* [nKT];

	for(int i = 0; i < n_pT_pts; i++)
	{
		S_func[i] = new double [n_pphi_pts];
		xs_S[i] = new double [n_pphi_pts];
		xs2_S[i] = new double [n_pphi_pts];
		xo_S[i] = new double [n_pphi_pts];
		xo2_S[i] = new double [n_pphi_pts];
		xl_S[i] = new double [n_pphi_pts];
		xl2_S[i] = new double [n_pphi_pts];
		t_S[i] = new double [n_pphi_pts];
		t2_S[i] = new double [n_pphi_pts];
		xo_xs_S[i] = new double [n_pphi_pts];
		xl_xs_S[i] = new double [n_pphi_pts];
		xs_t_S[i] = new double [n_pphi_pts];
		xo_xl_S[i] = new double [n_pphi_pts];
		xo_t_S[i] = new double [n_pphi_pts];
		xl_t_S[i] = new double [n_pphi_pts];

		x_S[i] = new double [n_pphi_pts];
		x2_S[i] = new double [n_pphi_pts];
		y_S[i] = new double [n_pphi_pts];
		y2_S[i] = new double [n_pphi_pts];
		z_S[i] = new double [n_pphi_pts];
		z2_S[i] = new double [n_pphi_pts];
		t_S[i] = new double [n_pphi_pts];
		t2_S[i] = new double [n_pphi_pts];
		xy_S[i] = new double [n_pphi_pts];
		xz_S[i] = new double [n_pphi_pts];
		xt_S[i] = new double [n_pphi_pts];
		yz_S[i] = new double [n_pphi_pts];
		yt_S[i] = new double [n_pphi_pts];
		zt_S[i] = new double [n_pphi_pts];
	
		R2_side_grid[i] = new double [n_pphi_pts];
		R2_out_grid[i] = new double [n_pphi_pts];
		R2_outside_grid[i] = new double [n_pphi_pts];
		R2_long_grid[i] = new double [n_pphi_pts];
		R2_sidelong_grid[i] = new double [n_pphi_pts];
		R2_outlong_grid[i] = new double [n_pphi_pts];
	}

	for(int i=0; i<nKT; i++)
	{
		R2_side[i] = new double [nKphi];
		R2_side_C[i] = new double [n_order];
		R2_side_S[i] = new double [n_order];
		R2_out[i] = new double [nKphi];
		R2_out_C[i] = new double [n_order];
		R2_out_S[i] = new double [n_order];
		R2_outside[i] = new double [nKphi];
		R2_outside_C[i] = new double [n_order];
		R2_outside_S[i] = new double [n_order];
		R2_long[i] = new double [nKphi];
		R2_long_C[i] = new double [n_order];
		R2_long_S[i] = new double [n_order];
		R2_sidelong[i] = new double [nKphi];
		R2_sidelong_C[i] = new double [n_order];
		R2_sidelong_S[i] = new double [n_order];
		R2_outlong[i] = new double [nKphi];
		R2_outlong_C[i] = new double [n_order];
		R2_outlong_S[i] = new double [n_order];
	}

	//initialize all source variances and HBT radii/coeffs
	for(int i=0; i<n_pT_pts; i++)
	{
		for(int j=0; j<n_pphi_pts; j++)
		{
			S_func[i][j] = 0.;
			xs_S[i][j] = 0.;
			xs2_S[i][j] = 0.;
			xo_S[i][j] = 0.;
			xo2_S[i][j] = 0.;
			xl_S[i][j] = 0.;
			xl2_S[i][j] = 0.;
			t_S[i][j] = 0.;
			t2_S[i][j] = 0.;
			xo_xs_S[i][j] = 0.;
			xl_xs_S[i][j] = 0.;
			xs_t_S[i][j] = 0.;
			xo_xl_S[i][j] = 0.;
			xo_t_S[i][j] = 0.;
			xl_t_S[i][j] = 0.;

		        x_S[i][j] = 0.;
                        x2_S[i][j] = 0.;
                        y_S[i][j] = 0.;
                        y2_S[i][j] = 0.;
                        z_S[i][j] = 0.;
                        z2_S[i][j] = 0.;
                        t_S[i][j] = 0.;
                        t2_S[i][j] = 0.;
                        xy_S[i][j] = 0.;
                        xz_S[i][j] = 0.;
                        xt_S[i][j] = 0.;
                        yz_S[i][j] = 0.;
                        yt_S[i][j] = 0.;
                        zt_S[i][j] = 0.;

			R2_side_grid[i][j] = 0.;
			R2_out_grid[i][j] = 0.;
			R2_long_grid[i][j] = 0.;
			R2_outside_grid[i][j] = 0.;
			R2_sidelong_grid[i][j] = 0.;
			R2_outlong_grid[i][j] = 0.;
		}
	}
	for(int i=0; i<nKT; i++)
	{
		for(int j=0; j<nKphi; j++)
		{
			R2_side[i][j] = 0.;
			R2_out[i][j] = 0.;
			R2_long[i][j] = 0.;
			R2_outside[i][j] = 0.;
			R2_sidelong[i][j] = 0.;
			R2_outlong[i][j] = 0.;
		}
		for(int j=0; j<n_order; j++)
		{
			R2_side_C[i][j] = 0.;
			R2_side_S[i][j] = 0.;
			R2_out_C[i][j] = 0.;
			R2_out_S[i][j] = 0.;
			R2_outside_C[i][j] = 0.;
			R2_outside_S[i][j] = 0.;
			R2_long_C[i][j] = 0.;
			R2_long_S[i][j] = 0.;
			R2_sidelong_C[i][j] = 0.;
			R2_sidelong_S[i][j] = 0.;
			R2_outlong_C[i][j] = 0.;
			R2_outlong_S[i][j] = 0.;
		}
	}


   return;
}

SourceVariances::~SourceVariances()
{
   delete[] SP_pT;
   delete[] SP_pT_weight;
   delete[] SP_pphi;
   delete[] SP_pphi_weight;
   delete[] dN_dydphi;
   delete[] dN_dypTdpT;
   delete[] pTdN_dydphi;
   for(int i=0; i<n_pT_pts; i++)
   {
      delete[] dN_dypTdpTdphi[i];
      delete[] cosine_iorder[i];
      delete[] sine_iorder[i];
   }
   delete[] dN_dypTdpTdphi;
   delete[] cosine_iorder;
   delete[] sine_iorder;
   delete[] plane_angle;

   delete[] K_T;
   delete[] K_phi;
   delete[] K_phi_weight;
   delete[] eta_s;
   delete[] eta_s_weight;

   for(int i=0; i<nKT; i++)
   {
      delete[] R2_side[i];
   }

   delete[] R2_side;

   for(int i=0; i<n_pT_pts; i++)
   {
      delete[] S_func[i];
      delete[] xs_S[i];
      delete[] xs2_S[i];
   }
   delete[] S_func;
   delete[] xs_S;
   delete[] xs2_S;

   return;
}

void SourceVariances::Allocate_decay_channel_info()
{
	if (VERBOSE > 2) *global_out_stream_ptr << "Reallocating memory for decay channel information..." << endl;
	VEC_n2_v_factor = new double [n_v_pts];
	VEC_n2_zeta_factor = new double * [n_v_pts];
	VEC_n2_P_Y = new double [n_v_pts];
	VEC_n2_MTbar = new double [n_v_pts];
	VEC_n2_DeltaMT = new double [n_v_pts];
	VEC_n2_MTp = new double [n_v_pts];
	VEC_n2_MTm = new double [n_v_pts];
	VEC_n2_MT = new double * [n_v_pts];
	VEC_n2_PPhi_tilde = new double * [n_v_pts];
	VEC_n2_PPhi_tildeFLIP = new double * [n_v_pts];
	VEC_n2_PT = new double * [n_v_pts];
	VEC_n2_Pp = new double ** [n_v_pts];
	VEC_n2_alpha = new double ** [n_v_pts];
	VEC_n2_Pm = new double ** [n_v_pts];
	VEC_n2_alpha_m = new double ** [n_v_pts];
	for(int iv = 0; iv < n_v_pts; ++iv)
	{
		VEC_n2_MT[iv] = new double [n_zeta_pts];
		VEC_n2_PPhi_tilde[iv] = new double [n_zeta_pts];
		VEC_n2_PPhi_tildeFLIP[iv] = new double [n_zeta_pts];
		VEC_n2_PT[iv] = new double [n_zeta_pts];
		VEC_n2_Pp[iv] = new double * [n_zeta_pts];
		VEC_n2_alpha[iv] = new double * [n_zeta_pts];
		VEC_n2_Pm[iv] = new double * [n_zeta_pts];
		VEC_n2_alpha_m[iv] = new double * [n_zeta_pts];
		VEC_n2_zeta_factor[iv] = new double [n_zeta_pts];
		for(int izeta = 0; izeta < n_zeta_pts; ++izeta)
		{
			VEC_n2_Pp[iv][izeta] = new double [4];
			VEC_n2_alpha[iv][izeta] = new double [4];
			VEC_n2_Pm[iv][izeta] = new double [4];
			VEC_n2_alpha_m[iv][izeta] = new double [4];
		}
	}
	s_pts = new double [n_s_pts];
	s_wts = new double [n_s_pts];
	VEC_pstar = new double [n_s_pts];
	VEC_Estar = new double [n_s_pts];
	VEC_DeltaY = new double [n_s_pts];
	VEC_g_s = new double [n_s_pts];
	VEC_s_factor = new double [n_s_pts];
	VEC_v_factor = new double * [n_s_pts];
	VEC_zeta_factor = new double ** [n_s_pts];
	VEC_Yp = new double [n_s_pts];
	VEC_Ym = new double [n_s_pts];
	VEC_P_Y = new double * [n_s_pts];
	VEC_MTbar = new double * [n_s_pts];
	VEC_DeltaMT = new double * [n_s_pts];
	VEC_MTp = new double * [n_s_pts];
	VEC_MTm = new double * [n_s_pts];
	VEC_MT = new double ** [n_s_pts];
	VEC_PPhi_tilde = new double ** [n_s_pts];
	VEC_PPhi_tildeFLIP = new double ** [n_s_pts];
	VEC_PT = new double ** [n_s_pts];
	VEC_Pp = new double *** [n_s_pts];
	VEC_alpha = new double *** [n_s_pts];
	VEC_Pm = new double *** [n_s_pts];
	VEC_alpha_m = new double *** [n_s_pts];
	for(int is = 0; is < n_s_pts; ++is)
	{
		VEC_v_factor[is] = new double [n_v_pts];
		VEC_zeta_factor[is] = new double * [n_v_pts];
		VEC_P_Y[is] = new double [n_v_pts];
		VEC_MTbar[is] = new double [n_v_pts];
		VEC_DeltaMT[is] = new double [n_v_pts];
		VEC_MTp[is] = new double [n_v_pts];
		VEC_MTm[is] = new double [n_v_pts];
		VEC_MT[is] = new double * [n_v_pts];
		VEC_PPhi_tilde[is] = new double * [n_v_pts];
		VEC_PPhi_tildeFLIP[is] = new double * [n_v_pts];
		VEC_PT[is] = new double * [n_v_pts];
		VEC_Pp[is] = new double ** [n_v_pts];
		VEC_alpha[is] = new double ** [n_v_pts];
		VEC_Pm[is] = new double ** [n_v_pts];
		VEC_alpha_m[is] = new double ** [n_v_pts];
		for(int iv = 0; iv < n_v_pts; ++iv)
		{
			VEC_MT[is][iv] = new double [n_zeta_pts];
			VEC_PPhi_tilde[is][iv] = new double [n_zeta_pts];
			VEC_PPhi_tildeFLIP[is][iv] = new double [n_zeta_pts];
			VEC_PT[is][iv] = new double [n_zeta_pts];
			VEC_Pp[is][iv] = new double * [n_zeta_pts];
			VEC_alpha[is][iv] = new double * [n_zeta_pts];
			VEC_Pm[is][iv] = new double * [n_zeta_pts];
			VEC_alpha_m[is][iv] = new double * [n_zeta_pts];
			VEC_zeta_factor[is][iv] = new double [n_zeta_pts];
			for(int izeta = 0; izeta < n_zeta_pts; ++izeta)
			{
				VEC_Pp[is][iv][izeta] = new double [4];
				VEC_alpha[is][iv][izeta] = new double [4];
				VEC_Pm[is][iv][izeta] = new double [4];
				VEC_alpha_m[is][iv][izeta] = new double [4];
			}
		}
	}
	if (VERBOSE > 2) *global_out_stream_ptr << "Reallocated memory for decay channel information." << endl;

	return;
}

void SourceVariances::Delete_decay_channel_info()
{
	if (VERBOSE > 2) *global_out_stream_ptr << "Deleting memory for decay channel information..." << endl;
	for(int iv = 0; iv < n_v_pts; ++iv)
	{
		for(int izeta = 0; izeta < n_zeta_pts; ++izeta)
		{
			delete [] VEC_n2_Pp[iv][izeta];
			delete [] VEC_n2_alpha[iv][izeta];
			delete [] VEC_n2_Pm[iv][izeta];
			delete [] VEC_n2_alpha_m[iv][izeta];
		}
		delete [] VEC_n2_MT[iv];
		delete [] VEC_n2_PPhi_tilde[iv];
		delete [] VEC_n2_PPhi_tildeFLIP[iv];
		delete [] VEC_n2_PT[iv];
		delete [] VEC_n2_Pp[iv];
		delete [] VEC_n2_alpha[iv];
		delete [] VEC_n2_Pm[iv];
		delete [] VEC_n2_alpha_m[iv];
		delete [] VEC_n2_zeta_factor[iv];
	}
	delete [] VEC_n2_v_factor;
	delete [] VEC_n2_zeta_factor;
	delete [] VEC_n2_P_Y;
	delete [] VEC_n2_MTbar ;
	delete [] VEC_n2_DeltaMT;
	delete [] VEC_n2_MTp;
	delete [] VEC_n2_MTm;
	delete [] VEC_n2_MT;
	delete [] VEC_n2_PPhi_tilde;
	delete [] VEC_n2_PPhi_tildeFLIP;
	delete [] VEC_n2_PT;
	delete [] VEC_n2_Pp;
	delete [] VEC_n2_alpha;
	delete [] VEC_n2_Pm;
	delete [] VEC_n2_alpha_m;

	for(int is = 0; is < n_s_pts; ++is)
	{
		for(int iv = 0; iv < n_v_pts; ++iv)
		{
			for(int izeta = 0; izeta < n_zeta_pts; ++izeta)
			{
				delete [] VEC_Pp[is][iv][izeta];
				delete [] VEC_alpha[is][iv][izeta];
				delete [] VEC_Pm[is][iv][izeta];
				delete [] VEC_alpha_m[is][iv][izeta];
			}
			delete [] VEC_MT[is][iv];
			delete [] VEC_PPhi_tilde[is][iv];
			delete [] VEC_PPhi_tildeFLIP[is][iv];
			delete [] VEC_PT[is][iv];
			delete [] VEC_Pp[is][iv];
			delete [] VEC_alpha[is][iv];
			delete [] VEC_Pm[is][iv];
			delete [] VEC_alpha_m[is][iv];
			delete [] VEC_zeta_factor[is][iv];
		}
		delete [] VEC_v_factor[is];
		delete [] VEC_zeta_factor[is];
		delete [] VEC_P_Y[is];
		delete [] VEC_MTbar[is];
		delete [] VEC_DeltaMT[is];
		delete [] VEC_MTp[is];
		delete [] VEC_MTm[is];
		delete [] VEC_MT[is];
		delete [] VEC_PPhi_tilde[is];
		delete [] VEC_PPhi_tildeFLIP[is];
		delete [] VEC_PT[is];
		delete [] VEC_Pp[is];
		delete [] VEC_alpha[is];
		delete [] VEC_Pm[is];
		delete [] VEC_alpha_m[is];
	}
	delete [] s_pts;
	delete [] s_wts;
	delete [] VEC_pstar;
	delete [] VEC_Estar;
	delete [] VEC_DeltaY;
	delete [] VEC_g_s;
	delete [] VEC_s_factor;
	delete [] VEC_v_factor;
	delete [] VEC_zeta_factor;
	delete [] VEC_Yp;
	delete [] VEC_Ym;
	delete [] VEC_P_Y;
	delete [] VEC_MTbar;
	delete [] VEC_DeltaMT;
	delete [] VEC_MTp;
	delete [] VEC_MTm;
	delete [] VEC_MT;
	delete [] VEC_PPhi_tilde;
	delete [] VEC_PPhi_tildeFLIP;
	delete [] VEC_PT;
	delete [] VEC_Pp;
	delete [] VEC_alpha;
	delete [] VEC_Pm;
	delete [] VEC_alpha_m;
	if (VERBOSE > 2) *global_out_stream_ptr << "Deleted memory for decay channel information." << endl;

	return;
}

void SourceVariances::Set_path(string path_in)
{
	path = path_in;

	return;
}

void SourceVariances::Set_use_delta_f()
{
	use_delta_f = INCLUDE_DELTA_F;
	if (!INCLUDE_DELTA_F)
		no_df_stem = "_no_df";
	return;
}

void SourceVariances::Set_FOsurf_ptr(FO_surf* FOsurf_ptr_in, int FO_length_in)
{
	FOsurf_ptr = FOsurf_ptr_in;
	FO_length = FO_length_in;
	return;
}

void SourceVariances::Get_current_decay_string(int dc_idx, string * decay_string)
{
	// N.B. - dc_idx == 0 is thermal pion(+)s in calling loop, dc_idx > 0 gives resonance decays
	//      ==> need to use dc_idx - 1 here
	*decay_string = decay_channels[dc_idx - 1].resonance_name + " --->> ";
	int temp_monval, tempID;
	for (int decay_part_idx = 0; decay_part_idx < decay_channels[dc_idx - 1].nbody; decay_part_idx++)
	{
		temp_monval = decay_channels[dc_idx - 1].resonance_decay_monvals[decay_part_idx];
		//if (VERBOSE > 0) *global_out_stream_ptr << "Get_current_decay_string(): temp_monval = " << temp_monval << endl;
		if (temp_monval == 0)
			continue;
		else
		{
			tempID = lookup_particle_id_from_monval(all_particles, Nparticle, temp_monval);
			*decay_string += all_particles[tempID].name;
			if (decay_part_idx < decay_channels[dc_idx - 1].nbody - 1) *decay_string += " + ";
		}
	}
	return;
}

int SourceVariances::lookup_resonance_idx_from_particle_id(int pid)
{
	// pid - particle index in all_particles array
	// looks up location in chosen_resonances of given value particle_id
	int result = -1;

	for (int ii = 0; ii < (int)chosen_resonances.size(); ii++)
	{
		if (chosen_resonances[ii] == pid)
		{
			result = ii;
			break;
		}
	}

	// if pid is not one of the chosen_resonances, is not the target daughter (pion(+)), is not stable and has a non-zero effective branching ratio
	if (result < 0 && pid != particle_id && all_particles[pid].stable == 0 && all_particles[pid].effective_branchratio >= 1.e-12)
	{
		*global_out_stream_ptr << " *** lookup_resonance_idx_from_particle_id(): Particle_id = " << pid
					<< " (" << all_particles[pid].name <<") not found in chosen_resonances!" << endl
					<< " *** br = " << all_particles[pid].effective_branchratio << endl;
	}
	return (result);
}

//End of file
