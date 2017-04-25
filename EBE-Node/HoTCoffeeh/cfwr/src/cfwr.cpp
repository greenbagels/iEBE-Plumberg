#include<iostream>
#include<sstream>
#include<string>
#include<fstream>
#include<cmath>
#include<iomanip>
#include<vector>
#include<set>
#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<queue>
#include<map>
#include<cstdlib>
#include<numeric>

#include "cfwr.h"
#include "cfwr_lib.h"
#include "Arsenal.h"
#include "Stopwatch.h"
#include "gauss_quadrature.h"
#include "bessel.h"

using namespace std;

const std::complex<double> i(0, 1);
gsl_cheb_series *cs_accel_expK0re, *cs_accel_expK0im, *cs_accel_expK1re, *cs_accel_expK1im;

// only need to calculated interpolation grid of spacetime moments for each resonance, NOT each decay channel!
bool recycle_previous_moments = false;
bool recycle_similar_moments = false;
int reso_particle_id_of_moments_to_recycle = -1;
int reso_idx_of_moments_to_recycle = -1;
string reso_name_of_moments_to_recycle = "NULL";
string current_decay_channel_string = "NULL";

inline void I(double alpha, double beta, double gamma, complex<double> & I0, complex<double> & I1, complex<double> & I2, complex<double> & I3)
{
	complex<double> ci0, ci1, ck0, ck1, ci0p, ci1p, ck0p, ck1p;
	complex<double> z0 = alpha - i*beta;
	complex<double> z0sq = pow(z0, 2.0);
	double gsq = gamma*gamma;
	complex<double> z = sqrt(z0sq + gsq);
	int errorCode = bessf::cbessik01(z, ci0, ci1, ck0, ck1, ci0p, ci1p, ck0p, ck1p);
	
	I0 = 2.0*ck0;
	I1 = 2.0*z0*ck1 / z;
	I2 = 2.0*z0sq*ck0 / (z*z)
			+ 2.0*(z0sq - gsq)*ck1 / pow(z, 3.0);
	I3 = 2.0*z0*( ( pow(z0, 4.0) - 2.0* z0sq*gsq - 3.0 * pow(gamma, 4.0) ) * ck0 / z
						+ (-6.0*gsq + z0sq*(2.0 + z0sq + gsq)) * ck1
				) / pow(z,5.0);

	return;
}

inline void Iint2(double alpha, double beta, double gamma, double & I0r, double & I1r, double & I2r, double & I3r, double & I0i, double & I1i, double & I2i, double & I3i)
{
	complex<double> z0 = alpha - i*beta;
	complex<double> z0sq = z0*z0;
	double gsq = gamma*gamma;
	complex<double> zsq = z0sq + gsq;
	complex<double> z = sqrt(zsq);
	complex<double> zcu = zsq*z;
	complex<double> zqi = zsq*zcu;
	double ea = exp(-alpha);

	complex<double> ck0(	ea * gsl_cheb_eval (cs_accel_expK0re, alpha),
							ea * gsl_cheb_eval (cs_accel_expK0im, alpha) );
	complex<double> ck1(	ea * gsl_cheb_eval (cs_accel_expK1re, alpha),
							ea * gsl_cheb_eval (cs_accel_expK1im, alpha) );

	complex<double> I0 = 2.0*ck0;
	complex<double> I1 = 2.0*z0*ck1 / z;
	complex<double> I2 = 2.0*z0sq*ck0 / zsq
			+ 2.0*(z0sq - gsq)*ck1 / zcu;
	complex<double> I3 = 2.0*z0*( ( z0sq*z0sq - 2.0* z0sq*gsq - 3.0 * gsq*gsq ) * ck0 / z
						+ (-6.0*gsq + z0sq*(2.0 + z0sq + gsq)) * ck1
				) / zqi;

	I0r = I0.real();
	I1r = I1.real();
	I2r = I2.real();
	I3r = I3.real();
	I0i = I0.imag();
	I1i = I1.imag();
	I2i = I2.imag();
	I3i = I3.imag();

	return;
}

double CorrelationFunction::place_in_range(double phi, double min, double max)
{
	while (phi < min || phi > max)
	{
		if (phi < min) phi += twopi;
		else phi -= twopi;
	}

	return (phi);
}

// ************************************************************
// Compute correlation function at all specified q points for all resonances here
// ************************************************************
void CorrelationFunction::Fourier_transform_emission_function(int iqt, int iqz)
{
	Stopwatch BIGsw;
	global_plane_psi = 0.0;	//for now

	int decay_channel_loop_cutoff = n_decay_channels;			//loop over direct pions and decay_channels
	current_iqt = iqt;
	current_iqz = iqz;
	double loc_qz = qz_pts[iqz];
	double loc_qt = qt_pts[iqt];
	current_pY_shift = - double(abs(loc_qz)>1.e-10) * asinh(loc_qz / sqrt(abs(loc_qt*loc_qt-loc_qz*loc_qz) + 1.e-100));

	///////
	*global_out_stream_ptr << "Initializing HDF files...";
	{
		int HDFInitializationSuccess = Administrate_resonance_HDF_array(0);
		HDFInitializationSuccess = Administrate_target_thermal_HDF_array(0);
		Set_all_Bessel_grids(iqt, iqz);
	}
	*global_out_stream_ptr << "done." << endl << endl;
	///////
	
	*global_out_stream_ptr << "Setting spacetime moments grid..." << endl;
	BIGsw.Start();

	// loop over decay_channels (idc == 0 corresponds to thermal pions)
	for (int idc = 0; idc <= decay_channel_loop_cutoff; ++idc)
	{
		// check whether to do this decay channel
		if (idc > 0 && thermal_pions_only)
			break;
		else if (!Do_this_decay_channel(idc))
			continue;
	
		Set_current_particle_info(idc);

		Get_spacetime_moments(idc, iqt, iqz);
	}

	BIGsw.Stop();
	*global_out_stream_ptr << "\t ...finished all (thermal) space-time moments for loop (iqt = " << iqt << ", iqz = " << iqz << ") in " << BIGsw.printTime() << " seconds." << endl;
	
	//only need to calculate spectra, etc. once
	// Now dump all thermal spectra before continuing with resonance decay calculations
	if ( iqt == 0 && iqz == 0 )
	{
		Dump_spectra_array("thermal_spectra.dat", thermal_spectra);
		Dump_spectra_array("full_spectra.dat", spectra);
		//set logs and signs!
		for (int ipid = 0; ipid < Nparticle; ++ipid)
			Set_spectra_logs_and_signs(ipid);
	}

	///////
	*global_out_stream_ptr << "Cleaning up HDF files...";
	{
		//get thermal target moments here
		Access_resonance_in_HDF_array(target_particle_id, iqt, iqz, 1, thermal_target_dN_dypTdpTdphi_moments);
		//make sure they are written to separate file here
		Access_target_thermal_in_HDF_array(iqt, iqz, 0, thermal_target_dN_dypTdpTdphi_moments);

		//save thermal moments (without resonance decay feeddown) separately
		//int closeHDFresonanceSpectra = Administrate_resonance_HDF_array(2);
		int closeHDFresonanceSpectra = Administrate_target_thermal_HDF_array(2);
	}
	*global_out_stream_ptr << "done." << endl << endl;
	///////

   return;
}

void CorrelationFunction::Compute_phase_space_integrals(int iqt, int iqz)
{
	if (thermal_pions_only)
	{
		*global_out_stream_ptr << "Thermal pions only: no phase-space integrals need to be computed." << endl;
		return;
	}

	Stopwatch BIGsw;
	int decay_channel_loop_cutoff = n_decay_channels;			//loop over direct pions and decay_channels

	//set needed q-points
	Set_qlist(iqt, iqz);

	*global_out_stream_ptr << "Computing all phase-space integrals..." << endl;
	BIGsw.Start();
	
	// ************************************************************
	// Compute feeddown with heaviest resonances first
	// ************************************************************
	for (int idc = 1; idc <= decay_channel_loop_cutoff; ++idc)
	{
		// ************************************************************
		// check whether to do this decay channel
		// ************************************************************
		if (decay_channels[idc-1].resonance_particle_id == target_particle_id || thermal_pions_only)
			break;
		else if (!Do_this_decay_channel(idc))
			continue;
	
		// ************************************************************
		// if so, set decay channel info
		// ************************************************************
		Set_current_particle_info(idc);
		Allocate_decay_channel_info();				// allocate needed memory

		// ************************************************************
		// begin resonance decay calculations here...
		// ************************************************************

		Load_resonance_and_daughter_spectra(decay_channels[idc-1].resonance_particle_id, iqt, iqz);

		for (int idc_DI = 0; idc_DI < current_reso_nbody; ++idc_DI)
		{
			int daughter_resonance_particle_id = -1;
			if (!Do_this_daughter_particle(idc, idc_DI, &daughter_resonance_particle_id))
				continue;

			Set_current_daughter_info(idc, idc_DI);

			Do_resonance_integrals(current_resonance_particle_id, daughter_resonance_particle_id, idc, iqt, iqz);
		}
		Update_daughter_spectra(decay_channels[idc-1].resonance_particle_id, iqt, iqz);

		Delete_decay_channel_info();				// free up memory
	}											// END of decay channel loop
	BIGsw.Stop();
	*global_out_stream_ptr << "\t ...finished computing all phase-space integrals for loop (iqt = "
							<< iqt << ", iqz = " << iqz << ") in " << BIGsw.printTime() << " seconds." << endl;

	if (iqt == 0 && iqz == 0)
		Dump_spectra_array("full_spectra.dat", spectra);

	return;
}
//////////////////////////////////////////////////////////////////////////////////
// End of main routines for setting up computation of correlation function

/*
//this function is only called in cfwr.GFroutines.cpp
void CorrelationFunction::Set_thermal_target_moments()
{
	//load just thermal spectra (not just target)
	Load_spectra_array("thermal_spectra.dat", thermal_spectra);

	//load target thermal moments from HDF5 file
	int HDFOpenSuccess = Open_target_thermal_HDF_array();
	if (HDFOpenSuccess < 0)
	{
		cerr << "Failed to open resonance_thermal_moments.h5!  Exiting..." << endl;
		exit(1);
	}

	//everything currently in resonance*h5 file
	for (int iqt = 0; iqt < qtnpts; ++iqt)
	for (int iqz = 0; iqz < qznpts; ++iqz)
	{
		Get_target_thermal_in_HDF_array(iqt, iqz, thermal_target_dN_dypTdpTdphi_moments);

		for (int iqx = 0; iqx < qxnpts; ++iqx)
		for (int iqy = 0; iqy < qynpts; ++iqy)
		for (int ipT = 0; ipT < n_pT_pts; ++ipT)
		for (int ipphi = 0; ipphi < n_pphi_pts; ++ipphi)
		for (int itrig = 0; itrig < ntrig; ++itrig)
			 thermal_target_Yeq0_moments = fixQTQZ_indexer(ipT, ipphi, (n_pY_pts - 1) / 2, iqx, iqy, itrig);
	}
	int getHDFresonanceSpectra = Get_target_thermal_from_HDF_array((n_pY_pts - 1) / 2, thermal_target_dN_dypTdpTdphi_moments);
	if (getHDFresonanceSpectra < 0)
	{
		cerr << "Failed to get this resonance from HDF array!  Exiting..." << endl;
		exit;
	}

	int HDFCloseSuccess = Close_target_thermal_HDF_array();
	if (HDFCloseSuccess < 0)
	{
		cerr << "Failed to close HDF array of resonances (resonance_spectra.h5)!  Exiting..." << endl;
		exit(1);
	}

	return;
}

//this function is only called in cfwr.GFroutines.cpp and cfwr_IO.cpp
void CorrelationFunction::Set_full_target_moments()
{
	//load just spectra with resonances (not just target)
	Load_spectra_array("full_spectra.dat", spectra);

	//load full target moments with resonances from HDF5 file
	int HDFOpenSuccess = Open_resonance_HDF_array("resonance_spectra.h5");
	if (HDFOpenSuccess < 0)
	{
		cerr << "Failed to open HDF array of resonances (resonance_spectra.h5)!  Exiting..." << endl;
		exit(1);
	}

	int getHDFresonanceSpectra = Get_resonance_from_HDF_array(target_particle_id, (n_pY_pts - 1) / 2, current_dN_dypTdpTdphi_moments);
	if (getHDFresonanceSpectra < 0)
	{
		cerr << "Failed to get this resonance from HDF array!  Exiting..." << endl;
		exit;
	}

	int HDFCloseSuccess = Close_resonance_HDF_array();
	if (HDFCloseSuccess < 0)
	{
		cerr << "Failed to close HDF array of resonances (resonance_spectra.h5)!  Exiting..." << endl;
		exit(1);
	}

	return;
}
*/

bool CorrelationFunction::Do_this_decay_channel(int dc_idx)
{
	string local_name = "Thermal pion(+)";
	if (dc_idx == 0)
	{
		if (VERBOSE > 0) *global_out_stream_ptr << endl << local_name << ": doing this one." << endl;
		return true;
	}
	else
	{
		local_name = decay_channels[dc_idx-1].resonance_name;
		Get_current_decay_string(dc_idx, &current_decay_channel_string);
	}
	bool tmp_bool = decay_channels[dc_idx-1].include_channel;
	if (!tmp_bool && VERBOSE > 0) *global_out_stream_ptr << endl << local_name << ": skipping decay " << current_decay_channel_string << "." << endl;

	return (tmp_bool);
}

// ************************************************************
// Checks whether to do daughter particle for any given decay channel
// ************************************************************
bool CorrelationFunction::Do_this_daughter_particle(int dc_idx, int daughter_idx, int * daughter_resonance_pid)
{
	// assume dc_idx > 0
	string local_name = decay_channels[dc_idx-1].resonance_name;

	// look up daughter particle info
	int temp_monval = decay_channels[dc_idx-1].resonance_decay_monvals[daughter_idx];

	if (temp_monval == 0)
		return false;

	int temp_ID = lookup_particle_id_from_monval(all_particles, Nparticle, temp_monval);
	//*daughter_resonance_idx = lookup_resonance_idx_from_particle_id(temp_ID) + 1;
	*daughter_resonance_pid = temp_ID;
	// if daughter was found in chosen_resonances or is pion(+), this is correct
	particle_info temp_daughter = all_particles[temp_ID];

	if (*daughter_resonance_pid < 0 && temp_daughter.monval != particle_monval && temp_daughter.effective_branchratio >= 1.e-12)
		*global_out_stream_ptr << "Couldn't find " << temp_daughter.name << " in chosen_resonances!  Results are probably not reliable..." << endl;

	//bool daughter_does_not_contribute = ( (temp_daughter.stable == 1 || temp_daughter.effective_branchratio < 1.e-12) && temp_daughter.monval != particle_monval );
	bool daughter_does_not_contribute = ( (temp_daughter.decays_Npart[0] == 1 || temp_daughter.effective_branchratio < 1.e-12) && temp_daughter.monval != particle_monval );

	// if daughter particle gives no contribution to final pion spectra
	if (daughter_does_not_contribute)
	{
		if (VERBOSE > 0) *global_out_stream_ptr << "\t * " << local_name << ": in decay " << current_decay_channel_string << ", skipping " << temp_daughter.name
												<< " (daughter_resonance_pid = " << *daughter_resonance_pid << ")." << endl;
		return false;
	}
	else
	{
		if (VERBOSE > 0) *global_out_stream_ptr << "\t * " << local_name << ": in decay " << current_decay_channel_string << ", doing " << temp_daughter.name
												<< " (daughter_resonance_pid = " << *daughter_resonance_pid << ")." << endl;
		return true;
	}
}

void CorrelationFunction::Set_current_particle_info(int dc_idx)
{
	if (dc_idx == 0)
	{
		muRES = particle_mu;
		signRES = particle_sign;
		gRES = particle_gspin;
		current_resonance_particle_id = target_particle_id;
		
		return;
	}
	else
	{
		// assume dc_idx > 0
		string local_name = decay_channels[dc_idx-1].resonance_name;

		if (VERBOSE > 0) *global_out_stream_ptr << endl << local_name << ": doing decay " << current_decay_channel_string << "." << endl
			<< "\t * " << local_name << ": setting information for this decay channel..." << endl;

		if (dc_idx > 1)
		{
			//cerr << "Setting previous decay channel information for dc_idx = " << dc_idx << endl;
			previous_resonance_particle_id = current_resonance_particle_id;		//for look-up in all_particles
			previous_decay_channel_idx = current_decay_channel_idx;			//different for each decay channel
			previous_resonance_idx = current_resonance_idx;				//different for each decay channel
			previous_resonance_mass = current_resonance_mass;
			previous_resonance_Gamma = current_resonance_Gamma;
			previous_resonance_total_br = current_resonance_total_br;
			previous_resonance_direct_br = current_resonance_direct_br;
			previous_reso_nbody = current_reso_nbody;
		}
		//cerr << "Setting current decay channel information for dc_idx = " << dc_idx << endl;
		current_decay_channel_idx = dc_idx;
		current_resonance_idx = decay_channels[dc_idx-1].resonance_idx;
		current_resonance_particle_id = decay_channels[dc_idx-1].resonance_particle_id;
		current_resonance_mass = decay_channels[dc_idx-1].resonance_mass;
		current_resonance_Gamma = decay_channels[dc_idx-1].resonance_Gamma;
		current_resonance_total_br = decay_channels[dc_idx-1].resonance_total_br;
		current_resonance_direct_br = decay_channels[dc_idx-1].resonance_direct_br;
		current_reso_nbody = decay_channels[dc_idx-1].nbody;
		
		// might want to rename these for notational consistency...
		muRES = decay_channels[dc_idx-1].resonance_mu;
		signRES = decay_channels[dc_idx-1].resonance_sign;
		gRES = decay_channels[dc_idx-1].resonance_gspin;
		
		if (dc_idx > 1)
		{
			int similar_particle_idx = -1;
			int temp_reso_idx = decay_channels[dc_idx-1].resonance_idx;

			if ( current_resonance_particle_id == previous_resonance_particle_id )
			{
				//previous resonance is the same as this one...
				recycle_previous_moments = true;
				recycle_similar_moments = false;
				if (VERBOSE > 0) *global_out_stream_ptr << "\t * " << decay_channels[dc_idx-1].resonance_name << " (same as the last one)." << endl;
			}
			else if ( Search_for_similar_particle( temp_reso_idx, &similar_particle_idx ) )
			{
				//previous resonance is NOT the same as this one BUT this one is sufficiently similar to some preceding one...
				recycle_previous_moments = false;
				recycle_similar_moments = true;
				reso_particle_id_of_moments_to_recycle = chosen_resonances[similar_particle_idx];
				reso_idx_of_moments_to_recycle = similar_particle_idx;
				if (VERBOSE > 0) *global_out_stream_ptr << "\t * " << decay_channels[dc_idx-1].resonance_name << " (different from the last one, but close enough to "
														<< all_particles[reso_particle_id_of_moments_to_recycle].name << ")." << endl;
			}
			else
			{
				recycle_previous_moments = false;
				recycle_similar_moments = false;
				reso_particle_id_of_moments_to_recycle = -1;	//guarantees it won't be used spuriously
				reso_idx_of_moments_to_recycle = -1;
				if (VERBOSE > 0) *global_out_stream_ptr << "\t * " << decay_channels[dc_idx-1].resonance_name << " (different from the last one --> calculating afresh)." << endl;
			}
		}
	}
	
	return;
}

void CorrelationFunction::Set_current_daughter_info(int dc_idx, int daughter_idx)
{
	if (dc_idx > 1)
	{
		previous_resonance_particle_id = current_resonance_particle_id;		//for look-up in all_particles
		previous_decay_channel_idx = current_decay_channel_idx;			//different for each decay channel
		previous_resonance_idx = current_resonance_idx;
		previous_resonance_mass = current_resonance_mass;
		previous_resonance_Gamma = current_resonance_Gamma;
		previous_m2_Gamma = current_m2_Gamma;
		previous_m3_Gamma = current_m3_Gamma;
		previous_resonance_total_br = current_resonance_total_br;
		previous_resonance_direct_br = current_resonance_direct_br;
		previous_reso_nbody = current_reso_nbody;
		previous_daughter_mass = current_daughter_mass;
		previous_daughter_Gamma = current_daughter_Gamma;
	}
	current_decay_channel_idx = dc_idx;
	current_resonance_idx = decay_channels[dc_idx-1].resonance_idx;
	current_resonance_particle_id = decay_channels[dc_idx-1].resonance_particle_id;
	current_resonance_mass = decay_channels[dc_idx-1].resonance_mass;
	current_resonance_Gamma = decay_channels[dc_idx-1].resonance_Gamma;
	current_resonance_total_br = decay_channels[dc_idx-1].resonance_total_br;
	current_resonance_direct_br = decay_channels[dc_idx-1].resonance_direct_br;
	current_reso_nbody = decay_channels[dc_idx-1].nbody;
	current_daughter_mass = decay_channels[dc_idx-1].resonance_decay_masses[daughter_idx];
	current_daughter_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[daughter_idx];

	// might want to rename these for notational consistency...
	muRES = decay_channels[dc_idx-1].resonance_mu;
	signRES = decay_channels[dc_idx-1].resonance_sign;
	gRES = decay_channels[dc_idx-1].resonance_gspin;

	// set non-daughter decay masses for computing contributions to spectra of daughter
	double m2ex = 0.0, m3ex = 0.0, m4ex = 0.0;
	switch(current_reso_nbody)
	{
		case 1:
			break;
		case 2:
			current_resonance_decay_masses[1] = 0.0;
			current_m3_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[0];
			if (daughter_idx == 0)
			{
				current_resonance_decay_masses[0] = decay_channels[dc_idx-1].resonance_decay_masses[1];
				current_m2_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[1];
			}
			else
			{
				current_resonance_decay_masses[0] = decay_channels[dc_idx-1].resonance_decay_masses[0];
				current_m2_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[0];
			}
			break;
		case 3:
			if (daughter_idx == 0)
			{
				current_resonance_decay_masses[0] = decay_channels[dc_idx-1].resonance_decay_masses[1];
				current_resonance_decay_masses[1] = decay_channels[dc_idx-1].resonance_decay_masses[2];
				current_m2_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[1];
				current_m3_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[2];
			}
			else if (daughter_idx == 1)
			{
				current_resonance_decay_masses[0] = decay_channels[dc_idx-1].resonance_decay_masses[0];
				current_resonance_decay_masses[1] = decay_channels[dc_idx-1].resonance_decay_masses[2];
				current_m2_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[0];
				current_m3_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[2];
			}
			else
			{
				current_resonance_decay_masses[0] = decay_channels[dc_idx-1].resonance_decay_masses[0];
				current_resonance_decay_masses[1] = decay_channels[dc_idx-1].resonance_decay_masses[1];
				current_m2_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[0];
				current_m3_Gamma = decay_channels[dc_idx-1].resonance_decay_Gammas[1];
			}
			break;
		case 4:
			if (daughter_idx == 0)
			{
				m2ex = decay_channels[dc_idx-1].resonance_decay_masses[1];
				m3ex = decay_channels[dc_idx-1].resonance_decay_masses[2];
				m4ex = decay_channels[dc_idx-1].resonance_decay_masses[3];
			}
			else if (daughter_idx == 1)
			{
				m2ex = decay_channels[dc_idx-1].resonance_decay_masses[0];
				m3ex = decay_channels[dc_idx-1].resonance_decay_masses[2];
				m4ex = decay_channels[dc_idx-1].resonance_decay_masses[3];
			}
			else if (daughter_idx == 2)
			{
				m2ex = decay_channels[dc_idx-1].resonance_decay_masses[0];
				m3ex = decay_channels[dc_idx-1].resonance_decay_masses[1];
				m4ex = decay_channels[dc_idx-1].resonance_decay_masses[3];
			}
			else
			{
				m2ex = decay_channels[dc_idx-1].resonance_decay_masses[0];
				m3ex = decay_channels[dc_idx-1].resonance_decay_masses[1];
				m4ex = decay_channels[dc_idx-1].resonance_decay_masses[2];
			}
			current_resonance_decay_masses[0] = m2ex;
			current_resonance_decay_masses[1] = 0.5 * (m3ex + m4ex + current_resonance_mass - current_daughter_mass - m2ex);
			break;
		default:
			cerr << "Set_current_daughter_info(): shouldn't have ended up here, bad value of current_reso_nbody!" << endl;
			exit(1);
	}
}

bool CorrelationFunction::Search_for_similar_particle(int reso_idx, int * result)
{
	// for the timebeing, just search from beginning of decay_channels until similar particle is found;
	// should be more careful, since could lead to small numerical discrepancies if similar particle was
	// already recycled by some other (dissimilar) particle, but ignore this complication for now...
	*result = -1;
	
	for (int local_ir = 0; local_ir < reso_idx; ++local_ir)
	{// only need to search decay_channels that have already been calculated
		if (particles_are_the_same(local_ir, reso_idx))
		{
			*result = local_ir;
			break;
		}
	}
	
	return (*result >= 0);
}

//**********************************************************************************************
bool CorrelationFunction::particles_are_the_same(int reso_idx1, int reso_idx2)
{
	int icr1 = chosen_resonances[reso_idx1];
	int icr2 = chosen_resonances[reso_idx2];
	//int icr1 = reso_idx1;
	//int icr2 = reso_idx2;
	if (all_particles[icr1].sign != all_particles[icr2].sign)
		return false;
	if (abs(all_particles[icr1].mass-all_particles[icr2].mass) / (all_particles[icr2].mass+1.e-30) > PARTICLE_DIFF_TOLERANCE)
		return false;
	//assume chemical potential mu is constant over entire FO surface
	double chem1 = all_particles[icr1].mu, chem2 = all_particles[icr2].mu;
	if (2.*abs(chem1 - chem2)/(chem1 + chem2 + 1.e-30) > PARTICLE_DIFF_TOLERANCE)
		return false;

	return true;
}

void CorrelationFunction::Recycle_spacetime_moments()
{
*global_out_stream_ptr << "PIDs: " << current_resonance_particle_id << "   " << reso_particle_id_of_moments_to_recycle << endl;
//*global_out_stream_ptr << "PIDs: " << current_resonance_idx << "   " << reso_idx_of_moments_to_recycle << endl;
	int HDFcopyChunkSuccess = Copy_chunk(current_resonance_particle_id, reso_particle_id_of_moments_to_recycle);
	//int HDFcopyChunkSuccess = Copy_chunk(current_resonance_idx, reso_idx_of_moments_to_recycle);
if (HDFcopyChunkSuccess < 0) exit(1);

	for (int ipT = 0; ipT < n_pT_pts; ++ipT)
	for (int ipphi = 0; ipphi < n_pphi_pts; ++ipphi)
	{
		spectra[current_resonance_particle_id][ipT][ipphi] = spectra[reso_particle_id_of_moments_to_recycle][ipT][ipphi];
		thermal_spectra[current_resonance_particle_id][ipT][ipphi] = thermal_spectra[reso_particle_id_of_moments_to_recycle][ipT][ipphi];
	}

	return;
}

//**************************************************************
//**************************************************************
void CorrelationFunction::Load_resonance_and_daughter_spectra(int local_pid, int iqt, int iqz)
{
	// get parent resonance spectra, set logs and signs arrays that are needed for interpolation
	int getHDFresonanceSpectra = Access_resonance_in_HDF_array(local_pid, iqt, iqz, 1, current_dN_dypTdpTdphi_moments);
	Set_current_resonance_logs_and_signs();

	// get spectra for all daughters, set all of the logs and signs arrays that are needed for interpolation
	int n_daughters = Set_daughter_list(local_pid);

	if (n_daughters > 0)
	{
		int d_idx = 0;

		Setup_current_daughters_dN_dypTdpTdphi_moments(n_daughters);

		for (set<int>::iterator it = daughter_resonance_indices.begin(); it != daughter_resonance_indices.end(); ++it)
		{
			int daughter_pid = *it;		//daughter pid is pointed to by iterator
			getHDFresonanceSpectra = Access_resonance_in_HDF_array(daughter_pid, iqt, iqz, 1, current_daughters_dN_dypTdpTdphi_moments[d_idx]);
			++d_idx;
		}
	
		Set_current_daughters_resonance_logs_and_signs(n_daughters);
	}
	else
	{
		cerr << "Particle is stable, shouldn't have ended up here!  Something went wrong..." << endl;
		exit(1);
	}

	return;
}

void CorrelationFunction::Update_daughter_spectra(int local_pid, int iqt, int iqz)
{
	int d_idx = 0;
	for (set<int>::iterator it = daughter_resonance_indices.begin(); it != daughter_resonance_indices.end(); ++it)
	{
		int daughter_pid = *it;		//daughter pid is pointed to by iterator
		int setHDFresonanceSpectra = Access_resonance_in_HDF_array(daughter_pid, iqt, iqz, 0, current_daughters_dN_dypTdpTdphi_moments[d_idx]);

		++d_idx;
	}

	// cleanup previous iteration and setup the new one
	Cleanup_current_daughters_dN_dypTdpTdphi_moments(daughter_resonance_indices.size());

	return;
}

void CorrelationFunction::Set_spectra_logs_and_signs(int local_pid)
{
	for (int ipT = 0; ipT < n_pT_pts; ++ipT)
	for (int ipphi = 0; ipphi < n_pphi_pts; ++ipphi)
	{
		log_spectra[local_pid][ipT][ipphi] = log(abs(spectra[local_pid][ipT][ipphi])+1.e-100);
		sign_spectra[local_pid][ipT][ipphi] = sgn(spectra[local_pid][ipT][ipphi]);
	}

	return;
}

void CorrelationFunction::Set_current_resonance_logs_and_signs()
{
	for (int ipT = 0; ipT < n_pT_pts; ++ipT)
	for (int ipphi = 0; ipphi < n_pphi_pts; ++ipphi)
	for (int ipY = 0; ipY < n_pY_pts; ++ipY)
	for (int iqx = 0; iqx < qxnpts; ++iqx)
	for (int iqy = 0; iqy < qynpts; ++iqy)
	for (int itrig = 0; itrig < 2; ++itrig)
	{
		double temp = current_dN_dypTdpTdphi_moments[fixQTQZ_indexer(ipT,ipphi,ipY,iqx,iqy,itrig)];
		current_ln_dN_dypTdpTdphi_moments[fixQTQZ_indexer(ipT,ipphi,ipY,iqx,iqy,itrig)] = log(abs(temp)+1.e-100);
		current_sign_of_dN_dypTdpTdphi_moments[fixQTQZ_indexer(ipT,ipphi,ipY,iqx,iqy,itrig)] = sgn(temp);
	}

	return;
}

void CorrelationFunction::Set_current_daughters_resonance_logs_and_signs(int n_daughters)
{
	for (int idaughter = 0; idaughter < n_daughters; ++idaughter)
	for (int ipT = 0; ipT < n_pT_pts; ++ipT)
	for (int ipphi = 0; ipphi < n_pphi_pts; ++ipphi)
	for (int ipY = 0; ipY < n_pY_pts; ++ipY)
	for (int iqx = 0; iqx < qxnpts; ++iqx)
	for (int iqy = 0; iqy < qynpts; ++iqy)
	for (int itrig = 0; itrig < 2; ++itrig)
	{
		double temp = current_daughters_dN_dypTdpTdphi_moments[idaughter][fixQTQZ_indexer(ipT,ipphi,ipY,iqx,iqy,itrig)];
		current_daughters_ln_dN_dypTdpTdphi_moments[idaughter][fixQTQZ_indexer(ipT,ipphi,ipY,iqx,iqy,itrig)] = log(abs(temp)+1.e-100);
		current_daughters_sign_of_dN_dypTdpTdphi_moments[idaughter][fixQTQZ_indexer(ipT,ipphi,ipY,iqx,iqy,itrig)] = sgn(temp);
	}

	return;
}
//**************************************************************
//**************************************************************

void CorrelationFunction::Get_spacetime_moments(int dc_idx, int iqt, int iqz)
{
//**************************************************************
//Set resonance name
//**************************************************************
	string local_name = "Thermal pion(+)";
	if (dc_idx > 0)
		local_name = decay_channels[dc_idx-1].resonance_name;
//**************************************************************
//Decide what to do with this resonance / decay channel
//**************************************************************
	if (recycle_previous_moments && dc_idx > 1)	// same as earlier resonance
	{
		if (VERBOSE > 0) *global_out_stream_ptr << local_name
			<< ": new parent resonance (" << decay_channels[current_decay_channel_idx-1].resonance_name << ", dc_idx = " << current_decay_channel_idx
			<< " of " << n_decay_channels << ") same as preceding parent resonance \n\t\t--> reusing old dN_dypTdpTdphi_moments!" << endl;
	}
	else if (recycle_similar_moments && dc_idx > 1)	// sufficiently similar (but different) earlier resonance
	{
		if (VERBOSE > 0) *global_out_stream_ptr << local_name
			<< ": new parent resonance (" << decay_channels[current_decay_channel_idx-1].resonance_name << ", dc_idx = " << current_decay_channel_idx
			<< " of " << n_decay_channels << ") sufficiently close to preceding parent resonance (" << all_particles[reso_particle_id_of_moments_to_recycle].name
			<< ", reso_particle_id = " << reso_particle_id_of_moments_to_recycle << ") \n\t\t--> reusing old dN_dypTdpTdphi_moments!" << endl;
		Recycle_spacetime_moments();
	}
	else
	{
		if (dc_idx == 0)	//if it's thermal pions
		{
			if (VERBOSE > 0) *global_out_stream_ptr << "  --> Computing dN_dypTdpTdphi_moments for thermal pion(+)!" << endl;
		}
		else if (dc_idx == 1)	//if it's the first resonance
		{
			if (VERBOSE > 0) *global_out_stream_ptr << "  --> Computing dN_dypTdpTdphi_moments for " << local_name << endl;
		}
		else			//if it's a later resonance
		{
			if (VERBOSE > 0 && !recycle_previous_moments && !recycle_similar_moments) *global_out_stream_ptr << local_name
				<< ": new parent resonance (" << decay_channels[current_decay_channel_idx-1].resonance_name << ", dc_idx = " << current_decay_channel_idx
				<< " of " << n_decay_channels << ") dissimilar from all preceding decay_channels \n\t\t--> calculating new dN_dypTdpTdphi_moments!" << endl;
			else
			{
				cerr << "You shouldn't have ended up here!" << endl;
				exit(1);
			}
		}

		//allows to omit thermal spectra calculations from specified resonances, e.g., all resonances which contribute up to 60% of decay pions
		if (find(osr.begin(), osr.end(), current_resonance_particle_id) != osr.end())
			*global_out_stream_ptr << "  --> ACTUALLY SKIPPING WEIGHTED THERMAL SPECTRA FOR " << local_name << endl;
		else
		{
			*global_out_stream_ptr << "  --> ACTUALLY DOING WEIGHTED THERMAL SPECTRA FOR " << local_name << endl;
			Set_dN_dypTdpTdphi_moments(current_resonance_particle_id, iqt, iqz);
		}
	}
//**************************************************************
//Spacetime moments now set
//**************************************************************
	return;
}

void CorrelationFunction::Set_dN_dypTdpTdphi_moments(int local_pid, int iqt, int iqz)
{
	double localmass = all_particles[local_pid].mass;
	string local_name = "Thermal pion(+)";
	if (local_pid != target_particle_id)
		local_name = all_particles[local_pid].name;
	
	// get spectra at each fluid cell, sort by importance
	Stopwatch sw, sw_qtqzpY;
	sw.Start();
	if (iqt == 0 && iqz == 0)
	{
		*global_out_stream_ptr << "Computing un-weighted thermal spectra..." << endl;
		Cal_dN_dypTdpTdphi_no_weights(local_pid);
	}

	// get weighted spectra with only most important fluid cells, up to given threshhold
	*global_out_stream_ptr << "Computing weighted thermal spectra..." << endl;

	//prepare for reading...
	int HDFcode = Administrate_besselcoeffs_HDF_array(1);	//open
	double * BC_chunk = new double [4 * FO_length * (n_alpha_points + 1)];
	cs_accel_expK0re = gsl_cheb_alloc (n_alpha_points);
	cs_accel_expK0im = gsl_cheb_alloc (n_alpha_points);
	cs_accel_expK1re = gsl_cheb_alloc (n_alpha_points);
	cs_accel_expK1im = gsl_cheb_alloc (n_alpha_points);

	///////////////////////////////////
	// Loop over pY points
	///////////////////////////////////
	//if (iqz > 0) exit(8);
	for (int ipY = 0; ipY < n_pY_pts; ++ipY)
	{
		sw_qtqzpY.Reset();
		sw_qtqzpY.Start();
		ch_SP_pY[ipY] = cosh(SP_Del_pY[ipY] + current_pY_shift);
		sh_SP_pY[ipY] = sinh(SP_Del_pY[ipY] + current_pY_shift);

		//load appropriate Bessel coefficients
		HDFcode = Access_besselcoeffs_in_HDF_array(ipY, 1, BC_chunk);
		//do the calculations
		Cal_dN_dypTdpTdphi_with_weights(local_pid, ipY, iqt, iqz, BC_chunk);
		sw_qtqzpY.Stop();
		if (VERBOSE > 1) *global_out_stream_ptr << "Finished loop with ( iqt, iqz, ipY ) = ( " << iqt << ", " << iqz << ", " << ipY << " ) in " << sw_qtqzpY.printTime() << " seconds." << endl;
	}
//if (1) exit (8);

	{
		// store in HDF5 file
		int setHDFresonanceSpectra = Access_resonance_in_HDF_array(local_pid, iqt, iqz, 0, current_dN_dypTdpTdphi_moments);
		if (setHDFresonanceSpectra < 0)
		{
			cerr << "Failed to set this resonance in HDF array!  Exiting..." << endl;
			exit(1);
		}

		HDFcode = Administrate_besselcoeffs_HDF_array(2);
	}

	sw.Stop();
	*global_out_stream_ptr << "Took " << sw.printTime() << " seconds to set dN/dypTdpTdphi moments." << endl;

	delete [] BC_chunk;

	return;
}

void CorrelationFunction::Cal_dN_dypTdpTdphi_no_weights(int local_pid)
{
	// set particle information
	double sign = all_particles[local_pid].sign;
	double degen = all_particles[local_pid].gspin;
	double localmass = all_particles[local_pid].mass;
	double mu = all_particles[local_pid].mu;

	// set some freeze-out surface information that's constant the whole time
	double prefactor = 1.0*degen/(8.0*M_PI*M_PI*M_PI)/(hbarC*hbarC*hbarC);
	double loc_Tdec = (&FOsurf_ptr[0])->Tdec;
	double loc_Pdec = (&FOsurf_ptr[0])->Pdec;
	double loc_Edec = (&FOsurf_ptr[0])->Edec;
	double one_by_Tdec = 1./loc_Tdec;

	double deltaf_prefactor = 0.;
	if (use_delta_f)
		deltaf_prefactor = 1./(2.0*loc_Tdec*loc_Tdec*(loc_Edec+loc_Pdec));

	double eta_s_symmetry_factor = 2.0;

	Stopwatch sw_ThermalResonanceSpectra;

	//Time full calculation of thermal spectra for this resonance
	sw_ThermalResonanceSpectra.Start();

	//////////////////////////////////////////////////
	// Organize calculation of boost-invariant spectra
	// by looping over pT and pphi
	//////////////////////////////////////////////////
	for (int ipT = 0; ipT < n_pT_pts; ++ipT)
	for (int ipphi = 0; ipphi < n_pphi_pts; ++ipphi)
	{
		double loc_pT = SP_pT[ipT];
		int pTpphi_index = ipT * n_pphi_pts + ipphi;
		double sin_pphi = sin_SP_pphi[ipphi];
		double cos_pphi = cos_SP_pphi[ipphi];

		double px = loc_pT*cos_pphi;
		double py = loc_pT*sin_pphi;
		double loc_mT = sqrt(loc_pT*loc_pT+localmass*localmass);

		double spectra_at_pTpphi = 0.0;

		////////////////////////////////////////////////
		// Loop over freeze-out surface fluid cells
		////////////////////////////////////////////////
		for (int isurf = 0; isurf < FO_length; ++isurf)
		{
			//////////////////////////////////
			//////////////////////////////////
			FO_surf * surf = &FOsurf_ptr[isurf];

			double tau = surf->tau;

			double vx = surf->vx;
			double vy = surf->vy;
			double gammaT = surf->gammaT;

			double da0 = surf->da0;
			double da1 = surf->da1;
			double da2 = surf->da2;

			double pi00 = surf->pi00;
			double pi01 = surf->pi01;
			double pi02 = surf->pi02;
			double pi11 = surf->pi11;
			double pi12 = surf->pi12;
			double pi22 = surf->pi22;
			double pi33 = surf->pi33;

			double A = tau*prefactor*loc_mT*da0;
			double B = tau*prefactor*(px*da1 + py*da2);
			double C = deltaf_prefactor;

			double a = loc_mT*loc_mT*(pi00 + pi33);
			double b = -2.0*loc_mT*(px*pi01 + py*pi02);
			double c = px*px*pi11 + 2.0*px*py*pi12 + py*py*pi22 - loc_mT*loc_mT*pi33;

			double alpha = one_by_Tdec*gammaT*loc_mT;
			double transverse_f0 = exp( one_by_Tdec*(gammaT*(px*vx + py*vy) + mu) );

			complex<double> I0_a_b_g, I1_a_b_g, I2_a_b_g, I3_a_b_g;
			complex<double> I0_2a_b_g, I1_2a_b_g, I2_2a_b_g, I3_2a_b_g;
			I(alpha, 0.0, 0.0, I0_a_b_g, I1_a_b_g, I2_a_b_g, I3_a_b_g);
			I(2.0*alpha, 0.0, 0.0, I0_2a_b_g, I1_2a_b_g, I2_2a_b_g, I3_2a_b_g);

			double term1 = transverse_f0 * (A*I1_a_b_g.real() + B*I0_a_b_g.real());
			double term2 = C * transverse_f0
							* ( A*a*I3_a_b_g.real() + (B*a+b*A)*I2_a_b_g.real() + (B*b+c*A)*I1_a_b_g.real() + B*c*I0_a_b_g.real() );
			double term3 = -sign * C * transverse_f0 * transverse_f0
							* ( A*a*I3_2a_b_g.real() + (B*a+b*A)*I2_2a_b_g.real() + (B*b+c*A)*I1_2a_b_g.real() + B*c*I0_2a_b_g.real() );

			double FOcell_density = term1 + term2 + term3;
			//////////////////////////////////
			//////////////////////////////////

			//////////////////////////////////
			// Now decide what to do with this FO cell
			//ignore points where delta f is large or emission function goes negative from pdsigma
			if ( (term2 + term3 < 0.0) || (flagneg == 1 && FOcell_density < tol) )
			{
				FOcell_density = 0.0;
				//continue;
			}

			// add FOdensity into full spectra at this pT, pphi
			spectra_at_pTpphi += eta_s_symmetry_factor * FOcell_density;
		}		//end of isurf loop

		//update spectra
		spectra[local_pid][ipT][ipphi] = spectra_at_pTpphi;
		thermal_spectra[local_pid][ipT][ipphi] = spectra_at_pTpphi;
		log_spectra[local_pid][ipT][ipphi] = log(abs(spectra_at_pTpphi) + 1.e-100);
		sign_spectra[local_pid][ipT][ipphi] = sgn(spectra_at_pTpphi);
	}		// end of pT, pphi loop

	sw_ThermalResonanceSpectra.Stop();
	*global_out_stream_ptr << "\t\t\t*** Took " << sw_ThermalResonanceSpectra.printTime() << " seconds for whole function." << endl;

	return;
}

//////////////////////////////////////////
void CorrelationFunction::Cal_dN_dypTdpTdphi_with_weights(int local_pid, int ipY, int iqt, int iqz, double * BC_chunk)
{
	Stopwatch sw, sw_FOsurf;
	sw.Start();

	// set particle information
	double sign = all_particles[local_pid].sign;
	double degen = all_particles[local_pid].gspin;
	double localmass = all_particles[local_pid].mass;
	double mu = all_particles[local_pid].mu;

	// set some freeze-out surface information that's constant the whole time
	double prefactor = 1.0*degen/(8.0*M_PI*M_PI*M_PI)/(hbarC*hbarC*hbarC);
	double eta_s_symmetry_factor = 2.0;
	double Tdec = (&FOsurf_ptr[0])->Tdec;
	double Pdec = (&FOsurf_ptr[0])->Pdec;
	double Edec = (&FOsurf_ptr[0])->Edec;
	double one_by_Tdec = 1./Tdec;
	double deltaf_prefactor = 0.;
	if (use_delta_f)
		deltaf_prefactor = 1./(2.0*Tdec*Tdec*(Edec+Pdec));

	double alpha_min = 4.0, alpha_max = 75.0;

	double * expK0_Bessel_re = new double [n_alpha_points+1];
	double * expK0_Bessel_im = new double [n_alpha_points+1];
	double * expK1_Bessel_re = new double [n_alpha_points+1];
	double * expK1_Bessel_im = new double [n_alpha_points+1];
	cs_accel_expK0re->a = alpha_min;
	cs_accel_expK0re->b = alpha_max;
	cs_accel_expK0im->a = alpha_min;
	cs_accel_expK0im->b = alpha_max;
	cs_accel_expK1re->a = alpha_min;
	cs_accel_expK1re->b = alpha_max;
	cs_accel_expK1im->a = alpha_min;
	cs_accel_expK1im->b = alpha_max;

	for (int ia = 0; ia < n_alpha_points+1; ++ia)
	{
		expK0_Bessel_re[ia] = 0.0;
		expK0_Bessel_im[ia] = 0.0;
		expK1_Bessel_re[ia] = 0.0;
		expK1_Bessel_im[ia] = 0.0;
	}

	double I0_a_b_g_re, I1_a_b_g_re, I2_a_b_g_re, I3_a_b_g_re;
	double I0_2a_b_g_re, I1_2a_b_g_re, I2_2a_b_g_re, I3_2a_b_g_re;
	double I0_a_b_g_im, I1_a_b_g_im, I2_a_b_g_im, I3_a_b_g_im;
	double I0_2a_b_g_im, I1_2a_b_g_im, I2_2a_b_g_im, I3_2a_b_g_im;

	double C = deltaf_prefactor;

	double ** alt_long_array_C = new double * [qxnpts * qynpts];
	double ** alt_long_array_S = new double * [qxnpts * qynpts];
	for (int isa = 0; isa < qxnpts * qynpts; ++isa)
	{
		alt_long_array_C[isa] = new double [n_pT_pts * n_pphi_pts];
		alt_long_array_S[isa] = new double [n_pT_pts * n_pphi_pts];
		for (int isa2 = 0; isa2 < n_pT_pts * n_pphi_pts; ++isa2)
		{
			alt_long_array_C[isa][isa2] = 0.0;
			alt_long_array_S[isa][isa2] = 0.0;
		}
	}

	/////////////////////////////////////////////////////////////
	// Loop over all freeze-out surface fluid cells (for now)
	/////////////////////////////////////////////////////////////
	int iBC = 0;
	for (int isurf = 0; isurf < FO_length; ++isurf)
	{
		FO_surf * surf = &FOsurf_ptr[isurf];

		double tau = surf->tau;

		double vx = surf->vx;
		double vy = surf->vy;
		double gammaT = surf->gammaT;

		double da0 = surf->da0;
		double da1 = surf->da1;
		double da2 = surf->da2;

		double pi00 = surf->pi00;
		double pi01 = surf->pi01;
		double pi02 = surf->pi02;
		double pi11 = surf->pi11;
		double pi12 = surf->pi12;
		double pi22 = surf->pi22;
		double pi33 = surf->pi33;

		double qt = qt_pts[iqt];
		double qz = qz_pts[iqz];
		double ch_pY = ch_SP_pY[ipY];
		double sh_pY = sh_SP_pY[ipY];
		double beta = tau * hbarCm1 * ( qt*ch_pY - qz*sh_pY );
		double gamma = tau * hbarCm1 * ( qz*ch_pY - qt*sh_pY );

		// Load Bessel Chebyshev coefficients
		for (int ia = 0; ia < n_alpha_points+1; ++ia)
			expK0_Bessel_re[ia] = BC_chunk[iBC++];
		for (int ia = 0; ia < n_alpha_points+1; ++ia)
			expK0_Bessel_im[ia] = BC_chunk[iBC++];
		for (int ia = 0; ia < n_alpha_points+1; ++ia)
			expK1_Bessel_re[ia] = BC_chunk[iBC++];
		for (int ia = 0; ia < n_alpha_points+1; ++ia)
			expK1_Bessel_im[ia] = BC_chunk[iBC++];

		cs_accel_expK0re->c = expK0_Bessel_re;
		cs_accel_expK0im->c = expK0_Bessel_im;
		cs_accel_expK1re->c = expK1_Bessel_re;
		cs_accel_expK1im->c = expK1_Bessel_im;

		double * tmpX = oscx[isurf];
		double * tmpY = oscy[isurf];
		double short_array_C[n_pT_pts * n_pphi_pts], short_array_S[n_pT_pts * n_pphi_pts];
		for (int isa = 0; isa < n_pT_pts * n_pphi_pts; ++isa)
		{
			short_array_C[isa] = 0.0;
			short_array_S[isa] = 0.0;
		}

		/////////////////////////////////////////////////////
		// Loop over pT and pphi points (as fast as possible)
		/////////////////////////////////////////////////////
		int iidx = 0;
		for (int ipT = 0; ipT < n_pT_pts; ++ipT)
		{
			double pT = SP_pT[ipT];
			double mT = sqrt(pT*pT+localmass*localmass);
			double alpha = one_by_Tdec*gammaT*mT;

			Iint2(alpha, beta, gamma, I0_a_b_g_re, I1_a_b_g_re, I2_a_b_g_re, I3_a_b_g_re, I0_a_b_g_im, I1_a_b_g_im, I2_a_b_g_im, I3_a_b_g_im);
			if (use_delta_f)
				Iint2(2.0*alpha, beta, gamma, I0_2a_b_g_re, I1_2a_b_g_re, I2_2a_b_g_re, I3_2a_b_g_re, I0_2a_b_g_im, I1_2a_b_g_im, I2_2a_b_g_im, I3_2a_b_g_im);

			double A = tau*prefactor*mT*da0;
			double a = mT*mT*(pi00 + pi33);

			for (int ipphi = 0; ipphi < n_pphi_pts; ++ipphi)
			{
				// initialize transverse momentum information
				double px = pT*cos_SP_pphi[ipphi];
				double py = pT*sin_SP_pphi[ipphi];

				double B = tau*prefactor*(px*da1 + py*da2);
				double b = -2.0*mT*(px*pi01 + py*pi02);
				double c = px*px*pi11 + 2.0*px*py*pi12 + py*py*pi22 - mT*mT*pi33;

				double transverse_f0 = exp( one_by_Tdec*(gammaT*(px*vx + py*vy) + mu) );

				double term1_re = transverse_f0 * (A*I1_a_b_g_re + B*I0_a_b_g_re);
				double term1_im = transverse_f0 * (A*I1_a_b_g_im + B*I0_a_b_g_im);

				double c1 = A*a, c2 = B*a+b*A, c3 = B*b+c*A, c4 = B*c;
				double C1 = C * transverse_f0;
				double C2 = -sign * transverse_f0 * C1;
				double term2_re = C1 * ( c1*I3_a_b_g_re + c2*I2_a_b_g_re + c3*I1_a_b_g_re + c4*I0_a_b_g_re );
				double term3_re = C2 * ( c1*I3_2a_b_g_re + c2*I2_2a_b_g_re + c3*I1_2a_b_g_re + c4*I0_2a_b_g_re );
				double term2_im = C1 * ( c1*I3_a_b_g_im + c2*I2_a_b_g_im + c3*I1_a_b_g_im + c4*I0_a_b_g_im );
				double term3_im = C2 * ( c1*I3_2a_b_g_im + c2*I2_2a_b_g_im + c3*I1_2a_b_g_im + c4*I0_2a_b_g_im );

				short_array_C[iidx] = term1_re + term2_re + term3_re;
				short_array_S[iidx++] = term1_im + term2_im + term3_im;
			}
		}

		////////////////////////////////////////
		// Loop over qx, qy, pT, and pphi points
		////////////////////////////////////////
		long idx = 0;
		const long iidx_end = (long)n_pT_pts * (long)n_pphi_pts;
		for (int iqx = 0; iqx < qxnpts; ++iqx)
		{
			double cosAx = tmpX[iqx * 2 + 0], sinAx = tmpX[iqx * 2 + 1];
			for (int iqy = 0; iqy < qynpts; ++iqy)
			{
				double cosAy = tmpY[iqy * 2 + 0], sinAy = tmpY[iqy * 2 + 1];
				double cos_trans_Fourier = cosAx*cosAy - sinAx*sinAy;
				double sin_trans_Fourier = -sinAx*cosAy - cosAx*sinAy;
				double * ala_C = alt_long_array_C[idx];
				double * ala_S = alt_long_array_S[idx];
				long iidx = 0;
				while ( iidx < iidx_end )
				{
					double cos_qx_S_x_K = short_array_C[iidx];
					double sin_qx_S_x_K = short_array_S[iidx];
					ala_C[iidx] += cos_trans_Fourier * cos_qx_S_x_K + sin_trans_Fourier * sin_qx_S_x_K;
					ala_S[iidx++] += cos_trans_Fourier * sin_qx_S_x_K - sin_trans_Fourier * cos_qx_S_x_K;
				}
			}
		}
	}

	int idx = 0;
	for (int iqx = 0; iqx < qxnpts; ++iqx)
	for (int iqy = 0; iqy < qynpts; ++iqy)
	for (int ipT = 0; ipT < n_pT_pts; ++ipT)
	for (int ipphi = 0; ipphi < n_pphi_pts; ++ipphi)
	{
		current_dN_dypTdpTdphi_moments[fixQTQZ_indexer(ipT,ipphi,ipY,iqx,iqy,0)] = alt_long_array_C[iqx * qynpts + iqy][ipT * n_pphi_pts + ipphi];
		current_dN_dypTdpTdphi_moments[fixQTQZ_indexer(ipT,ipphi,ipY,iqx,iqy,1)] = alt_long_array_S[iqx * qynpts + iqy][ipT * n_pphi_pts + ipphi];
	}
	//////////
	//////////

	delete [] expK0_Bessel_re;
	delete [] expK0_Bessel_im;
	delete [] expK1_Bessel_re;
	delete [] expK1_Bessel_im;

	for (int isa = 0; isa < qxnpts * qynpts; ++isa)
	{
		delete [] alt_long_array_C[isa];
		delete [] alt_long_array_S[isa];
	}
	delete [] alt_long_array_C;
	delete [] alt_long_array_S;

	sw.Stop();
	*global_out_stream_ptr << "Total function call took " << sw.printTime() << " seconds." << endl;
	
	return;
}

//End of file
