// crustcool.cc
//
// A time dependent code to model the thermal evolution
// of an accreting neutron star crust
// based on "cool.cc" which makes long X-ray burst lightcurves

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "../h/nr.h"
#include "../h/nrutil.h"
#include "../h/odeint.h"
#include "../h/eos.h"
#include "../h/spline.h"
#include "../h/timer.h"
#include "../h/data.h"
#include "../h/ns.h"

#pragma mark ====== Declarations ======
double dTdt(int i, double *T);
void derivs(double t, double T[], double dTdt[]);
void jacobn(double, double *, double *, double **, int);
void calculate_vars(int i, double T, double y, double *CP, double *K, double *NU, double *EPS);
void outer_boundary(double T1, double K1, double CP1, double NU1, double EPS1, double *T0, double *K0, double *CP0, double *NU0, double *EPS0);
void inner_boundary(double TN, double KN, double CPN, double NUN, double EPSN, double *TN1, double *KN1, double *CPN1, double *NUN1, double *EPSN1);
double calculate_heat_flux(int i, double *T);
void set_up_initial_temperature_profile_by_heating(void);
void set_up_initial_temperature_profile_piecewise(char *fname);
void precalculate_vars(void);
void set_up_grid(const char*fname);
void get_TbTeff_relation(void);
double crust_heating(int i);
double energy_deposited(int i);
void output_result_for_step(int j, FILE *fp, FILE *fp2, double timesofar, double *last_time_output);
void read_in_data(const char *fname);
void calculate_cooling_curve(char *fname);
void calculate_chisq(Ode_Int *ODE, Spline *TEFF, double g, double ZZ, double R,double Lscale,double Lmin);
void set_composition(void);
void heatderivs(double t, double T[], double dTdt[]);
void heatderivs2(double t, double T[], double dTdt[]);
void parse_parameters(char *fname, char*sourcename);

// global variables
struct globals {
  	int N;  
  	double dx;
  	double *P, *CP, *K, *F, *NU, *rho, *EPS;
	double *Qheat,*Qimp;
	double **CP_grid, **K1_grid, **K0_grid, **NU_grid, **EPS_grid, **KAPPA_grid, **K1perp_grid, **K0perp_grid;
	double betamin, betamax, deltabeta;
  	double g, ZZ,mass,radius;
	double time_to_run;
	double Pb, Pt, yt;
	double rhot,rhob,heating_P1,heating_P2;
	double mdot;
	double Tt, Fin, Tc;
	int nbeta, nuflag, accreting, output, hardwireQ, instant_heat;
	int use_piecewise, force_precalc, use_my_envelope, gpe;
	int force_cooling_bc, extra_heating;
	FILE *fp,*fp2;
	double Qinner, Qrho;
	double energy_deposited_outer, energy_deposited_inner, energy_slope;
	double outburst_duration, deep_heating_factor;
	double angle_mu;
	double extra_Q,extra_y;
	double Lscale,Lmin;
} G;

Ode_Int ODE;
Eos EOS;
Spline RHO;
Spline TEFF;
Spline AASpline; 
Spline ZZSpline;
Spline YnSpline;
clock_t timer;

#pragma mark =========== Code ============

int main(int argc, char *argv[])
{

	// ------------------------------ Parameters ---------------------------------

	// determine the filename for the 'init.dat' parameter file
	char fname[200];
	char fnamedefault[10]="init.dat";
	switch(argc) {
		case 3:
			strcat(fname,"/tmp/init.dat.");
			strcat(fname,argv[1]);
			break;
		case 2:
			strcat(fname,"init/init.dat.");
			strcat(fname,argv[1]);
			break;
		default:
			strcat(fname,fnamedefault);
	}
	
	// name of source for data file: default is 1659
	char sourcename[200]="1659";

	// get input parameters
	parse_parameters(fname,sourcename);
	
	// Read in observed lightcurve
	read_in_data(sourcename);

	// ------------------------------ Set up ------------------------------------

 	// set up the hydrostatic grid
	// the filename is a crust model which gives composition and heating
	// it is *not* used if hardwireQ = 1 (i.e. if EOS.Q is specified in the init.dat file)
	set_up_grid("data/crust_model_shell");

	// read in Tb-Teff relation
	get_TbTeff_relation();
	
  	// precalculate CP, K, eps_nu as a function of T on the grid
	// note that for K this is done in such a way that we do not need to recalculate
	// if Qimp changes
	start_timing(&timer);
  	precalculate_vars();
	stop_timing(&timer,"precalculate_vars");

  	// initialize the integrator
  	ODE.init(G.N+1);
  	ODE.stiff=1; ODE.tri=1;  // stiff integrator with tridiagonal solver
  
	if (G.output) {
  		G.fp=fopen("gon_out/out","w");
  		G.fp2=fopen("gon_out/prof","w");
  		fprintf(G.fp,"%d %lg\n",G.N+1,G.g);
	}
	
	// ---------------------------- Integrate ----------------------------------

	// calculate the cooling curve
	calculate_cooling_curve(fname);

	// calculate chisq
	calculate_chisq(&ODE,&TEFF,G.g,G.ZZ,G.radius,G.Lscale,G.Lmin);

	// ----------------------------------------------------------------------------------

	// tidy up
	if (G.output) {
		fclose(G.fp);
  		fclose(G.fp2);
	}
  	ODE.tidy(); 
	EOS.tidy();
  	free_vector(G.rho,0,G.N+2);
  	free_vector(G.CP,0,G.N+2);
  	free_vector(G.P,0,G.N+2);
  	free_vector(G.K,0,G.N+2);
  	free_vector(G.F,0,G.N+2);
  	free_vector(G.NU,0,G.N+2);
  	free_vector(G.EPS,0,G.N+2);
}



void calculate_cooling_curve(char *fname) 
{
	double timesofar=0.0,last_time_output=0.0;

 	// set up the initial temperature profile
  	if (G.use_piecewise) set_up_initial_temperature_profile_piecewise(fname);
	else set_up_initial_temperature_profile_by_heating();

	//	now cool
	printf("Running for time %lg seconds\n", G.time_to_run);
	G.accreting=0;  // turn off accretion for the cooling phase
	start_timing(&timer);
	ODE.dxsav=1e4;
	ODE.go(0.0, G.time_to_run, ODE.dxsav, 1e-6, derivs);
	stop_timing(&timer,"ODE.go");

	// output results
	if (G.output) {
		printf("Starting output\n");
		start_timing(&timer);
		for (int j=1; j<=ODE.kount; j++) output_result_for_step(j,G.fp,G.fp2,timesofar,&last_time_output);
		fflush(G.fp); fflush(G.fp2);
		stop_timing(&timer,"output");
	}
	
	timesofar+=G.time_to_run;
    printf("number of steps = %d, time=%lg\n", ODE.kount, timesofar);	
}






void output_result_for_step(int j, FILE *fp, FILE *fp2,double timesofar,double *last_time_output) 
{
	// Output if enough time has elapsed
	if ((fabs(log10(fabs(timesofar+ODE.get_x(j))*G.ZZ)-log10(fabs(*last_time_output))) >= 0.01) || 
			(fabs(timesofar)+ODE.get_x(j))*G.ZZ < 1e5) {

		// get CP,K,eps,eps_nu at each point on the grid
		for (int i=1; i<=G.N+1; i++) calculate_vars(i,ODE.get_y(i,j),G.P[i],&G.CP[i],&G.K[i],&G.NU[i],&G.EPS[i]);

		// outer boundary
		double T0;
		outer_boundary(ODE.get_y(1,j),G.K[1],G.CP[1],G.NU[1],G.EPS[1],&T0,&G.K[0],&G.CP[0],&G.NU[0],&G.EPS[0]);

		// timestep
		double dt;
		if (j==1) dt=ODE.get_x(j); else dt=ODE.get_x(j)-ODE.get_x(j-1);

		// heat fluxes on the grid
		double *TT;
		TT=vector(1,G.N+1);
		for (int i=1; i<=G.N+1; i++) TT[i]=ODE.get_y(i,j);
		double FF = calculate_heat_flux(1,TT);
		for (int i=1; i<=G.N+1; i++) G.F[i] = calculate_heat_flux(i,TT);
		free_vector(TT,1,G.N+1);

		// total neutrino luminosity
		double Lnu=0.0;
		for (int i=1; i<=G.N; i++) Lnu += G.NU[i]*G.dx*G.P[i]/G.g;
	
		// we output time, fluxes and TEFF that are already redshifted into the observer frame
		// gon_out/prof
		fprintf(fp2, "%lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg\n", (timesofar+ODE.get_x(j))*G.ZZ, 
			pow((G.radius/11.2),2.0)*G.F[2]/(G.ZZ*G.ZZ), pow((G.radius/11.2),2.0)*FF/(G.ZZ*G.ZZ),
			ODE.get_y(G.N-5,j), pow((G.g/2.28e14)*TEFF.get(ODE.get_y(1,j))/5.67e-5,0.25)/G.ZZ, 
			ODE.get_y(1,j), pow((G.g/2.28e14)*TEFF.get(ODE.get_y(1,j))/5.67e-5,0.25),
			pow((G.radius/11.2),2.0)*G.F[G.N+1]/(G.ZZ*G.ZZ),pow((G.radius/11.2),2.0)*G.F[G.N]/(G.ZZ*G.ZZ),
			4.0*PI*pow(1e5*G.radius,2.0)*Lnu/(G.ZZ*G.ZZ), dt);
			
		if ((fabs(log10(fabs(timesofar+ODE.get_x(j))*G.ZZ)-log10(fabs(*last_time_output))) >= 1000.0) ||
			(fabs(timesofar)+ODE.get_x(j))*G.ZZ < 1e10) {
			// temperature profile into gon_out/out
			fprintf(fp,"%lg\n",G.ZZ*(timesofar+ODE.get_x(j)));
			for (int i=1; i<=G.N+1; i++)
				fprintf(fp, "%lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg\n", 
					G.P[i], ODE.get_y(i,j), G.F[i], G.NU[i], G.g*(G.F[i+1]-G.F[i])/(G.dx*G.P[i]), G.rho[i], G.CP[i]*G.rho[i], 
					ODE.get_d(i,j),1e8*pow(G.P[i]/2.521967e17,0.25), G.K[i], 2.521967e-15*pow(ODE.get_y(i,j),4)/G.P[i],
					G.NU[i]);
 		}
		*last_time_output=(timesofar+ODE.get_x(j))*G.ZZ;

	}


}


#pragma mark ====== Integration ======

void derivs(double t, double T[], double dTdt[])
// calculates the time derivatives for the whole grid
{
	// First calculate quantities at each grid point
	for (int j=1; j<=G.N; j++) calculate_vars(j,T[j], G.P[j], &G.CP[j], &G.K[j], &G.NU[j],&G.EPS[j]);
  	outer_boundary(T[1],G.K[1],G.CP[1],G.NU[1],G.EPS[1],&T[0],&G.K[0],&G.CP[0],&G.NU[0],&G.EPS[0]);
  	inner_boundary(T[G.N],G.K[G.N],G.CP[G.N],G.NU[G.N],G.EPS[G.N],
				&T[G.N+1],&G.K[G.N+1],&G.CP[G.N+1],&G.NU[G.N+1],&G.EPS[G.N+1]);

	// determine the fluxes at the half-grid points
	//  G.F[i] is the flux at i-1/2
  	for (int i=1; i<=G.N+1; i++)   G.F[i] = calculate_heat_flux(i,T);	
	
	// Calculate the derivatives dT/dt
	for (int i=1; i<=G.N; i++) {
  		dTdt[i]=G.g*(G.F[i+1]-G.F[i])/(G.dx*G.CP[i]*G.P[i]);
		if (G.nuflag) dTdt[i]+=-(G.NU[i]/G.CP[i]);
		if (G.accreting) dTdt[i]+=G.EPS[i]/G.CP[i];
	}
  	dTdt[G.N+1]=0.0;
}

double calculate_heat_flux(int i, double *T)
{
	double flux;
	if (i>1 || (G.accreting && G.outburst_duration > 1.0/365.0 && !G.force_cooling_bc))
//		if (i>1 || (G.accreting && EOS.B == 0.0))   
		// use this inside the grid, or at the surface when we are accreting (which 
		// fixes the outer temperature)
	 	flux = 0.5*(G.K[i]+G.K[i-1])*(T[i]-T[i-1])/G.dx;	
	else {
		// cooling boundary condition
		if (EOS.B == 0.0 || G.use_my_envelope) {
			// from my envelope calculation (makegrid.cc)
			flux = (G.g/2.28e14)*TEFF.get(T[i]);
		} else {
			// for magnetars we use
			// Potekhin & Yakovlev 2001 eq.(27)
			double T9 = T[i]*1e-9;
			double xi = T9 - 0.001*pow(1e-14*G.g,0.25)*sqrt(7.0*T9);
			flux = 5.67e-5 * 1e24 * G.g*1e-14 * (pow(7*xi,2.25)+pow(0.333*xi,1.25));
		
			// or use makegrid.cc calculation
			//flux = (G.g/2.28e14)*TEFF.get(T[i]);
			
			
			// now correct for B ... 
			if (G.angle_mu >= 0.0) {
				// use the enhancement along the field direction
				double B12=EOS.B*1e-12;
				double chi1 = 1.0 + 0.0492*pow(B12,0.292)/pow(T9,0.24);
				//double chi2 = sqrt(1.0 + 0.1076*B12*pow(0.03+T9,-0.559))/
				//			pow(1.0+0.819*B12/(0.03+T9),0.6463);
				double fcond = 4.0*G.angle_mu*G.angle_mu/(1.0+3.0*G.angle_mu*G.angle_mu);		
				flux *= fcond*pow(chi1,4.0);//+(1.0-fcond)*pow(chi2,4.0);

			} else {
				// or use eq. (31) or PY2001  which gives F(B)/F(0)
				double fac, a1,a2,a3,beta;
				beta = 0.074*sqrt(1e-12*EOS.B)*pow(T9,-0.45);
				a1=5059.0*pow(T9,0.75)/sqrt(1.0 + 20.4*sqrt(T9) + 138.0*pow(T9,1.5) + 1102.0*T9*T9);
				a2=1484.0*pow(T9,0.75)/sqrt(1.0 + 90.0*pow(T9,1.5)+ 125.0*T9*T9);
				a3=5530.0*pow(T9,0.75)/sqrt(1.0 + 8.16*sqrt(T9) + 107.8*pow(T9,1.5)+ 560.0*T9*T9);
				fac = (1.0 + a1*beta*beta + a2*pow(beta,3.0) + 0.007*a3*pow(beta,4.0))/(1.0+a3*beta*beta);
				flux *= fac;
			}
		}
	}
		
	return flux;
}

double dTdt(int i, double *T)
// calculates the time derivative for grid cell i 
// This is used when calculating the jacobian in tri-diagonal form
{
	int k=i-1; if (k<1) k=1;
	int k2=i+1; if (k2>G.N+1) k2=G.N+1;
	for (int j=k; j<=k2; j++) calculate_vars(j,T[j], G.P[j], &G.CP[j], &G.K[j], &G.NU[j],&G.EPS[j]);
	if (i==1) outer_boundary(T[1],G.K[1],G.CP[1],G.NU[1],G.EPS[1],&T[0],&G.K[0],&G.CP[0],&G.NU[0], &G.EPS[0]);
	if (i==G.N) inner_boundary(T[G.N],G.K[G.N],G.CP[G.N],G.NU[G.N], G.EPS[G.N],
										&T[G.N+1],&G.K[G.N+1],&G.CP[G.N+1],&G.NU[G.N+1],&G.EPS[G.N+1]);

	double f=G.g*(calculate_heat_flux(i+1,T)-calculate_heat_flux(i,T))/(G.dx*G.CP[i]*G.P[i]);
	if (G.nuflag) f+=-(G.NU[i]/G.CP[i]);
	if (G.accreting) f+=G.EPS[i]/G.CP[i];

	return f;
}

void outer_boundary(double T1, double K1, double CP1, double NU1, double EPS1,
		double *T0, double *K0, double *CP0, double *NU0, double *EPS0)  
{
	if (G.accreting && G.Tt>0.0) *T0=G.Tt;   // constant temperature during accretion
	else *T0=T1*(8.0-G.dx)/(8.0+G.dx);   // assumes radiative zero solution, F\propto T^4
	*K0=K1; *CP0=CP1;
	if (G.nuflag) *NU0=NU1; else *NU0=0.0;
	if (G.accreting) *EPS0=EPS1; else *EPS0=0.0;
}

void inner_boundary(double TN, double KN, double CPN, double NUN, double EPSN,
	double *TN1, double *KN1, double *CPN1, double *NUN1, double *EPSN1)
{
	*TN1=G.Tc;   // fixed core temperature
//	*TN1=TN;    // zero flux inner boundary
	*KN1=KN;
	*CPN1=CPN;  
	if (G.nuflag) *NUN1=NUN; else *NUN1=0.0;
	if (G.accreting) *EPSN1=EPSN; else *NUN1=0.0;
	
}

void jacobn(double t, double *T, double *dfdt, double **dfdT, int n)
// calculates the Jacobian numerically
{
  	double e=0.01;

	// takes advantage of the tri-diagonal nature to calculate as few dTdt's as needed
	double f;
  // this assumes the arrays dfdt and dfdT are preinitialized to zero (I changed odeint to do this)
  for (int i=2; i<n; i++) {
  	T[i-1]*=1.0+e; f=dTdt(i,T);
    T[i-1]/=1.0+e; dfdT[i][i-1]=(f-dfdt[i])/(T[i-1]*e);
    T[i]*=1.0+e; f=dTdt(i,T);
    T[i]/=1.0+e; dfdT[i][i]=(f-dfdt[i])/(T[i]*e);
    T[i+1]*=1.0+e; f=dTdt(i,T);
    T[i+1]/=1.0+e; dfdT[i][i+1]=(f-dfdt[i])/(T[i+1]*e);
  }
  {
	int i=1;
	T[i]*=1.0+e; f=dTdt(i,T);
	T[i]/=1.0+e; dfdT[i][i]=(f-dfdt[i])/(T[i]*e);
	T[i+1]*=1.0+e; f=dTdt(i,T);
	T[i+1]/=1.0+e; dfdT[i][i+1]=(f-dfdt[i])/(T[i+1]*e);
  }
}  


void calculate_vars(int i, double T, double P, double *CP, double *K, double *NU,double *EPS)
{
	// sometimes we get a nan value for T here from the integrator
	// In this case, set the temperature to be some value.. this seems to
	// deal with this problem ok
	if (isnan(T) || T<0.0) T=1e7;
	
	double beta=log10(T);
	// if beta lies outside the table, set it to the max or min value
	if (beta > G.betamax) beta = G.betamax;
	if (beta < G.betamin) beta = G.betamin;
		
		// lookup values in the precalculated table
	int j = 1 + (int) ((beta-G.betamin)/G.deltabeta);
	double interpfac=(beta-(G.betamin + (j-1)*G.deltabeta))/G.deltabeta;
	// interpolate the thermal conductivity to the current
	// value of impurity parameter Q
	double K0=G.K0_grid[i][j] + (G.K0_grid[i][j+1]-G.K0_grid[i][j])*interpfac;
	double K1=G.K1_grid[i][j] + (G.K1_grid[i][j+1]-G.K1_grid[i][j])*interpfac;
	//double K0perp=G.K0perp_grid[i][j] + (G.K0perp_grid[i][j+1]-G.K0perp_grid[i][j])*interpfac;
	//double K1perp=G.K1perp_grid[i][j] + (G.K1perp_grid[i][j+1]-G.K1perp_grid[i][j])*interpfac;
	//K0perp=0.0; K1perp=0.0;

	// use something like this next line to hardwire Q values
	double Qval;
	if (G.hardwireQ) {
		if (G.rho[i] > G.Qrho) Qval=G.Qinner; else Qval=EOS.Q;
//		if (P>2.28e29) Qval=G.Qinner; else Qval=EOS.Q;
	} else {
		Qval = G.Qimp[i];	
	}
	double KK,KKperp;
	KK=G.g*K0*K1/(K0*Qval+(1.0-Qval)*K1);

	double kappa;
	kappa=G.KAPPA_grid[i][j] + (G.KAPPA_grid[i][j+1]-G.KAPPA_grid[i][j])*interpfac;
	kappa*=G.g;
	KK += kappa;
	
	if (EOS.B > 0) {
		KKperp=0.0; //G.g*K0perp*K1perp/(K0perp*Qval+(1.0-Qval)*K1perp);
		if (G.angle_mu >= 0.0) {
			KK *= 4.0*G.angle_mu*G.angle_mu/(1.0+3.0*G.angle_mu*G.angle_mu);
		} else {
			KK = 0.5*(1.0544*KK+0.9456*KKperp);  // average over dipole geometry	
		}
	}
//	if (EOS.B > 0) {
//	//KKperp = G.g*K0perp*K1perp/(K0perp*Qval+(1.0-Qval)*K1perp);		
//	double fcond = 4.0*G.angle_mu*G.angle_mu/(1.0+3.0*G.angle_mu*G.angle_mu);		
//	*K=fcond*KK;//+(1.0-fcond)*KKperp;	
//} else {
	*K=KK;
//}
	
	*CP=G.CP_grid[i][j] + (G.CP_grid[i][j+1]-G.CP_grid[i][j])*interpfac;
	if (G.nuflag) *NU=G.NU_grid[i][j] + (G.NU_grid[i][j+1]-G.NU_grid[i][j])*interpfac; 
	else *NU=0.0;
	if (G.accreting) {
		*EPS=G.EPS_grid[i][1];  // assume heating is independent of temperature 
	//	*EPS=(G.EPS_grid[i][j] + (G.EPS_grid[i][j+1]-G.EPS_grid[i][j])*interpfac); 
		*EPS=*EPS * G.mdot * G.g;
	}
	else *EPS=0.0;
 }


#pragma mark ====== Setup temperature profile ======

void set_up_initial_temperature_profile_piecewise(char *fname)
{
	// first read in the specified temperature-density relation from the file
	FILE *fp;
	char s1[100];
	printf("Reading initial temperature profile from %s\n",fname);
	fp = fopen(fname,"r");
	double *rhovec, *Tvec;
	rhovec=vector(1,100);
	Tvec=vector(1,100);
	int commented=0;
	rhovec[1] = G.rho[1];
	Tvec[1] = G.Tc;
	int i=2;
	while (!feof(fp)) {		
		double rho, T,T2=0.0;
		// new lines: read from lines marked ">" in init.dat
		(void) fgets(s1,200,fp);		
		if (!strncmp(s1,"##",2)) commented = 1-commented;
		if (!strncmp(s1,">",1) && commented==0) {
			int nvar = sscanf(s1,">%lg\t%lg\t%lg\n",&rho,&T,&T2);
			// old: direct read from Tinit.dat
			//		fscanf(fp,"%lg %lg\n",&rho,&T);
			if (T < 0.0) T=G.Tc;
			if (T2 < 0.0) T2=G.Tc;
			if (rho < 0.0) rho = G.rho[G.N];
			if (rho == 0.0) {
				Tvec[1] = T;
			} else {
				rhovec[i] = rho;
				Tvec[i] = T;
				i++;
				if (nvar == 3) {
					rhovec[i] = rho*1.01;
					Tvec[i] = T2;
					i++;
				}
			}			
		}
	}
	fclose(fp);
	if (rhovec[i-1] != G.rho[G.N]) {  // if we didn't specify it in the file,
									// set the temperature of the base to the core temperature
		rhovec[i] = G.rho[G.N];
		Tvec[i] = G.Tc;
		i++;
	}
	int nvec=i-1;

	if (nvec == 0) {
		printf("ERROR:The piecewise flag is set but the temperature profile is not specified in init.dat!\n");
		exit(0);
	}

	double totalEd=0.0;

	// now assign initial temperatures
	if (G.output) fp = fopen("gon_out/initial_condition","w");
	double I=0.0;   // I is the integral used to get the thermal time
	double E=0.0;   // total energy deposited
	for (int i=1; i<=G.N+1; i++) {

		double Ti;
		if (i==1) {
			Ti=Tvec[1]; 
			G.Tt=Ti;
		} else {
			if (i==G.N+1) {
				Ti=Tvec[nvec];		
				//G.Tc=Ti;
			} else {
				int	j=1; 
				while (rhovec[j] < G.rho[i] && j<nvec) j++;
				Ti = pow(10.0,log10(Tvec[j-1]) + log10(Tvec[j]/Tvec[j-1])*log10(G.rho[i]/rhovec[j-1])/log10(rhovec[j]/rhovec[j-1]));
				//printf("%d %lg %lg %lg %lg %lg\n",j,Tvec[j-1],Tvec[j],rhovec[j-1],rhovec[j],G.rho[i]);
			}
		}	
		
//		printf("%lg %lg\n",G.rho[i],Ti);
		ODE.set_bc(i,Ti);							
				
		// Figure out the energy deposited
		EOS.P=G.P[i];
		EOS.T8=G.Tc*1e-8;
		set_composition();				
		EOS.rho=EOS.find_rho();
		Ode_Int ODEheat;
		ODEheat.init(1);
		ODEheat.set_bc(1,0.0);
		double Ed;
		ODEheat.go(G.Tc,Ti,0.01*G.Tc, 1e-6, heatderivs2);
		Ed = ODEheat.get_y(1,ODEheat.kount);
		totalEd+=Ed*4.0*PI*1e10*G.radius*G.radius*G.dx*G.P[i]/G.g;
		//if (Ed > 0.0) printf("heating cell %d: Ti=%lg Tf=%lg E25=%lg rho=%lg\n",
		//			i, G.Tc, Ti, Ed*G.rho[i]*1e-25, G.rho[i]);
		ODEheat.tidy();			
					
		if (G.output) {
			EOS.P=G.P[i]; EOS.rho=G.rho[i]; EOS.T8=1e-8*Ti;
			set_composition();				

			double TTC=EOS.TC();
			if (EOS.Yn >0.0 && Ti > TTC && TTC > Tvec[nvec]) {
				printf("%d %lg %lg %lg\n", i, TTC, Ti, EOS.Yn);
				ODE.set_bc(i,TTC);
			}

			double Kcond = EOS.potek_cond();

			I+=sqrt(EOS.CP()/(Kcond*EOS.rho))*(G.P[i]-G.P[i-1])/G.g;
			double tt = I*I*0.25/(24.0*3600.0);
			
			fprintf(fp, "%d %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg\n", i, G.P[i], Ti, EOS.rho,EOS.CV(), 
					Kcond, EOS.Yn, 1e-39*EOS.Yn * EOS.rho/1.67e-24, tt,E,EOS.A[1],EOS.Z[1],TTC, EOS.econd(), EOS.Ye()*EOS.rho/1.67e-24);
		}
	}	
	
	printf("Total energy input to get this initial T profile = %lg (redshifted=%lg)\n",totalEd,totalEd/G.ZZ);
	
	if (G.output) fclose(fp);
	
	free_vector(rhovec,1,100);
	free_vector(Tvec,1,100);
	
}


void heatderivs2(double T, double E[], double dEdT[])
{
	EOS.T8 = T/1e8;
	EOS.rho = EOS.find_rho();
	dEdT[1] = EOS.CP();
}

void heatderivs(double E, double T[], double dTdE[])
{
	EOS.T8 = T[1]/1e8;
	EOS.rho = EOS.find_rho();
	dTdE[1] = 1e25/(EOS.rho*EOS.CP());
}


void set_up_initial_temperature_profile_by_heating(void)
// initialize the temperature profile on the grid
{
	
	// first run some accretion with heating, long enough for the crust
	// to get into a thermal steady-state
	for (int i=G.N+1; i>=1; i--) {
		// a linear profile between top and bottom
		//double Ti = pow(10.0,log10(G.Tc) + log10(0.3*G.Tt/G.Tc)*log10(G.P[i]/G.Pb)/log10(G.Pt/G.Pb));
		// or constant profile
		double Ti = G.Tc;
		// a linear profile adjusts to steady state *much* more quickly,
		// but for XTEJ for example I want to heat up from isothermal and the crust 
		// does not get to steady state
		ODE.set_bc(i,Ti);
	}
	start_timing(&timer);

	if (0) {
	// First, let the crust cool for 30 years to get into eqm with the core
	G.accreting = 0;  // switch off heating for this one
	ODE.go(0.0, 30.0*3.15e7, 1e6, 1e-6, derivs);
	for (int i=1; i<=G.N+1; i++)
		ODE.set_bc(i,ODE.get_y(i,ODE.kount));
	}

	G.accreting = !G.instant_heat;	
	double dt=G.outburst_duration*3.15e7*0.0001;
	if (dt > 1e6) dt=1e6;
	//	ODE.verbose  = 1;
//	ODE.dxsav = 1e8;
	ODE.go(0.0, G.outburst_duration*3.15e7,dt, 1e-6, derivs);
	stop_timing(&timer,"ODE.go (initial heating)");
	printf("number of steps = %d  (time=%lg)\n", ODE.kount, ODE.get_x(ODE.kount));

	double timesofar = -G.outburst_duration*3.15e7;
	double last_time_output = timesofar;
	if (G.output) {
		for (int j=1; j<=ODE.kount; j++) output_result_for_step(j,G.fp,G.fp2,timesofar,&last_time_output);
		fflush(G.fp); fflush(G.fp2);
	}

	// set initial condition and write out some info
	FILE *fp=NULL;
	if (G.output) fp = fopen("gon_out/initial_condition","w");
	double I=0.0;   // I is the integral used to get the thermal time
	double E=0.0;   // total energy deposited
	for (int i=1; i<=G.N+1; i++) {
		
		double Ti = ODE.get_y(i,ODE.kount);
		if (crust_heating(i) > 0.0 && G.instant_heat) {				
			// find the initial temperature from instantaneous heating
			EOS.P=G.P[i];
			set_composition();				
			Ode_Int ODEheat;
			ODEheat.init(1);
			ODEheat.set_bc(1,Ti);
			printf("heating cell %d:  Ti=%lg E25=%lg ", i,Ti,energy_deposited(i));
			ODEheat.go(0.0, energy_deposited(i), 1e-4, 1e-6, heatderivs);
			Ti = ODEheat.get_y(1,ODEheat.kount);
			printf(" Tf=%lg  rho=%lg\n", Ti, G.rho[i]);
			ODEheat.tidy();			
		}
		
		ODE.set_bc(i,Ti);
					
		if (G.output) {
			EOS.P=G.P[i]; EOS.rho=G.rho[i]; EOS.T8=1e-8*Ti;
			set_composition();				
				//double f=1.0+EOS.Uex()/3.0;
				//double Pion=8.254e15*this->rho*this->T8*Yi()*f;   // ions
			   //double Pe=EOS.pe();
					
			double Qval;			
				if (G.hardwireQ) {
					if (G.rho[i] > G.Qrho) Qval=G.Qinner; else Qval=EOS.Q;
						//			if (P>2.28e29) Qval=G.Qinner; else Qval=EOS.Q;
								} else {
									Qval = G.Qimp[i];	
								}
								double Q_store=EOS.Q;
								EOS.Q=Qval;
								
								double Kcondperp=0.0;
			//double Kcond = potek_cond(&Kcondperp);
			double Kcond = EOS.K_cond(EOS.Chabrier_EF());
								
			I+=sqrt(EOS.CV()/(Kcond*EOS.rho))*G.P[i]*G.dx/G.g;
			double tt = I*I*0.25/(24.0*3600.0);
	//		tt = 0.25 * EOS.rho * EOS.CP() * pow(G.P[i]/(G.g*EOS.rho),2.0)/(Kcond*24.0*3600.0);

			E+=energy_deposited(i)*crust_heating(i)*G.mdot*G.g*G.outburst_duration*3.15e7*
				4.0*PI*G.radius*G.radius*1e10*G.dx*G.P[i]/G.g;
											
			fprintf(fp, "%d %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg\n", i, G.P[i], Ti, EOS.rho,EOS.CV(), 
					Kcond, EOS.Yn, 1e-39*EOS.Yn * EOS.rho/1.67e-24, tt,E,EOS.A[1],EOS.Z[1],EOS.TC(),
					EOS.Chabrier_EF(),Kcondperp,0.4*1e-9*EOS.rho*pow(EOS.Ye()/0.4,3.0)*EOS.Z[1]/34.0, EOS.cve, EOS.cvion,
					EOS.chi(&EOS.rho), EOS.chi(&EOS.T8), G.P[i]/(G.g*EOS.rho), EOS.econd());
				EOS.Q=Q_store;	
		}
	}	
	if (G.output) fclose(fp);

	printf("Total energy deposited=%lg (redshifted=%lg)\n",E,E/G.ZZ);

}



#pragma mark ====== Initial setup ======

void precalculate_vars(void) 
// calculate various quantities at each grid point as a function of temperature
// then during the run we can look them up in a table
{
	// the table is constructed in terms of log10(T)
	// for historical reasons, this is called beta here
	// (for long X-ray bursts where radiation pressure is significant,
	// beta=Prad/P is a better variable to use)
	G.nbeta=100;
	G.betamin=6.5;
	G.betamax=10.0;
	G.deltabeta = (G.betamax-G.betamin)/(1.0*(G.nbeta-1));	

	G.CP_grid = matrix(1,G.N+2,1,G.nbeta);	
	G.K1_grid = matrix(1,G.N+2,1,G.nbeta);	
	G.K0_grid = matrix(1,G.N+2,1,G.nbeta);	
	G.KAPPA_grid = matrix(1,G.N+2,1,G.nbeta);	
	G.K1perp_grid = matrix(1,G.N+2,1,G.nbeta);	
	G.K0perp_grid = matrix(1,G.N+2,1,G.nbeta);	
	G.NU_grid = matrix(1,G.N+2,1,G.nbeta);	
	G.EPS_grid = matrix(1,G.N+2,1,G.nbeta);	

	// For the crust heating, we need to convert the density limits into 
	// pressures
	EOS.rho = G.rhot;
	set_composition();
	G.heating_P1 = EOS.ptot();
	EOS.rho = G.rhob;
	set_composition();
	G.heating_P2 = EOS.ptot();
	
	FILE *fp = NULL;
	char s[100];
	if (EOS.B > 0.0) sprintf(s,"gon_out/precalc_results_%lg",log10(EOS.B));
	else sprintf(s,"gon_out/precalc_results_0");
	if (!G.force_precalc) fp=fopen(s,"r");
	// if unsuccessful (or if precalc is set) we need to recalculate
	if (fp == NULL) {
		if (G.output)
			fp=fopen(s,"w");
		printf("Precalculating quantities and writing to file %s...\n",s);

		for (int i=1; i<=G.N+1; i++) {
	
			EOS.P=G.P[i];
			EOS.rho = G.rho[i];
			set_composition();
			
			if (G.output) 
				fprintf(fp, "Grid point %d  P=%lg  rho=%lg  A=%lg  Z=%lg Yn=%lg:  T8,CP,K,eps_nu,eps_nuc\n",
					i, G.P[i], G.rho[i], (1.0-EOS.Yn)*EOS.A[1], EOS.Z[1], EOS.Yn);
		
			double heating = crust_heating(i);
		
			for (int j=1; j<=G.nbeta; j++) {		
				double beta = G.betamin + (j-1)*(G.betamax-G.betamin)/(1.0*(G.nbeta-1));
				EOS.T8 = 1e-8*pow(10.0,beta);

				G.CP_grid[i][j]=EOS.CV();
				G.NU_grid[i][j]=EOS.eps_nu();			
				G.EPS_grid[i][j]=heating;

				// we calculate the thermal conductivity for Q=0 and Q=1, and later interpolate to the
				// current value of Q. This means we can keep the performance of table lookup even when
				// doing MCMC trials which vary Q.

				double Q_store=EOS.Q;  // store Q temporarily

				EOS.Q=0.0;
				double Kcond,Kcondperp;
				//Kcond = EOS.K_cond(EOS.Chabrier_EF());
				//Kcondperp=Kcond;
				Kcond = EOS.potek_cond();
				Kcondperp = EOS.Kperp;   
				G.K0_grid[i][j]=EOS.rho*Kcond/G.P[i];
				G.K0perp_grid[i][j]=EOS.rho*Kcondperp/G.P[i];

				EOS.Q=1.0;
				//Kcond = EOS.K_cond(EOS.Chabrier_EF());
				//Kcondperp=Kcond;
				Kcond = EOS.potek_cond();
				Kcondperp = EOS.Kperp;
				G.K1_grid[i][j]=EOS.rho*Kcond/G.P[i];
				G.K1perp_grid[i][j]=EOS.rho*Kcondperp/G.P[i];

				EOS.Q=Q_store;  // restore to previous value

				// conductivity due to radiation
				(void) EOS.opac();  // call to kappa sets the variable kappa_rad
				G.KAPPA_grid[i][j] = 3.03e20*pow(EOS.T8,3)/(EOS.kappa_rad*G.P[i]);

				if (G.output) 
					fprintf(fp, "%lg %lg %lg %lg %lg %lg %lg %lg %lg\n", EOS.T8, G.CP_grid[i][j], 
						G.K0_grid[i][j],G.K1_grid[i][j], G.K0perp_grid[i][j],G.K1perp_grid[i][j],
						G.NU_grid[i][j], G.EPS_grid[i][j], G.KAPPA_grid[i][j] );

				G.EPS_grid[i][j]*=energy_deposited(i);

			}	
		}
		if (G.output) fclose(fp);

	} else {
		
		printf("***Reading precalculated quantities from file %s\n", s);
		
		for (int i=1; i<=G.N+1; i++) {
			int kk; double dd;
			fscanf(fp, "Grid point %d  P=%lg  rho=%lg  A=%lg  Z=%lg Yn=%lg:  T8,CP,K,eps_nu,eps_nuc\n",
					&kk,&dd,&dd,&dd,&dd,&dd);
			for (int j=1; j<=G.nbeta; j++) {		
				fscanf(fp, "%lg %lg %lg %lg %lg %lg %lg %lg %lg\n", &EOS.T8, &G.CP_grid[i][j], 
					&G.K0_grid[i][j],&G.K1_grid[i][j], &G.K0perp_grid[i][j],&G.K1perp_grid[i][j],
					&G.NU_grid[i][j], &G.EPS_grid[i][j],&G.KAPPA_grid[i][j]);
				// always calculate the crust heating..
				G.EPS_grid[i][j]=crust_heating(i);
				G.EPS_grid[i][j]*=energy_deposited(i);
			}
		}
		fclose(fp);
			
	}
	
}


double energy_deposited(int i)
{
	double ener;
	if (G.rho[i]>4e11) ener = G.energy_deposited_inner;
	else ener = G.energy_deposited_outer;
	ener *= pow(G.rho[i]/1e10,G.energy_slope);
	return ener;
}


double crust_heating(int i) 
// calculates the crust heating in erg/g/s
// for grid point i
{
	double eps=0.0,P = G.P[i];

	// if we are heating on < 1day timescale then its a magnetar
	if (G.outburst_duration<1.0/365.0) {
		
		// eps in erg/g/s
		double eps_heat = 1e25/(G.rho[i]*G.outburst_duration*3.15e7);
		eps_heat /= G.mdot * G.g;   // modify to the units used in the code

		// limit the heating to a region of the crust
		double P1 = P*exp(-0.5*G.dx);
		double P2 = P*exp(0.5*G.dx);
		if (P1 > G.heating_P1 && P2 < G.heating_P2)   // we are within the heating zone
			eps = eps_heat;
		if (P1 < G.heating_P1 && P2 < G.heating_P2 && G.heating_P1<P2) {   // left hand edge of heated region
			eps = eps_heat * log(P2/G.heating_P1)/G.dx;
		}
		if (P1 > G.heating_P1 && P2 > G.heating_P2 && G.heating_P2>P1) {  // right hand edge of heated region
			eps = eps_heat * log(G.heating_P2/P1)/G.dx;
		}

	} else {  // otherwise we are doing an accreting neutron star

		if (!G.hardwireQ) {
			eps = G.Qheat[i]*8.8e4*9.64e17/(G.P[i]*G.dx);
		} else {
			// simple "smeared out" heating function, 1.2MeV in inner crust, 0.2MeV in outer crust
			if (P >= 1e16*2.28e14 && P <= 1e17*2.28e14) eps=8.8e4*G.deep_heating_factor*1.7*9.64e17/(P*log(1e17/1e16));
		 	if (P >= 3e12*2.28e14 && P < 3e15*2.28e14) eps=8.8e4*G.deep_heating_factor*0.2*9.64e17/(P*log(3e15/3e12));

			// Extra heat source in the ocean
			if (G.extra_heating) {	
				// Put all of the extra heat into one grid point
				//if (G.P[i]*exp(-0.5*G.dx) <G.extra_y*2.28e14 && G.P[i]*exp(0.5*G.dx)>G.extra_y*2.28e14)
				//		eps+=8.8e4*G.extra_Q*9.64e17/(P*G.dx);

				// More distributed heating
				double extra_y1 = G.extra_y/3.0;	
				double extra_y2 = G.extra_y*3.0;
				double eps_extra=0.0;
				double P1 = P*exp(-0.5*G.dx);
				double P2 = P*exp(0.5*G.dx);
				double geff=2.28e14;

				if (P1 > extra_y1*geff && P2 < extra_y2*geff)   // we are within the heating zone
					eps_extra=8.8e4*G.extra_Q*9.64e17/(P*log(extra_y2/extra_y1));
				if (P1 < extra_y1*geff && P2 < extra_y2*geff && extra_y1*geff<P2) {   // left hand edge of heated region
					eps_extra=8.8e4*G.extra_Q*9.64e17/(P*log(extra_y2/extra_y1));
					eps_extra *= log(P2/(extra_y1*geff))/G.dx;
				}
				if (P1 > extra_y1*geff && P2 > extra_y2*geff && extra_y2*geff>P1) {  // right hand edge of heated region
					eps_extra=8.8e4*G.extra_Q*9.64e17/(P*log(extra_y2/extra_y1));
					eps_extra *= log(extra_y2*geff/P1)/G.dx;
				}
				eps+=eps_extra;
			}
		}
	}

	return eps;	
}


void get_TbTeff_relation(void)
// reads in the Flux-T relation from the data file output by makegrid.cc
{
	double *temp, *flux;  // temporary storage to initialize the spline
	int npoints = 195;  //  needs to be >= number of points read in
	temp = vector(1,npoints);
	flux = vector(1,npoints);
	
	// the file "out/grid" is made by makegrid.cc
	// it contains  (column depth, T, flux)  in cgs
	FILE *fp;
	if (G.use_my_envelope) {
		if (EOS.B == 1e15) fp = fopen("out/grid_1e15_nopotek","r");
		else if (EOS.B == 1e14) fp = fopen("out/grid_1e14_potek","r");
		else if (EOS.B == 3e14) fp = fopen("out/grid_3e14_potek","r");
		else if (EOS.B == 3e15) fp = fopen("out/grid_3e15_potek","r");
		else {
			printf("Don't know which envelope model to use for this B!\n");
			exit(1);
		}
	} else {
		if (G.gpe) fp = fopen("out/grid_He4","r");
		else fp = fopen("out/grid_He9","r");
	}
	FILE *fp2=NULL;
	if (G.output) fp2=fopen("gon_out/TbTeff", "w");
	
	double y,T,F,rho,dummy;
	int count = 0;
	while (!feof(fp)) {
		fscanf(fp, "%lg %lg %lg %lg %lg %lg\n", &y, &T, &F,&rho,&dummy,&dummy);
		if (fabs(y-log10(G.yt))<1e-3) {  // select out the points which correspond to the top column
			count++;
			temp[count] = pow(10.0,T);
			// correct for gravity here:
			flux[count] = pow(10.0,F); //* (G.g/2.28e14);
			if (G.output) fprintf(fp2, "%d %lg %lg %lg %lg %lg\n", count,y,T,F,temp[count],flux[count]);
		}
	}
		
	fclose(fp);
	if (G.output) fclose(fp2);
	
	// the following spline contains the flux as a function of temperature at column depth G.yt
	TEFF.minit(temp,flux,count);
	
	free_vector(temp,1,npoints);
	free_vector(flux,1,npoints);
}



void set_up_grid(const char *fname)
// allocates storage for the grid and also computes the density
// and composition at each grid point
{
	// number of grid points G.N has already been set
	G.Pb=6.5e32; // pressure at the crust/core boundary
	G.Pt=G.yt*2.28e14;   // pressure at the top   // note I need to use 2.28 here to get the correct match to the envelope

	Spline QiSpline;
	Spline QhSpline;
	if (!G.hardwireQ) {   // only need this if we read in the crust model
		FILE *fp=fopen(fname,"r");
	
		int npoints;
		fscanf(fp,"%d",&npoints);
		npoints--;
		printf("Crust model has %d points\n",npoints);
		
		double *Qi,*Qh,*AA,*ZZ,*P,*Yn;
		Qi=vector(1,npoints);
		Qh=vector(1,npoints);
		AA=vector(1,npoints);
		ZZ=vector(1,npoints);
		Yn=vector(1,npoints);
		P=vector(1,npoints);

		for (int i=1; i<=npoints; i++) {
			double dummy;
			fscanf(fp, "%lg %lg %lg %lg %lg %lg %lg %lg\n",
				&P[i],&dummy,&dummy,&Qh[i],&ZZ[i],&AA[i],&Qi[i],&Yn[i]);
			//printf("%lg %lg %lg %lg %lg\n", rho[i], Qh[i],ZZ[i],AA[i],Qi[i]);
			P[i]=log10(P[i]);
		}

		YnSpline.minit(P,Yn,npoints);
		ZZSpline.minit(P,ZZ,npoints);
		AASpline.minit(P,AA,npoints);
		QiSpline.minit(P,Qi,npoints);
		QhSpline.minit(P,Qh,npoints);

		free_vector(Qi,1,npoints);
		free_vector(Qh,1,npoints);
		free_vector(AA,1,npoints);
		free_vector(ZZ,1,npoints);		
		free_vector(Yn,1,npoints);		
		free_vector(P,1,npoints);		
		fclose(fp);

	}

  	// storage
  	G.rho=vector(0,G.N+2);  
  	G.CP=vector(0,G.N+2);
  	G.P=vector(0,G.N+2);
  	G.K=vector(0,G.N+2);
  	G.F=vector(0,G.N+2);
  	G.NU=vector(0,G.N+2);
  	G.EPS=vector(0,G.N+2);
  
  	G.Qheat=vector(0,G.N+2);
  	G.Qimp=vector(0,G.N+2);

  	G.dx=log(G.Pb/G.Pt)/(G.N-1);   // the grid is equally spaced in log column
  
	FILE *fp=NULL;
	if (G.output) fp = fopen("gon_out/grid_profile","w");

	double Qtot=0.0;
  	for (int i=0; i<=G.N+2; i++) {
    	double x=log(G.Pt)+G.dx*(i-1);
    	G.P[i]=exp(x);
		EOS.P = G.P[i];
		EOS.T8=1.0;   // we have to set the temperature to something
		set_composition();
		EOS.rho=EOS.find_rho();
		G.rho[i]=EOS.rho;

		// GammaT[i] refers to i+1/2
		// The following line uses a composition of 56Fe to calculate gamma,
		// it avoids jumps in the melting point associated with e-capture boundaries
		double GammaT;
		if (0) {
			double Z=26.0,A=56.0;   // 28Si
			GammaT = pow(Z*4.8023e-10,2.0)*pow(4.0*PI*EOS.rho/(3.0*A*1.67e-24),1.0/3.0)/1.38e-16;
		} else {
			GammaT = pow(EOS.Z[1]*4.8023e-10,2.0)*pow(4.0*PI*EOS.rho/(3.0*EOS.A[1]*1.67e-24),1.0/3.0)/1.38e-16;
		}

		double Tmelt = 5e8*pow(G.P[i]/(2.28e14*1.9e13),0.25)*pow(EOS.Z[1]/30.0,5.0/3.0);
		double LoverT = 0.8 * 1.38e-16 /(EOS.A[1]*1.67e-24);

		G.Qheat[i]=0.0;
		if (!G.hardwireQ) {
			double P1 = exp(x-0.5*G.dx);
			double P2 = exp(x+0.5*G.dx);
			G.Qheat[i]=QhSpline.get(log10(P2))-QhSpline.get(log10(P1));
			if (G.Qheat[i]<0.0) G.Qheat[i]=0.0;
		}
		Qtot+=G.Qheat[i];

		if (!G.hardwireQ) {
			G.Qimp[i]=QiSpline.get(log10(G.P[i]));
			if (G.Qimp[i] < 0.0) G.Qimp[i]=0.0;
		}

		if (G.output) 
			fprintf(fp, "%d %lg %lg %lg %lg %lg %lg %lg %lg %lg %lg\n", i, G.P[i], G.rho[i], EOS.A[1]*(1.0-EOS.Yn), 
				EOS.Z[1], EOS.Yn,EOS.A[1],EOS.ptot(), Tmelt, GammaT/1e8, LoverT*1e8);
			//,AASpline.get(log10(G.rho[i])),  ZZSpline.get(log10(G.rho[i])), G.Qimp[i], G.Qheat[i]);
  	}

	if (G.output) fclose(fp);

	if (!G.hardwireQ) QiSpline.tidy();

  	printf("Grid has %d points, delx=%lg, Pb=%lg, rhob=%lg, Pt=%lg, rhot=%lg\n", 
			G.N, G.dx, G.P[G.N],G.rho[G.N],G.P[1],G.rho[1]);
		printf("Total heat release is %lg MeV\n",Qtot);
}


void set_composition(void)
{
	if (G.hardwireQ) {
		// use the EOS routines to get the composition
		// ie. crust models from the literature
		EOS.set_composition_by_pressure();	
	} else {
		// otherwise use our crust model
		// the model gives the mean A, mean Z and Yn
		// and we set up the variables as in our EOS.set_comp() routine
		EOS.Yn = YnSpline.get(log10(EOS.P));
		if (EOS.Yn < 1e-6) EOS.Yn=0.0;
		EOS.A[1]=AASpline.get(log10(EOS.P));
		EOS.A[1]/=(1.0-EOS.Yn);
		EOS.Z[1]=ZZSpline.get(log10(EOS.P));
		EOS.set_Ye=EOS.Z[1]/EOS.A[1];		
		//printf("%lg %lg %lg %lg\n", EOS.Yn, EOS.rho, EOS.A[1], EOS.Z[1]);
	}
}

void parse_parameters(char *fname,char *sourcename) {

	// ----------------------------------------------------------------------
 	//   Set parameters

	// first, some defaults, including default EOS settings
	double mass=1.62;
	G.radius=11.2;
	G.N=100;
	G.nuflag = 1;
	G.energy_slope=0.0;
	G.force_precalc=0;
	G.Qinner=-1.0;
	G.outburst_duration = (1.0/24.0) * 1.0/(365.0);  // rapid heating for magnetar case	(1 hour)
  	EOS.init(1); 
  	EOS.X[1] = 1.0;   // we only have one species at each depth;
	EOS.accr = 0;   // set crust composition
	EOS.use_potek_eos = 0;
	EOS.use_potek_cond = 1;
	EOS.B=0;   // magnetic field in the crust   (set B>0 for magnetar case)
	EOS.gap = 1;    // 0 = no gap, normal neutrons
	EOS.kncrit = 0.0;  // neutrons are normal for kn<kncrit (to use this set gap=4)
	G.mdot=0.1;
	G.energy_deposited_outer=1.0;
	G.energy_deposited_inner=-1.0;
	G.rhob=1e14; 
	G.rhot=1e6;
	G.use_piecewise=0;
	G.Qrho=1e12;
	G.instant_heat = 0;
	G.angle_mu=-1.0;
	G.gpe=0;
	G.force_cooling_bc=0;
	G.extra_heating=0;
	G.use_my_envelope=0;
	G.yt=1e12;
	G.extra_Q=0.0;
	G.extra_y=0.0;
	G.output=1;
	G.deep_heating_factor=1.0;
	G.Lscale=1.0;
	G.Lmin=0.0;
	
	printf("============================================\n");
	printf("Reading input data from %s\n",fname);
	FILE *fp = fopen(fname,"r");
	char s1[100];
	char s[100];
	double x;				
	int commented=0;
	while (!feof(fp)) {   // we read the file line by line
		(void) fgets(s1,200,fp);		
		// ignoring lines that begin with \n (blank) or with # (comments)
		// or with $ (temperature profile)
		if (!strncmp(s1,"##",2)) commented = 1-commented;
		if (strncmp(s1,"#",1) && strncmp(s1,"\n",1) && strncmp(s1,">",1) && commented==0) {
			sscanf(s1,"%s\t%lg\n",s,&x);
			if (!strncmp(s,"Bfield",6)) EOS.B=x;
			if (!strncmp(s,"Tc",2)) G.Tc=x;
			if (!strncmp(s,"Tt",2)) G.Tt=x;
			if (!strncmp(s,"SFgap",5)) EOS.gap=(int) x;
			if (!strncmp(s,"ngrid",5)) G.N=(int) x;
			if (!strncmp(s,"kncrit",6)) EOS.kncrit=x;
			if (!strncmp(s,"mdot",4)) G.mdot=x;
			if (!strncmp(s,"mass",4)) mass=x;
			if (!strncmp(s,"gpe",3)) G.gpe=(int) x;
			if (!strncmp(s,"radius",6)) G.radius=x;
			if (!strncmp(s,"Edep",4)) G.energy_deposited_outer=x;
			if (!strncmp(s,"ytop",4)) G.yt=x;
			if (!strncmp(s,"Einner",6)) G.energy_deposited_inner=x;
			if (!strncmp(s,"Qimp",4)) EOS.Q=x;
			if (!strncmp(s,"Qrho",4)) G.Qrho=x;
			if (!strncmp(s,"rhob",4)) G.rhob=x;
			if (!strncmp(s,"rhot",4)) G.rhot=x;
			if (!strncmp(s,"precalc",7)) G.force_precalc=(int) x;
			if (!strncmp(s,"instant",7)) G.instant_heat=(int) x;
			if (!strncmp(s,"Qinner",6)) G.Qinner=x;
			if (!strncmp(s,"output",6)) G.output=x;
			if (!strncmp(s,"timetorun",9)) G.time_to_run=24.0*3600.0*x;			
			if (!strncmp(s,"toutburst",9)) G.outburst_duration=x;
			if (!strncmp(s,"piecewise",9)) G.use_piecewise=(int) x;
			if (!strncmp(s,"neutrinos",9)) G.nuflag=(int) x;
			if (!strncmp(s,"accreted",8)) EOS.accr=(int) x;
			if (!strncmp(s,"angle_mu",8)) G.angle_mu=x;
			if (!strncmp(s,"cooling_bc",10)) G.force_cooling_bc=(int) x;
			if (!strncmp(s,"extra_heating",13)) G.extra_heating=(int) x;
			if (!strncmp(s,"deep_heating_factor",19)) G.deep_heating_factor=x;
			if (!strncmp(s,"energy_slope",12)) G.energy_slope=x;
			if (!strncmp(s,"potek_eos",9)) EOS.use_potek_eos=(int) x;
			if (!strncmp(s,"envelope",8)) G.use_my_envelope=(int) x;
			if (!strncmp(s,"extra_Q",7)) G.extra_Q=x;
			if (!strncmp(s,"extra_y",7)) G.extra_y=x;
			if (!strncmp(s,"Lscale",6)) G.Lscale=x;
			if (!strncmp(s,"Lmin",4)) G.Lmin=x;
			if (!strncmp(s,"source",6)) {
				sscanf(s1,"%s\t%s\n",s,sourcename);
			}
		}
	}

	fclose(fp);

	if (G.yt < 10.0) G.yt=pow(10.0,G.yt);
	if (G.extra_y < 16.0) G.extra_y=pow(10.0,G.extra_y);

	if (G.Qinner == -1.0) G.Qinner=EOS.Q;
	if (G.energy_deposited_inner == -1.0) G.energy_deposited_inner = G.energy_deposited_outer;
	
	if (EOS.Q>=0.0) {   	// the Q values are assigned directly in 'calculate_vars'
		G.hardwireQ=1;
		printf("Using supplied Qimp values and HZ composition and heating.\n");
	} else {
		G.hardwireQ=0;
		printf("Using Qimp, composition, and heating from the crust model.\n");
	}

	// Include dipole angular dependence in B
	// The B provided is the polar magnetic field strength
	if (G.angle_mu >= 0.0) EOS.B*=sqrt(0.75*G.angle_mu*G.angle_mu+0.25);
	printf("Magnetic field set to B=%lg\n", EOS.B);
	
	set_ns_parameters(mass,G.radius,&G.g,&G.ZZ);

	G.outburst_duration /= G.ZZ;   // redshift the outburst duration (shorter time on the NS surface)
	
}

