// dear emacs, please treat this as -*- C++ -*-

#include <R.h>
#include <Rmath.h>
#include <Rdefines.h>
#include <Rinternals.h>
#include <R_ext/Rdynload.h>

#include "pomp_internal.h"

SEXP do_rmeasure (SEXP object, SEXP x, SEXP times, SEXP params, SEXP fun)
{
  int nprotect = 0;
  int mode = -1;
  int ntimes, nvars, npars, ncovars, nreps, nrepsx, nrepsp, nobs;
  SEXP Snames, Pnames, Cnames, Onames;
  SEXP statenames, paramnames, covarnames, obsnames;
  SEXP tvec, xvec, pvec, cvec;
  SEXP fn, fcall, rho, ans, nm;
  SEXP Y;
  int *dim, ndim[3], *op;
  int *sidx, *pidx, *cidx, *oidx;
  struct lookup_table covariate_table;
  pomp_measure_model_simulator *ff = NULL;

  PROTECT(times = AS_NUMERIC(times)); nprotect++;
  ntimes = length(times);
  if (ntimes < 1)
    error("rmeasure error: length('times') = 0, no work to do");

  PROTECT(x = as_state_array(x)); nprotect++;
  dim = INTEGER(GET_DIM(x));
  nvars = dim[0]; nrepsx = dim[1]; 

  if (ntimes != dim[2])
    error("rmeasure error: length of 'times' and 3rd dimension of 'x' do not agree");

  PROTECT(params = as_matrix(params)); nprotect++;
  dim = INTEGER(GET_DIM(params));
  npars = dim[0]; nrepsp = dim[1]; 

  nreps = (nrepsp > nrepsx) ? nrepsp : nrepsx;

  if ((nreps % nrepsp != 0) || (nreps % nrepsx != 0))
    error("rmeasure error: larger number of replicates is not a multiple of smaller");

  dim = INTEGER(GET_DIM(GET_SLOT(object,install("data"))));
  nobs = dim[0];

  PROTECT(Snames = GET_ROWNAMES(GET_DIMNAMES(x))); nprotect++;
  PROTECT(Pnames = GET_ROWNAMES(GET_DIMNAMES(params))); nprotect++;
  PROTECT(Cnames = GET_COLNAMES(GET_DIMNAMES(GET_SLOT(object,install("covar"))))); nprotect++;
  PROTECT(Onames = GET_ROWNAMES(GET_DIMNAMES(GET_SLOT(object,install("data"))))); nprotect++;
    
  // set up the covariate table
  covariate_table = make_covariate_table(object,&ncovars);

  // vector for interpolated covariates
  PROTECT(cvec = NEW_NUMERIC(ncovars)); nprotect++;
  SET_NAMES(cvec,Cnames);

  ndim[0] = nobs; ndim[1] = nreps; ndim[2] = ntimes;
  PROTECT(Y = makearray(3,ndim)); nprotect++; 
  setrownames(Y,Onames,3);

  // extract the user-defined function
  PROTECT(fn = unpack_pomp_fun(fun,&mode)); nprotect++;

  // extract 'userdata' as pairlist
  PROTECT(fcall = VectorToPairList(GET_SLOT(object,install("userdata")))); nprotect++;

  // first do setup
  switch (mode) {
  case 0:			// use R function

    PROTECT(tvec = NEW_NUMERIC(1)); nprotect++;
    PROTECT(xvec = NEW_NUMERIC(nvars)); nprotect++;
    PROTECT(pvec = NEW_NUMERIC(npars)); nprotect++;
    SET_NAMES(xvec,Snames);
    SET_NAMES(pvec,Pnames);

    // set up the function call
    PROTECT(fcall = LCONS(cvec,fcall)); nprotect++;
    SET_TAG(fcall,install("covars"));
    PROTECT(fcall = LCONS(pvec,fcall)); nprotect++;
    SET_TAG(fcall,install("params"));
    PROTECT(fcall = LCONS(tvec,fcall)); nprotect++;
    SET_TAG(fcall,install("t"));
    PROTECT(fcall = LCONS(xvec,fcall)); nprotect++;
    SET_TAG(fcall,install("x"));
    PROTECT(fcall = LCONS(fn,fcall)); nprotect++;

    // get the function's environment
    PROTECT(rho = (CLOENV(fn))); nprotect++;

    break;

  case 1:				// use native routine

    // construct state, parameter, covariate, observable indices
    oidx = INTEGER(PROTECT(name_index(Onames,object,"obsnames"))); nprotect++;
    sidx = INTEGER(PROTECT(name_index(Snames,object,"statenames"))); nprotect++;
    pidx = INTEGER(PROTECT(name_index(Pnames,object,"paramnames"))); nprotect++;
    cidx = INTEGER(PROTECT(name_index(Cnames,object,"covarnames"))); nprotect++;

    // address of native routine
    ff = (pomp_measure_model_simulator *) R_ExternalPtrAddr(fn);

    break;

  default:

    error("unrecognized 'mode' slot in 'rmeasure'");
    break;

  }

  // now do computations
  switch (mode) {

  case 0:			// R function

    {
      int first = 1;
      int use_names = 0;
      double *yt = REAL(Y);
      double *time = REAL(times);
      double *tp = REAL(tvec);
      double *cp = REAL(cvec);
      double *xp = REAL(xvec);
      double *pp = REAL(pvec);
      double *xs = REAL(x);
      double *ps = REAL(params);
      double *ys;
      int *posn;
      int i, j, k;

      for (k = 0; k < ntimes; k++, time++) { // loop over times

	R_CheckUserInterrupt();	// check for user interrupt

	*tp = *time;		// copy the time
	table_lookup(&covariate_table,*tp,cp,0); // interpolate the covariates
    
	for (j = 0; j < nreps; j++, yt += nobs) { // loop over replicates

	  // copy the states and parameters into place
	  for (i = 0; i < nvars; i++) xp[i] = xs[i+nvars*((j%nrepsx)+nrepsx*k)];
	  for (i = 0; i < npars; i++) pp[i] = ps[i+npars*(j%nrepsp)];
	
	  if (first) {
	    // evaluate the call
	    PROTECT(ans = eval(fcall,rho)); nprotect++;
	    if (LENGTH(ans) != nobs) {
	      error("user 'rmeasure' returns a vector of %d observables but %d are expected: compare 'data' slot?",
		    LENGTH(ans),nobs);
	    }

	    // get name information to fix potential alignment problems
	    PROTECT(nm = GET_NAMES(ans)); nprotect++;
	    use_names = !isNull(nm);
	    if (use_names) {		// match names against names from data slot
	      posn = INTEGER(PROTECT(matchnames(Onames,nm))); nprotect++;
	    } else {
	      posn = 0;
	    }

	    ys = REAL(AS_NUMERIC(ans));

	    first = 0;

	  } else {

	    ys = REAL(AS_NUMERIC(eval(fcall,rho)));

	  }

	  if (use_names) {
	    for (i = 0; i < nobs; i++) yt[posn[i]] = ys[i];
	  } else {
	    for (i = 0; i < nobs; i++) yt[i] = ys[i];
	  }
      
	}
      }
    }

    break;

  case 1: 			// native routine

    {
      double *yt = REAL(Y);
      double *time = REAL(times);
      double *xs = REAL(x);
      double *ps = REAL(params);
      double *cp = REAL(cvec);
      double *xp, *pp;
      int j, k;

      set_pomp_userdata(fcall);
      GetRNGstate();

      for (k = 0; k < ntimes; k++, time++) { // loop over times

	R_CheckUserInterrupt();	// check for user interrupt

	// interpolate the covar functions for the covariates
	table_lookup(&covariate_table,*time,cp,0);
    
	for (j = 0; j < nreps; j++, yt += nobs) { // loop over replicates
	
	  xp = &xs[nvars*((j%nrepsx)+nrepsx*k)];
	  pp = &ps[npars*(j%nrepsp)];
	
	  (*ff)(yt,xp,pp,oidx,sidx,pidx,cidx,ncovars,cp,*time);
      
	}
      }

      PutRNGstate();
      unset_pomp_userdata();
    }
    
    break;

  default:

    error("unrecognized 'mode' slot in 'rmeasure'");
    break;

  }

  UNPROTECT(nprotect);
  return Y;
}
