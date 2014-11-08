#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include "matrix.h"
#include "omxCsolnp.h"

double EMPTY;

bool DEBUG;
int flag, flag_L, flag_U, index_flag_L, index_flag_U, flag_NormgZ, flag_step, minr_rec;

Matrix ineqLB;
Matrix ineqUB;

Matrix LB;

Matrix UB;

Matrix control;

Matrix ind;

Matrix eqB;

Matrix resP;
double resLambda;
Matrix resMu;
Matrix resHessv;
Matrix resY;
Matrix sx_Matrix;

int ineqLBLength;
int ineqUBLength;
int LBLength;
int UBLength;
int parsLength;
int eqBLength;
int outerIter;
int mode_val;
int* mode = &mode_val;
Matrix inform;
Matrix hessi;
Matrix p_hess;
Matrix p_grad;
struct Param_Obj pfunv;


template <typename T1>
static Matrix subnp(Matrix pars,  double (*solFun)(Matrix, int*, int), Matrix (*solEqBFun)(int), Matrix (*myineqFun)(int),
		    Matrix yy,  Matrix ob,  Matrix hessv, double lambda,  Matrix vscale,  Eigen::ArrayBase<T1> &ctrl, int verbose);

Param_Obj solnp(Matrix solPars, double (*solFun)(Matrix, int*, int), Matrix solEqB, Matrix (*solEqBFun)(int), Matrix (*myineqFun)(int), Matrix solLB, Matrix solUB, Matrix solIneqUB, Matrix solIneqLB, Matrix solctrl, bool debugToggle, int verbose)
{
    int i;
    mode_val = 0;
	if(verbose >= 3){
		mxLog("solPars is: \n");
        for (i = 0; i < solPars.cols; i++) mxLog("%f", solPars.t[i]);
		mxLog("4th call is: \n");
		mxLog("%2f", solFun(solPars, mode, 0));
		mxLog("solEqB is: \n");
		for (i = 0; i < solEqB.cols; i++) mxLog("%f", solEqB.t[i]);
		mxLog("solEqBFun is: \n");
        for (i = 0; i < solEqB.cols; i++) mxLog("%f",solEqBFun(0).t[i]);
		mxLog("myineqFun is: \n");
        for (i = 0; i < solIneqLB.cols; i++) mxLog("%f", myineqFun(0).t[i]);
		mxLog("solLB is: \n");
        for (i = 0; i < solLB.cols; i++) mxLog("%f", solLB.t[i]);
		mxLog("solUB is: \n");
		for (i = 0; i < solUB.cols; i++) mxLog("%f", solUB.t[i]);
		mxLog("solIneqUB is: \n");
        for (i = 0; i < solIneqUB.cols; i++) mxLog("%f", solIneqUB.t[i]);
		mxLog("solIneqLB is: \n");
        for (i = 0; i < solIneqLB.cols; i++) mxLog("%f", solIneqLB.t[i]);
	}
    flag = 0; flag_L = 0; flag_U = 0;
    Matrix pars;
    double funv;
    double resultForTT;
	double solnp_nfn = 0;

    //time_t sec;
    //sec = time (NULL);
	ind = fill(11, 1, (double) 0.0, FALSE);
	DEBUG = debugToggle;
	EMPTY = -999999.0;
    int maxit_trace = 0;
	
	ineqLBLength = solIneqLB.cols;
	ineqUBLength = solIneqUB.cols;
	LBLength = solLB.cols;
	UBLength = solUB.cols;
	parsLength = solPars.cols;
	eqBLength = solEqB.cols;
	ineqLB.cols = solIneqLB.cols;
	ineqUB.cols = solIneqUB.cols;
    
	Matrix grad = fill(solPars.cols, 1, (double)0.0);
    //free(matrices.front().t);
    Matrix ineqLBx;
    Matrix ineqUBx;
    Matrix pb_cont;
    Matrix difference1, difference2, tmpv, testMin, firstCopied, subnp_ctrl, subsetMat, temp2, temp1, temp, funv_mat, tempdf, firstPart, copied, subsetOne, subsetTwo, subsetThree, diff1, diff2, copyValues, diff, llist, tempTTVals, searchD;
    
    pars = duplicateIt(solPars);

	eqB = duplicateIt(solEqB, FALSE);
    
	control = duplicateIt(solctrl, FALSE);
    
	if(verbose >= 2){
		mxLog("control is: \n");
		for (i = 0; i < control.cols; i++) mxLog("%f",control.t[i]);
	}
    
	if (ineqLB.cols > 1){
		ineqLB = duplicateIt(solIneqLB, FALSE);
		if (ineqUB.cols < 1){
			ineqUB = fill(ineqLB.cols, 1, (double) DBL_MAX/2, FALSE);
            
		}
	}
	else
    {
        ineqLB = fill(1, 1, (double) 0.0, FALSE);
        M(ineqLB, 0, 0) = EMPTY;
    }
	
	if (ineqUB.cols > 1){
		ineqUB = duplicateIt(solIneqUB, FALSE);
		if (ineqLB.cols < 1){
			ineqLB = fill(ineqUB.cols, 1, (double) -DBL_MAX/2, FALSE);
		}
	}
	else
    {
        ineqUB = fill(1, 1, (double) 0.0, FALSE);
        M(ineqUB, 0, 0) = EMPTY;
    }
    
    
	if (LBLength > 1){
		LB = duplicateIt(solLB, FALSE);
		if (UB.cols < 1){
			UB = fill(LB.cols, 1, (double) DBL_MAX/2, FALSE);
		}
	}
	else
    {
        LB = fill(1, 1, (double) 0.0, FALSE);
        M(LB, 0, 0) = EMPTY;
    }
	
	if (UBLength > 1){
		UB = duplicateIt(solUB, FALSE);
		if (LB.cols < 1){
			LB = fill(UB.cols, 1, (double) -DBL_MAX/2, FALSE);
		}
        
	}
	else{
		UB = fill(1, 1, (double) 0.0, FALSE);
		M(UB, 0, 0) = EMPTY;
	}
    
    if(LB.cols > 1)
    {
        for (int i = 0; i < LB.cols; i++)
        {
            if (M(LB, i, 0) < M(pars, i, 0) && M(UB, i, 0) > M(pars, i, 0))
            { continue;  }
            else if (M(pars, i, 0) <= M(LB, i, 0))
            {   inform = fill(1, 1, 9);
                flag_L = 1;
                index_flag_L = i;
                M(pars, i, 0) = M(LB, i, 0) + M(control, 4, 0); 
            }
            else if (M(pars, i, 0) >= M(UB, i, 0))
            {   inform = fill(1, 1, 9);
                flag_U = 1;
                index_flag_U = i;
                M(pars, i, 0) = M(UB, i, 0) - M(control, 4, 0);
            }
        }
    }
    
	if (verbose >= 2){
		mxLog("LB is: \n");
		for (i = 0; i < LB.cols; i++) mxLog("%f",LB.t[i]);
        
		mxLog("UB is: \n");
		for (i = 0; i < UB.cols; i++) mxLog("%f",UB.t[i]);
        
        mxLog("pars is: \n");
		for (i = 0; i < pars.cols; i++) mxLog("%f",pars.t[i]);
    }
        
	int np = pars.cols;
    inform = new_matrix(1, 1);
    hessi = new_matrix(np*np, 1);
    p_hess = new_matrix(np+(np*np), 1);
    p_grad = new_matrix(np+(np*np)+np, 1);
	// [0] Rf_length of pars
	// [1] has function gradient?
	// [2] has hessian?
	// [3] has ineq?
	// [4] ineq Rf_length
	// [5] has jacobian (inequality)
	// [6] has eq?
	// [7] eq Rf_length
	// [8] has jacobian (equality)
	// [9] has upper / lower bounds
	// [10] has either lower/upper bounds or ineq
	
	M(ind, 0, 0) = pars.cols;
	
	if (M(LB, 0, 0) != EMPTY || M(UB, 0, 0) != EMPTY){
		M(ind, 9, 0) = 1;
	}
	
	// does not have a function gradient (currently not supported in Rsolnp)
    M(ind, 1, 0) = 0;
	//# do function checks and return starting value
    
    funv = solFun(pars, mode, verbose);
    
	// does not have a hessian (currently not supported in Rsolnp)
	M(ind, 2, 0) = 0;
	
	// do inequality checks and return starting values
	int nineq;
	Matrix ineqx0 = fill(ineqLB.cols, 1, (double)0.0);
    
	Matrix ineqv = myineqFun(verbose);
    
	if ( M(ineqv, 0, 0) != EMPTY){
		
		M(ind, 3, 0) = 1;
		nineq = ineqLB.cols;
		
		M(ind, 4, 0) = nineq;
		
		// check for infitnites/nans
        
        ineqLBx = ineqLB;
		ineqUBx = ineqUB;
        
		int i;
		for (i = 0; i<ineqLBx.cols; i++)
        {
            if (M(ineqLBx,i,0) <= -99999999.0){ 
                M(ineqLBx,i,0) = -1.0 * (1e10);
            }
            if (M(ineqUBx,i,0) >= DBL_MAX){
                M(ineqUBx,i,0) = 1e10;
            }
        }
        
        
		for (i = 0; i < ineqLBx.cols; i++)
        {
            M(ineqx0, i, 0) = (M(ineqLBx, i, 0) + M(ineqUBx, i, 0)) / 2.0;
        }
        
		// no jacobian
		M(ind, 5, 0) = 0;
        
	}
	else{
		
		M(ineqv, 0, 0) = EMPTY;
		M(ind, 3, 0) = 0;
		nineq = 0;
		M(ind, 4, 0) = 0;
		M(ind, 5, 0) = 0;
		M(ineqx0, 0, 0) = EMPTY;
	}
    
	int neq;
	Matrix eqv = solEqBFun(verbose);
    
	if( M(eqv, 0, 0) != EMPTY){
		M(ind, 6, 0) = 1;
		neq = eqB.cols;
		M(ind, 7, 0) = neq;
		M(ind, 8, 0) = 0;
	} else{
		M(eqv, 0, 0) = EMPTY;
		M(ind, 6, 0) = 0;
		neq = 0;
		M(ind, 7, 0) = 0;
		M(ind, 8, 0) = 0;
	}
	if ( (M(ind, 9, 0) > 0) || (M(ind, 3, 0) > 0) ){
		M(ind, 10, 0) = 1;
	}
	
	if (verbose >= 2){
		mxLog("ind is: \n");
		for (i = 0; i < ind.cols; i++) mxLog("%f",ind.t[i]);
	}
    
    
	Matrix pb;
    
    
	if(M(ind, 10, 0))
    {   if((M(LB, 0, 0) != EMPTY) && (M(ineqLB, 0, 0) != EMPTY))
        {   pb = fill(2, nineq, (double)0.0, FALSE);
		setColumnInplace(pb, ineqLB, 0);
		setColumnInplace(pb, ineqUB, 1);
            pb_cont = fill(2, np, (double)0.0);
            setColumnInplace(pb_cont, LB, 0);
            setColumnInplace(pb_cont, UB, 1);
            pb = duplicateIt(transpose(copy(transpose(pb), transpose(pb_cont))), FALSE);
        }
        else if((M(LB, 0, 0) == EMPTY) && (M(ineqLB, 0, 0) != EMPTY))
        {
            pb = fill(2, nineq, (double)0.0, FALSE);
            setColumnInplace(pb, ineqLB, 0);
            setColumnInplace(pb, ineqUB, 1);
        }
        else if((M(LB, 0, 0) != EMPTY) && (M(ineqLB, 0, 0) == EMPTY))
        {
            pb = fill(2, np, (double)0.0, FALSE);
            setColumnInplace(pb, LB, 0);
            setColumnInplace(pb, UB, 1);
        }
    }
    
	else    {pb = fill(1, 1, EMPTY, FALSE);}
    
	double rho   = M(control, 0, 0);
	int maxit = M(control, 1, 0);
	int minit = M(control, 2, 0);
	double delta = M(control, 3, 0);
	double tol   = M(control, 4, 0);
	double trace = M(control, 5, 0);
	
	int tc = nineq + neq;
    
	double j = funv;
    Matrix jh = fill(1, 1, funv, FALSE);
	Matrix tt = fill(1, 3, (double)0.0, FALSE);
    
	Matrix lambda;
	Matrix constraint;

	if (tc > 0){
		lambda = fill(1, tc, (double)0.0);
        
		if (M(ineqv, 0, 0) != EMPTY){
			if(M(eqv,0,0) != EMPTY)
            {
                constraint = copy(eqv, ineqv);
            }
			else{
				constraint = duplicateIt(ineqv);
			}
		}
		else    {constraint = duplicateIt(eqv);}
        
		if( M(ind, 3, 0) > 0 ) {
			
			// 	tmpv = cbind(constraint[ (neq[0]):(tc[0]-1) ] - .ineqLB, .ineqUB - constraint[ (neq + 1):tc ] )
            difference1 = subtract(subset(constraint, 0, neq, tc-1), ineqLB);
            difference2 = subtract(ineqUB, subset(constraint, 0, neq, tc-1));
            tmpv = fill(2, nineq, (double)0.0);
            setColumnInplace(tmpv, difference1, 0);
            setColumnInplace(tmpv, difference2, 1);
            testMin = rowWiseMin(tmpv);
            
			if( allGreaterThan(testMin, 0) ) {
				ineqx0 = subset(constraint, 0, neq, tc-1);
			}
            
			copyIntoInplace(constraint, subtract(subset(constraint, 0, neq, tc-1), ineqx0), 0, neq, tc-1);
		}
        
		M(tt, 0, 1) = vnorm(constraint);
		double zeroCheck = M(tt, 0, 1) - (10 * tol);
		if( max(zeroCheck, nineq) <= 0 ) {
			rho = 0;
		}
	} // end if tc > 0
	else {
		lambda = fill(1, 1, (double)0.0);
	}
    
    
	Matrix tempv;
	Matrix p;
	
	if ( M(ineqx0, 0, 0) != EMPTY){
		p = copy(ineqx0, pars, FALSE);
	}
	else{
		p = duplicateIt(pars, FALSE);
	}
    
	Matrix hessv = diag(fill((np+nineq), 1, (double)1.0, FALSE));
    
	double mu = np;
	
	int solnp_iter = 0;
    
	Matrix ob;
	Matrix funvMatrix = fill(1, 1, funv);
    
	if ( M(ineqv, 0, 0) != EMPTY){
		if(M(eqv,0,0) != EMPTY){
			Matrix firstCopied = copy(funvMatrix, eqv);
			ob = copy(firstCopied, ineqv);
		}
		else{
			ob = copy(funvMatrix, ineqv);
		}
        
	}
	else if (M(eqv,0,0) != EMPTY){
		ob = copy(funvMatrix, eqv);
	}
	else ob = funvMatrix;
    
    
	if(verbose >= 3){
		mxLog("ob is: \n");
		for (i = 0; i < ob.cols; i++) mxLog("%f",ob.t[i]);
	}
    
	Matrix vscale;
    
	while(solnp_iter < maxit){
		solnp_iter = solnp_iter + 1;
		outerIter = solnp_iter;
		Eigen::Array<double, 5, 1> subnp_ctrl;
		subnp_ctrl[0] = rho;
		subnp_ctrl[1] = minit;
		subnp_ctrl[2] = delta;
		subnp_ctrl[3] = tol;
		subnp_ctrl[4] = trace;
        
		if ( M(ind, 6, 0) > 0){
			Matrix subsetMat = subset(ob, 0, 1, neq);
			double max = matrixMaxAbs(subsetMat);
            
			Matrix temp2 = fill(neq, 1, max);
			Matrix temp1 = fill(1, 1, M(ob, 0, 0));
			vscale = copy(temp1, temp2);
            
		}
		else{
			vscale = fill(1, 1, (double)1.0);
		}
		if ( M(ind, 10, 0) <= 0){
			vscale = copy(vscale, p);
		}
		else{
			vscale = copy(vscale, fill(p.cols, 1, (double)1.0));
		}
		vscale = minMaxAbs(vscale, tol);
        
		if (verbose >= 1){
			mxLog("------------------------CALLING SUBNP------------------------");
			mxLog("p information: ");
			for (i = 0; i < p.cols; i++) mxLog("%f",p.t[i]);
			mxLog("lambda information: ");
			for (i = 0; i < lambda.cols; i++) mxLog("%f",lambda.t[i]);
			mxLog("ob information: ");
            for (i = 0; i < ob.cols; i++) mxLog("%f",ob.t[i]);
			mxLog("hessv information: ");
            for (i = 0; i < hessv.cols*hessv.cols; i++) mxLog("%f",hessv.t[i]);
			mxLog("mu information: ");
			mxLog("%2f", mu);
			mxLog("vscale information: ");
            for (i = 0; i < vscale.cols; i++) mxLog("%f",vscale.t[i]);
			mxLog("subnp_ctrl information: ");
			for (i = 0; i < subnp_ctrl.size(); i++) mxLog("%f",subnp_ctrl[i]);
			mxLog("------------------------END CALLING SUBNP------------------------");
        }
        
        if (*mode == -1)
        {
            M(inform, 0, 0) = 0;
            hessi = MatrixToVector(fill(np, np, (double)0.0));
            p_hess = copy(p, hessi);
            p_grad = copy(p_hess, grad);
            pfunv.parameter = copy(p_grad, inform, FALSE);
            pfunv.objValue = funv;
            return pfunv;
        }

		grad = subnp(p, solFun, solEqBFun, myineqFun, lambda, ob, hessv, mu, vscale, subnp_ctrl, verbose);
        
        if (flag == 1)
        {
            p = duplicateIt(resP);
            funv = solFun(p, mode, verbose);
            funvMatrix = fill(1, 1, funv);
            eqv = solEqBFun(verbose);
            ineqv = myineqFun(verbose);
            if ( M(ineqv, 0, 0) != EMPTY)
            {
                if(M(eqv,0,0) != EMPTY)
                {
                    Matrix firstCopied = copy(funvMatrix, eqv);
                    ob = copy(firstCopied, ineqv);
                }
                else{
                    ob = copy(funvMatrix, ineqv);
                }
            }
            else if (M(eqv,0,0) != EMPTY){
                ob = copy(funvMatrix, eqv);
            }
            else ob = funvMatrix;

            if ( M(ind, 6, 0) > 0){
                Matrix subsetMat = subset(ob, 0, 1, neq);
                double max = matrixMaxAbs(subsetMat);
                
                Matrix temp2 = fill(neq, 1, max);
                Matrix temp1 = fill(1, 1, M(ob, 0, 0));
                vscale = copy(temp1, temp2);
                
            }
            else{
                vscale = fill(1, 1, (double)1.0);
            }
            if ( M(ind, 10, 0) <= 0){
                vscale = copy(vscale, p);
            }
            else{
                vscale = copy(vscale, fill(p.cols, 1, (double)1.0));
            }
            vscale = minMaxAbs(vscale, tol);
            lambda = duplicateIt(resY);
            hessv = duplicateIt(resHessv);
            mu = resLambda;
            grad = subnp(p, solFun, solEqBFun, myineqFun, lambda, ob, hessv, mu, vscale, subnp_ctrl, verbose);
        }
		p = duplicateIt(resP);
        
		lambda = duplicateIt(resY);
        
		hessv = duplicateIt(resHessv);
        
		mu = resLambda;
        
		Matrix temp = subset(p, 0, nineq, (nineq+np-1));
        
        funv = solFun(temp, mode, verbose);
        if (*mode == -1)
        {
            M(inform, 0, 0) = 0;
            hessi = MatrixToVector(hessv);
            p_hess = copy(p, hessi);
            p_grad = copy(p_hess, grad);
            pfunv.parameter = copy(p_grad, inform, FALSE);
            pfunv.objValue = funv;
            return pfunv;
        }

		solnp_nfn = solnp_nfn + 1;
        
		//Matrix funv_mat = fill(1, 1, funv);
		//Matrix tempdf = copy(temp, funv_mat);
		eqv = solEqBFun(verbose);
        
		ineqv = myineqFun(verbose);
        
		Matrix firstPart, copied;
		if (M(ineqv, 0, 0) != EMPTY){
			if(M(eqv,0,0) != EMPTY){
				copied = copy(fill(1, 1, funv), eqv);
				ob = copy(copied, ineqv);
                
			}
			else{
				ob = copy(fill(1, 1, funv), ineqv);
			}
		}
		else if (M(eqv,0,0) != EMPTY){
			ob = copy(fill(1, 1, funv), eqv);
		}
		else ob = fill(1, 1, funv);
        
        if (verbose >= 1){
            mxLog("j2 in while: \n");
            mxLog("%.20f", j);
            mxLog("M(ob2, 0, 0) \n");
            mxLog("%.20f", M(ob, 0, 0));
        }
        
        resultForTT = (j - M(ob, 0, 0)) / max(fabs(M(ob, 0, 0)), 1.0);
		M(tt, 0, 0) = resultForTT;
        if (verbose >= 1){
            mxLog("resultForTT \n");
            mxLog("%.20f", resultForTT);
        }
		j = M(ob, 0, 0);
        
		if (tc > 0){
			// constraint = ob[ 2:(tc + 1) ]
			constraint = subset(ob, 0, 1, tc);
            
			if ( M(ind, 3, 0) > 0.5){
				//tempv = rbind( constraint[ (neq + 1):tc ] - pb[ 1:nineq, 1 ], pb[ 1:nineq, 2 ] - constraint[ (neq + 1):tc ] )
				Matrix subsetOne = subset(constraint, 0, neq, tc-1);
				Matrix subsetTwo = subset(getColumn(pb, 0), 0, 0, nineq-1);
				Matrix subsetThree = subset(getColumn(pb, 1), 0, 0, nineq-1);
				Matrix diff1 = subtract(subsetOne, subsetTwo);
				Matrix diff2 = subtract(subsetThree, subsetOne);
				Matrix tempv = fill(nineq, 2, (double)0.0);
				setRowInplace(tempv, 0, diff1);
				setRowInplace(tempv, 1, diff2);
                
				if (findMin(tempv) > 0){
					Matrix copyValues = subset(constraint, 0, neq, tc-1);
					copyIntoInplace(p, copyValues, 0, 0, nineq-1);
				}
                Matrix diff = subtract(subset(constraint, 0, neq, tc-1),
                                       subset(p, 0, 0, nineq-1));
                
                copyIntoInplace(constraint, diff, 0, neq, tc-1);
			} // end if (ind[0][3] > 0.5){
            
			M(tt, 0, 2) = vnorm(constraint);
            
            
			if ( M(tt, 0, 2) < (10 *tol)){
				rho =0;
				mu = min(mu, tol);
			}
            
			if ( M(tt, 0, 2) < (5 * M(tt, 0, 1))){
				rho = rho/5;
			}
            
			if ( M(tt, 0, 2) > (10 * M(tt, 0, 1))){
				rho = 5 * max(rho, sqrt(tol));
			}
            
            llist = fill(2, 1, (double)0.0);
            
			M(llist, 0, 0) = tol + M(tt, 0, 0);
			M(llist, 1, 0) = M(tt, 0, 1) - M(tt, 0, 2);
            
            
			if (findMax(llist) <= 0){
				//hessv = diag( diag ( hessv ) )
				/** DOESN'T AFFECT US NOW EVENTUALLY IT WILL **/
				lambda = fill(1, 1, (double)0.0);
				hessv = diag(diag2(hessv));
			}
            
			M(tt, 0, 1) = M(tt, 0, 2);
            
		} // end if (tc > 0){
        
        if (verbose >= 3){
            mxLog("tt is \n");
            for (i = 0; i < tt.cols; i++) mxLog("%f",tt.t[i]);
        }
		double vnormValue;
		Matrix tempTTVals = fill(2, 1, (double)0.0);
		M(tempTTVals, 0, 0) = M(tt, 0, 0);
		M(tempTTVals, 1, 0) = M(tt, 0, 1);
        
		vnormValue = vnorm(tempTTVals);
        
		if (vnormValue <= tol){
            maxit_trace = maxit;
			maxit = solnp_iter;
		}
        
        if (verbose >= 3)
        {
            mxLog("vnormValue in while \n");
            mxLog("%.20f", vnormValue);
		}
        jh = copy(jh, fill(1, 1, j), FALSE);
        
	} // end while(solnp_iter < maxit){
    
    
	if ( M(ind, 3, 0) > 0.5){
		ineqx0 = subset(p, 0, 0, nineq-1);
	}
	p = subset(p, 0, nineq, (nineq + np -1));
    
	if (false){
		/* TODO: LIST ERROR MESSAGES HERE */
	}
	else{
		double vnormValue;
		Matrix searchD;
        double iterateConverge;
        double iterateConvergeCond;
		Matrix tempTTVals = fill(2, 1, (double) 0.0);
		M(tempTTVals, 0, 0) = M(tt, 0, 0);
		M(tempTTVals, 1, 0) = M(tt, 0, 1);
		vnormValue = vnorm(tempTTVals);
        if (verbose >= 1)
        {
            mxLog("vnormValue \n");
            mxLog("%.20f", vnormValue);
        }
        searchD = duplicateIt(sx_Matrix);
         if (verbose >= 3){
             mxLog("searchD is: \n");
             for (i = 0; i < searchD.cols; i++) mxLog("%f",searchD.t[i]);
         }
         iterateConverge = delta * pow(vnorm(searchD),(double)2.0);
         if (verbose >= 1)
         {
             mxLog("vnorm(searchD) is: \n");
             mxLog("%.20f", vnorm(searchD));
             mxLog("iterateConverge is: \n");
             mxLog("%.20f", iterateConverge);
         }
        iterateConvergeCond = sqrt(tol) * ((double)1.0 + pow(vnorm(p), (double)2.0));
        if (verbose >= 1)
         {   mxLog("iterateConvergeCond is: \n");
             mxLog("%.20f", iterateConvergeCond);
         }
        
        if (vnormValue <= tol && flag_NormgZ == 1 && minr_rec == 1 && flag_step == 1){
            if (iterateConverge <= iterateConvergeCond){
                if (verbose >= 1){
                    mxLog("The solution converged in %d iterations. It is:", solnp_iter);}
                inform = fill(1, 1, 0);
            }
            else {
                if (verbose >= 1){
                    mxLog("The final iterate x satisfies the optimality conditions to the accuracy requested, but the sequence of iterates has not yet converged. CSOLNP was terminated because no further improvement could be made in the merit function.");}
                inform = fill(1, 1, 1);
                
            }
        }
        else{
            if (solnp_iter == maxit_trace)
            {
                if (verbose >= 1){
                    mxLog("Exiting after maximum number of iterations. Tolerance not achieved\n");}
                inform = fill(1, 1, 4);
            }
            else
            {
                if (verbose >= 1){
                    mxLog("Solution failed to converge.");
                }
                inform = fill(1, 1, 6);
            }
        }
    }
	
    hessi = fill(hessv.cols*hessv.cols,1 , (double)0.0);
    
	int ii;
	int ind_hess = 0;
	for (i = 0; i < hessv.cols; i++)
    {
        for (ii = 0; ii < hessv.rows; ii++)
        {
            M(hessi, ind_hess, 0) = M(hessv, i, ii);
            ind_hess = ind_hess + 1;
        }
    }
    
    if (verbose >= 1){
        mxLog("inform in subnp is: \n");
        for (i = 0; i < inform.cols; i++) mxLog("%f",inform.t[i]);
    }
    
    //hessi = MatrixToVector(hessv);
    p_hess = copy(p, hessi);
	p_grad = copy(p_hess, grad);
	pfunv.parameter = copy(p_grad, inform, FALSE);
    pfunv.objValue = funv;
    
    freeMatrices();
    
	return pfunv;
    
}

template <typename T1>
static Matrix subnp(Matrix pars, double (*solFun)(Matrix, int*, int), Matrix (*solEqBFun)(int) ,  Matrix(*myineqFun)(int),
             Matrix yy,  Matrix ob,  Matrix hessv, double lambda,  Matrix vscale,  Eigen::ArrayBase<T1> &ctrl, int verbose)
{

    if (verbose >= 3)
    {
        mxLog("pars in subnp is: \n");
        for (int i = 0; i < pars.cols; i++) mxLog("%f",pars.t[i]);
    }
	int yyRows = yy.rows;
	//int yyCols = yy.cols;
    double j;
    
    double rho   = ctrl[0];
    int maxit = ctrl[1];
    double delta = ctrl[2];
    double tol =   ctrl[3];
    
	int neq =  (int)M(ind, 7, 0);
	int nineq = (int)M(ind, 4, 0);
	int np = (int)M(ind, 0, 0);
    
	double ch = 1;
	Matrix argum;
	Matrix y;
    
	if (verbose >= 2){
		mxLog("ind inside subnp is: \n");
        for (int i = 0; i < ind.cols; i++) mxLog("%f",ind.t[i]);
	}
    
	Eigen::Array<double, 3, 1> alp;
	alp.setZero();
    
	int nc = neq + nineq;
	int npic = np + nineq;
	
	Matrix p0 = duplicateIt(pars);
    
    if (verbose >= 3)
    {
        mxLog("p0 p0 is: \n");
        for (int i = 0; i < p0.cols; i++) mxLog("%f",p0.t[i]);
    }
    
    
	Matrix pb;
	Matrix col1_pb;
    Matrix pb_cont;
    
	if(M(ind, 10, 0))
    {
        if((M(LB, 0, 0) != EMPTY) && (M(ineqLB, 0, 0) != EMPTY))
        {
		pb = fill(2, nineq, (double)0.0);
		setColumnInplace(pb, ineqLB, 0);
		setColumnInplace(pb, ineqUB, 1);
            
            pb_cont = fill(2, np, (double)0.0);
            setColumnInplace(pb_cont, LB, 0);
            setColumnInplace(pb_cont, UB, 1);
            
            pb = transpose(copy(transpose(pb), transpose(pb_cont)));
            
        }
        else if((M(LB, 0, 0) == EMPTY) && (M(ineqLB, 0, 0) != EMPTY))
        {
            pb = fill(2, nineq, (double)0.0);
            setColumnInplace(pb, ineqLB, 0);
            setColumnInplace(pb, ineqUB, 1);
            
        }
        else if((M(LB, 0, 0) != EMPTY) && (M(ineqLB, 0, 0) == EMPTY))
        {
            pb = fill(2, np, (double)0.0);
            setColumnInplace(pb, LB, 0);
            setColumnInplace(pb, UB, 1);
            
        }
    }
	else{
		pb = fill(1,1,EMPTY);
	}
    
	if (verbose >= 3){
		mxLog("pb is: \n");
		for (int i = 0; i < pb.cols; i++) mxLog("%f",pb.t[i]);
	}
    
	Eigen::Array<double, 3, 1> sob;
	sob.setZero();
	Matrix ptt;
	
	//Matrix yyMatrix = duplicateIt(yy);
    
	ob = divide(ob, subset(vscale, 0, 0, nc));
    
	p0 = divide(p0, subset(vscale, 0, (neq+1), (nc + np)));
    
    if (verbose >= 3){
        mxLog("p0 is: \n");
        for (int i = 0; i < p0.cols; i++) mxLog("%f",p0.t[i]);
    }
    
	int mm = -1;
	if (M(ind, 10, 0) > 0){
		if (M(ind, 9, 0) <= 0){
			mm = nineq;
		}
		else{
			mm=npic;
		}
		Matrix vscaleSubset = subset(vscale, 0, neq+1, neq+mm);
		//double vscaleSubsetLength = (neq+mm) - (neq+1) + 1;
		Matrix vscaleTwice = fill(pb.cols, pb.rows, (double)0.0);
		setColumnInplace(vscaleTwice, vscaleSubset, 0);
		setColumnInplace(vscaleTwice, vscaleSubset, 1);
        
		if (M(pb, 0, 0) != EMPTY){
			pb = divide(pb, vscaleTwice);
		}
	} // end if (ind [0][10] > 0)
    
	if (verbose >= 3){
		mxLog("pb is: \n");
		for (int i = 0; i < pb.cols; i++) mxLog("%f",pb.t[i]);
	}
    
	// scale the lagrange multipliers and the Hessian
	if( nc > 0) {
		// yy [total constraints = nineq + neq]
		// scale here is [tc] and dot multiplied by yy
		//yy = vscale[ 2:(nc + 1) ] * yy / vscale[ 1 ]
        yy = multiply(transpose(subset(vscale, 0, 1, nc)), yy);
		yy = divideByScalar2D(yy, M(vscale,0,0));
	}

	// hessv [ (np+nineq) x (np+nineq) ]
	// hessv = hessv * (vscale[ (neq + 2):(nc + np + 1) ] %*% t(vscale[ (neq + 2):(nc + np + 1)]) ) / vscale[ 1 ]
    
	Matrix vscaleSubset = subset(vscale, 0, (neq+1), (nc + np));
	Matrix transDotProduct = transposeDP(vscaleSubset);
	hessv = divideByScalar2D(multiply(hessv, transDotProduct), M(vscale, 0, 0));

	j = M(ob, 0, 0);
    if (verbose >= 3){
        mxLog("j j is: \n");
        mxLog("%2f", j);
    }
	Matrix a;
    
	if( M(ind, 3, 0) > 0){
		if ( M(ind, 6, 0) <= 0)
        {
			// arrays, rows, cols
			Matrix onesMatrix = fill(nineq, 1, (double)-1.0);
			Matrix negDiag = diag(onesMatrix);
			Matrix zeroMatrix = fill(np, nineq, (double)0.0);
			// a = cbind( -diag(nineq), matrix(0, ncol = np, nrow = nineq) )
			a = copy(negDiag, zeroMatrix);
		}
		else{
			// [ (neq+nineq) x (nineq+np)]
			//a = rbind( cbind( 0 * .ones(neq, nineq), matrix(0, ncol = np, nrow = neq) ),
			//      cbind( -diag(nineq), matrix(0, ncol = np, nrow = nineq) ) )
            
			Matrix zeroMatrix = fill(np, nineq, (double)0.0);
			Matrix firstHalf = copy(fill(nineq, neq, (double)0.0), fill(np, neq, (double)0.0));
			Matrix onesMatrix = fill(nineq, 1, (double)-1.0);
			Matrix negDiag = diag(onesMatrix);
			Matrix secondHalf = copy(negDiag, zeroMatrix);
			a = transpose(copy(transpose(firstHalf), transpose(secondHalf)));
		}
	}	// end 	if(ind[0][3] > 0){
    
	if ( (M(ind, 6, 0) > 0) && M(ind, 3, 0) <= 0 ){
		a = fill(np, neq, (double)0.0);
	}
	if (M(ind, 6, 0)<= 0 && (M(ind, 3, 0) <= 0)){
		a = fill(np, 1, (double)0.0);
	}
	Matrix g = fill(npic, 1, (double)0.0, FALSE);
	Matrix p = subset(p0, 0, 0, (npic-1));
    
	Matrix dx;
	Matrix b;
	double funv;
	Matrix eqv;
	Matrix ineqv;
	Matrix tmpv;
	Matrix constraint;
	Matrix gap;
    
	int solnp_nfn = 0;
	double go, reduce = 1e-300;
	int minit;
	double lambdaValue = lambda;

	if (nc > 0) {
		constraint = subset(ob, 0, 1, nc);

		int i;
        
		for (i=0; i<np; i++){
			int index = nineq + i;
			M(p0, index, 0) = M(p0, index, 0) + delta;
			tmpv = multiply(subset(p0, 0, nineq, (npic-1)), subset(vscale, 0, (nc+1), (nc+np)));
            
			if (verbose >= 2){
				mxLog("7th call is \n");
			}
			funv = solFun(tmpv, mode, verbose);
            
			eqv = solEqBFun(verbose);

			ineqv = myineqFun(verbose);
            
			solnp_nfn = solnp_nfn + 1;
			Matrix firstPart;
			Matrix firstPartt;
			Matrix secondPart;
            
			if (M(ineqv,0,0) != EMPTY){
				if(M(eqv,0,0) != EMPTY)
                {
                    firstPartt = copy(fill(1, 1, funv), eqv);
                    firstPart = copy(firstPartt, ineqv);
                }
				else{
					firstPart = copy(fill(1, 1, funv), ineqv);
				}
			}
			else if (M(eqv,0,0) != EMPTY){
				firstPart = copy(fill(1, 1, funv), eqv);
			}
			else firstPart = fill(1, 1, funv);
			secondPart = subset(vscale, 0, 0, nc);
			ob = divide(firstPart, secondPart);
            
			M(g, index, 0) = (M(ob, 0, 0)-j) / delta;
            /*if (M(ind, 1, 0) == 1)
            {   if (M(deriv, index, 0) - M(g, index, 0) > 0.001)
                {   printf("deriv not equal to g.\n");}
            }*/
            
			if (verbose >= 3){
				mxLog("g is: \n");
				for (int ilog = 0; ilog < g.cols; ilog++) mxLog("%f",g.t[ilog]);
                mxLog("a is: \n");
				for (int ilog = 0; ilog < a.cols; ilog++) mxLog("%f",a.t[ilog]);
            
			}
			Matrix colValues = subtract(subset(ob, 0, 1, nc), constraint);

			colValues = divideByScalar2D(colValues, delta);
			setColumnInplace(a, colValues, index);
			M(p0, index, 0) = M(p0, index, 0) - delta;
		} // end for (int i=0; i<np, i++){
        
        if (*mode == -1)
        {
            funv = 1e24;
            *mode = 0;
        }

		if(M(ind, 3, 0) > 0){
			//constraint[ (neq + 1):(neq + nineq) ] = constraint[ (neq + 1):(neq + nineq) ] - p0[ 1:nineq ]
			Matrix firstPart, secondPart;
			firstPart  = subset(constraint, 0, neq, (neq+nineq-1));
			secondPart = subset(p0, 0, 0, (nineq-1));
			Matrix values = subtract(firstPart, secondPart);
            
			copyIntoInplace(constraint, values, 0, neq, (neq+nineq-1));
		}
        
        if (false && solvecond(a) > 1/DBL_EPSILON) { // this can't be the cheapest way to check TODO
        Rf_error("Redundant constraints were found. Poor intermediate results may result. "
            "Remove redundant constraints and re-OPTIMIZE.");
        }
		
        b = fill(nc, 1, (double)0.0);
        
        Matrix timess_a_p0 = timess(a, transpose(p0));
        //  b [nc,1]
		b = subtract(transpose(timess_a_p0), constraint);

		ch = -1;
		alp[0] = tol - matrixMaxAbs(constraint);
		if ( alp[0] <= 0){
            
			ch = 1;
            
			if ( M(ind, 10, 0) < 0.5){
				Matrix dotProd = transposeDotProduct(a); //Mahsa: this is equal to "a %*% t(a)"
				Matrix solution = solve(dotProd, constraint);

				p0 = subtract(p0, matrixDotProduct(transpose(a), solution));

				alp[0] = 1;
			}
            
		} // end if (alp[0][0] <= 0){
        
        
		if (alp[0] <= 0){
            
			int npic_int = npic;
			p0 = copy(p0, fill(1, 1, (double)1.0));
			a = copy(a, transpose(multiplyByScalar2D(constraint, -1.0)));

			Matrix cx = copy(fill(npic, 1, (double)0.0), fill(1, 1, (double)1.0));
            
			dx = fill(1, npic+1, (double)1.0);
            
			go = 1;
			minit = 0;
            
			while(go >= tol)
            {
                minit = minit + 1;
                gap = fill(2, mm, (double)0.0);
                setColumnInplace(gap, subtract(subset(p0, 0, 0, mm-1), getColumn(pb, 0)), 0);
                setColumnInplace(gap, subtract(getColumn(pb, 1), subset(p0, 0, 0, mm-1)), 1);
                gap = rowSort(gap);
                dx = copyInto(transpose(dx), getColumn(gap,0), 0, 0, mm-1);
                
                M(dx, npic_int, 0) = M(p0, npic_int, 0);
                
                if(M(ind, 9, 0) <= 0)
                {
                    argum = multiplyByScalar2D(fill(1, npic-mm, (double)1.0) , max(findMax(subset(dx, 0, 0, mm-1)), 100));
                    
                    copyIntoInplace(dx, argum, 0, mm, npic-1);
                    
                }
                
                dx = transpose(dx);
                Matrix argum1 = transpose(timess(a, transpose(diag(dx))));
                Matrix argum2 = multiply(dx, transpose(cx));
                Matrix y = QRdsolve(argum1, argum2);
                Matrix v = multiply(dx, multiply(dx, subtract(transpose(cx),timess(transpose(a),y))));
                v = transpose(v);
                
                int indexx = npic;
                
                if (M(v, indexx, 0) > 0)
                {
                    double z = M(p0, indexx, 0)/M(v, indexx, 0);
                    
                    for (int i=0; i<mm; i++)
                    {
                        if(M(v, i, 0) < 0)
                        {
                            z = min(z, -(M(pb, 1, i) - M(p0, i, 0))/M(v, i, 0));
                            
                        }
                        else if(M(v, i, 0) > 0)
                        {
                            
                            z = min(z, (M(p0, i, 0) - M(pb, 0, i))/M(v, i, 0));
                        }
                    }
                    
                    if(z < (M(p0, indexx, 0)/M(v, indexx, 0))) {
			    z *= 0.9;
		    }

		    Eigen::Map< Eigen::VectorXd > Ep0(p0.t, p0.cols);
		    Eigen::Map< Eigen::VectorXd > Ev(v.t, v.cols);
		    Ep0 -= Ev * z;

                    go = M(p0, indexx, 0);
                    
                    if(minit >= 10){
                        go = 0;
                    }
                }
                else{
                    go = 0;
                    minit = 10;
                }
                
            }// end while(go >= tol)
            
            if (minit >= 10){
				mxLog("The linearized problem has no feasible solution. The problem may not be feasible.");
			}
			int h;
			Matrix aMatrix = fill(npic, nc, (double)0.0);

			for (h = 0; h<a.rows; h++) {
				setRowInplace(aMatrix, h, subset(getRow(a, h), 0, 0, npic-1));
			}
			a = aMatrix;
            
			b = timess(a, transpose(subset(p0, 0, 0, npic-1)));
            
            
		}// end if(M(alp, 0, 0) <= 0)
	} // end if (nc > 0){
    
	p = subset(p0, 0, 0, npic-1);
    y = fill(1, 1, (double)0.0);
    
	if (verbose >= 3){
		mxLog("p is: \n");
		for (int i = 0; i < p.cols; i++) mxLog("%f",p.t[i]);
	}
    
	if (ch > 0){
		tmpv = multiply(subset(p, 0, nineq, (npic-1)), subset(vscale, 0, (nc+1), (nc+np)));
		if (verbose >= 2){
			mxLog("tmpv is: \n");
			for (int i = 0; i < tmpv.cols; i++) mxLog("%f",tmpv.t[i]);
			mxLog("8th call is \n");
		}
		funv = solFun(tmpv, mode, verbose);
		if (verbose >= 3){
			mxLog("funv is: \n");
			mxLog("%2f", funv);
		}
        
        if (*mode == -1)
        {
            funv = 1e24;
            *mode = 0;
        }

		eqv = solEqBFun(verbose);
		if (verbose >= 3){
			mxLog("eqv is: \n");
			for (int i = 0; i < eqv.cols; i++) mxLog("%f",eqv.t[i]);
		}
		ineqv = myineqFun(verbose);
		if (verbose >= 3){
			mxLog("ineqv is: \n");
			for (int i = 0; i < ineqv.cols; i++) mxLog("%f",ineqv.t[i]);
		}
		solnp_nfn = solnp_nfn + 1;
		Matrix firstPart, secondPart, firstPartt;
		if ( M(ineqv,0,0) != EMPTY){
			if (M(eqv,0,0) != EMPTY){
				firstPartt = copy(fill(1, 1, funv), eqv);
				firstPart = copy(firstPartt, ineqv);
			}
			else{
				firstPart = copy(fill(1, 1, funv), ineqv);
			}
		}
		else if (M(eqv,0,0) != EMPTY){
			firstPart = copy(fill(1, 1, funv), eqv);
		}
		else firstPart = fill(1, 1, funv);
		secondPart = subset(vscale, 0, 0, nc);
		ob = divide(firstPart, secondPart);
        
	} // end of if (ch>0)
    
	if (verbose >= 3){
		mxLog("ob is: \n");
		for (int i = 0; i < ob.cols; i++) mxLog("%f",ob.t[i]);
	}
    
	j = M(ob, 0, 0);
    
	if (M(ind, 3, 0) > 0){
		copyIntoInplace(ob, subtract(subset(ob, 0, neq+1, nc), subset(p, 0, 0, nineq-1)), 0, neq+1, nc);
        
	}
    
	if (nc > 0){
        
		copyIntoInplace(ob, add(subtract(subset(ob, 0, 1, nc), transpose(timess(a, transpose(p)))), b), 0, 1, nc);
        
		Matrix temp = subset(ob, 0, 1, nc);
        
		double vnormTerm = vnorm(temp) * vnorm(temp);
		Matrix yyTerm = transpose(yy);
        
		double dotProductTerm = dotProduct(getRow(yyTerm, 0), getRow(temp, 0));
        
		j = M(ob, 0, 0) - dotProductTerm + rho * vnormTerm;

    } // end if (nc > 0)
    
	minit = 0;
	Matrix obm = fill(1, 1, (double)0.0);
	Matrix yg = fill(npic, 1, (double)0.0);
    Matrix yg_rec = fill(2, 1, (double)0.0);
	Matrix sx = fill(p.cols, 1, (double)0.0);
	Matrix sc = fill(2, 1, (double)0.0);
	Matrix cz = fill(np, np, (double)0.0);
	Matrix czSolution;
	Matrix u;
    
	int i;
	while (minit < maxit){
		minit = minit + 1;

		if (ch > 0){
            
			for (i=0; i<np; i++){
				int index = nineq+i;
				M(p, index, 0) = M(p, index, 0) + delta;
				tmpv = multiply(subset(p, 0, nineq, (npic-1)), subset(vscale, 0, (nc+1), (nc+np)));
				if (verbose >= 3){
					mxLog("9th call is \n");
                    
				}
				funv = solFun(tmpv, mode, verbose);
				if (verbose >= 3){
					mxLog("funv is: \n");
					mxLog("%2f", funv);
				}
                
                if (*mode == -1)
                {
                    funv = 1e24;
                    *mode = 0;
                }
				eqv = solEqBFun(verbose);
				ineqv = myineqFun(verbose);
				solnp_nfn = solnp_nfn + 1;
				Matrix firstPart, secondPart, firstPartt;
                
				if (M(ineqv, 0, 0) != EMPTY){
					if(M(eqv,0,0) != EMPTY)
                    {
                        firstPartt = copy(fill(1, 1, funv), eqv);
                        firstPart = copy(firstPartt, ineqv);
                    }
					else{
						firstPart = copy(fill(1, 1, funv), ineqv);
					}
				}
				else if (M(eqv,0,0) != EMPTY){
					firstPart = copy(fill(1, 1, funv), eqv);
				}
				else {firstPart = fill(1, 1, funv);}
                
				secondPart = subset(vscale, 0, 0, nc);
				obm = divide(firstPart, secondPart);
				if (verbose >= 3){
					mxLog("obm is: \n");
					for (int ilog = 0; ilog < obm.cols; ilog++) mxLog("%f",obm.t[ilog]);
					mxLog("j is: \n");
					mxLog("%.20f", j);
				}
                
				if (M(ind, 3, 0) > 0.5){
					copyIntoInplace(obm, subtract(subset(obm, 0, neq+1, nc), subset(p, 0, 0, nineq-1)), 0, neq+1, nc);
				}
                
				if (nc > 0){
                    
					Matrix first_part = subtract(subset(obm, 0, 1, nc),transpose(timess(a, transpose(p))));
					copyIntoInplace(obm, add(first_part, b), 0, 1, nc);
					Matrix temp = subset(obm, 0, 1, nc);
					double vnormTerm = vnorm(temp) * vnorm(temp);
					Matrix yyTerm = transpose(yy);
					double dotProductTerm = dotProduct(getRow(yyTerm, 0), getRow(temp, 0));
					double newOBMValue = M(obm, 0, 0) - dotProductTerm + rho * vnormTerm;
					obm = fill(1, 1, newOBMValue);
				}
                
				M(g, index, 0) = (M(obm, 0, 0) - j)/delta;
				M(p, index, 0) = M(p, index, 0) - delta;
                
				if (verbose >= 3){
					mxLog("g is: \n");
					for (int ilog = 0; ilog < g.cols; ilog++) mxLog("%f",g.t[ilog]);
					mxLog("p is: \n");
					for (int ilog = 0; ilog < p.cols; ilog++) mxLog("%f",p.t[ilog]);
				}
			} // end for (i=0; i<np; i++){
            
			if (M(ind, 3, 0) > 0.5){
				copyIntoInplace(g, fill(nineq, 1, (double)0.0), 0, 0, (nineq-1));
			}
            
		} // end if (ch > 0){
        
		if (verbose >= 3){
			mxLog("yg is: \n");
            for (i = 0; i < yg.cols; i++) mxLog("%f",yg.t[i]);
		}
        
		if (minit > 1){
			yg = subtract(g, yg);
			sx = subtract(p, sx);
			Matrix m_sc = timess(timess(sx, hessv), transpose(sx));
            M(sc, 0, 0) = M(m_sc, 0, 0);
            Matrix m_sc2 = timess(sx, transpose(yg));
            M(sc, 1, 0) = M(m_sc2, 0, 0);

			if ((M(sc, 0, 0) * M(sc, 1, 0)) > 0){
				//hessv  = hessv - ( sx %*% t(sx) ) / sc[ 1 ] + ( yg %*% t(yg) ) / sc[ 2 ]
                sx = timess(hessv, transpose(sx));
                
                Matrix sxMatrix = divideByScalar2D(timess(sx, transpose(sx)), M(sc, 0, 0));
                Matrix ygMatrix = divideByScalar2D(timess(transpose(yg), yg), M(sc, 1, 0));
				
				hessv = subtract(hessv, sxMatrix);
				hessv = add(hessv, ygMatrix);
			}
            
		}
		if (verbose >= 3){
			mxLog("yg is: \n");
			for (i = 0; i < yg.cols; i++) mxLog("%f",yg.t[i]);
			mxLog("sx is: \n");
			for (i = 0; i < sx.cols; i++) mxLog("%f",sx.t[i]);
			mxLog("sc is: \n");
			for (i = 0; i < sc.cols; i++) mxLog("%f",sc.t[i]);
			mxLog("hessv is: \n");
			for (i = 0; i < hessv.cols; i++) mxLog("%f",hessv.t[i]);
		}
        
		dx = fill(npic, 1, 0.01);
        
		if (M(ind, 10, 0) > 0.5){
			/** LOTS HERE BUT IT DOESN'T AFFECT US **/
            
			gap = fill(pb.cols, pb.rows, (double)0.0);
            
			setColumnInplace(gap, subtract(subset(p, 0, 0, mm-1), getColumn(pb, 0)), 0);
            
			setColumnInplace(gap, subtract(getColumn(pb, 1), subset(p, 0, 0, mm-1)), 1);
            
			gap = rowSort(gap);
            
			gap = add(getColumn(gap, 0), multiplyByScalar2D(fill(1, mm,(double)1.0),sqrt(DBL_EPSILON)));
            
			copyIntoInplace(dx, divide(fill(mm, 1, (double)1.0), gap), 0, 0, mm-1);
            
			if (verbose >= 3){
				mxLog("dx is: \n");
				for (i = 0; i < dx.cols; i++) mxLog("%f",dx.t[i]);
			}
            
			if(M(ind, 9, 0) <= 0)
            {
                argum = multiplyByScalar2D(fill(1, npic-mm, (double)1.0) , min(findMin(subset(dx, 0, 0, mm-1)), 0.01));
                copyIntoInplace(dx, argum, 0, mm, npic-1);
                
            }
            
		}

		go = -1;
		lambdaValue = lambdaValue/10.0;

		Matrix yMatrix;
        
		if (verbose >= 3){
			mxLog("lambdaValue is: \n");
			mxLog("%.20f", lambdaValue);
			mxLog("hessv is: \n");
			for (i = 0; i < hessv.cols; i++) mxLog("%f",hessv.t[i]);
		}
        
		while(go <= 0){
			Matrix dxDiagValues = multiply(dx, dx);
			if (verbose >= 3){
				mxLog("dxDiagValues is: \n");
				for (i = 0; i < dxDiagValues.cols; i++) mxLog("%f",dxDiagValues.t[i]);
			}
			Matrix dxDiag = diag(dxDiagValues);
			if (verbose >= 3){
				mxLog("dxDiag is: \n");
				for (i = 0; i < dxDiag.cols; i++) mxLog("%f",dxDiag.t[i]);
			}
			Matrix dxMultbyLambda = multiplyByScalar2D(dxDiag, lambdaValue);
			if (verbose >= 3){
				mxLog("dxMultbyLambda is: \n");
				for (i = 0; i < dxMultbyLambda.cols; i++) mxLog("%f",dxMultbyLambda.t[i]);
			}
			Matrix addMatrices = add(hessv, dxMultbyLambda);
			if (verbose >= 3){
				mxLog("addMatrices is: \n");
				for (i = 0; i < addMatrices.cols*addMatrices.cols; i++) mxLog("%f",addMatrices.t[i]);
			}
			cz = chol_lpk(addMatrices);
			if (verbose >= 3){
				mxLog("cz is: \n");
				for (i = 0; i < cz.cols*cz.cols; i++) mxLog("%f",cz.t[i]);
			}
            if (!R_FINITE(findMax(cz)))
            {
                double nudge = 1.490116e-08;
                flag = 1;
                Matrix subvscale = subset(vscale, 0, (neq+1), (nc + np));
                p = multiply(p, subvscale);
                if (flag_L) {M(p, index_flag_L, 0) = M(p, index_flag_L, 0) + nudge;}
                if (flag_U) {M(p, index_flag_U, 0) = M(p, index_flag_U, 0)- nudge;}
                if (nc > 0){ y = fill(1, 1, (double)0.0);}
                hessv = multiplyByScalar2D(divide(hessv, transposeDP(subvscale)), M(vscale, 0, 0));
                resP = duplicateIt(p, FALSE);
                resY = duplicateIt(y, FALSE);
                resHessv = duplicateIt(hessv, FALSE);
                resLambda = lambda;
                return g;
            }

            //Matrix identityMatrix = diag(fill(hessv.cols, 1, (double)1.0));
            
            cz = solveinv(cz);
            //cz = luSolve(cz, identityMatrix);
			
            if (verbose >= 3){
				mxLog("cz is: \n");
				for (i = 0; i < cz.cols*cz.cols; i++) mxLog("%f",cz.t[i]);
			}
            
			//Matrix getRowed = getRow(cz, 0);
			if (verbose >= 3){
				mxLog("g is: \n");
				for (i = 0; i < g.cols; i++) mxLog("%f",g.t[i]);
			}
			//Matrix getRowedtwo = getRow(g, 0);
			//double rr = dotProduct(getRowed, getRowedtwo);
			yg = timess(transpose(cz), transpose(g));
            if (minit == 1) M(yg_rec, 0, 0) = vnorm(yg);
            
			if (verbose >= 3){
				mxLog("yg is: \n");
				for (i = 0; i < yg.cols; i++) mxLog("%f",yg.t[i]);
			}
			if (nc <= 0){
                u = timess(divideByScalar2D(cz, -1.0), yg);
                u = transpose(u);
				if (verbose >= 3){
					mxLog("u inside nc <=0 is: \n");
					for (i = 0; i < u.cols; i++) mxLog("%f",u.t[i]);
				}
			}
			else{
				//y = qr.solve(t(cz) %*% t(a), yg)
				Matrix aTranspose = transpose(a);
				if (verbose >= 3){
					mxLog("aTranspose is: \n");
					for (i = 0; i < aTranspose.cols; i++) mxLog("%f",aTranspose.t[i]);
				}
                Matrix firstMatrix = timess(transpose(cz), aTranspose);
				if (verbose >= 3){
					mxLog("firstMatrix is: \n");
					for (i = 0; i < firstMatrix.cols; i++) mxLog("%f",firstMatrix.t[i]);
				}
				Matrix secondMatrix = yg;
				if (verbose >= 3){
					mxLog("secondMatrix is: \n");
					for (i = 0; i < secondMatrix.cols; i++) mxLog("%f",secondMatrix.t[i]);
				}
                Matrix solution = QRdsolve(firstMatrix, secondMatrix);
				if (verbose >= 3){
					mxLog("solution is: \n");
					for (i = 0; i < solution.cols; i++) mxLog("%f",solution.t[i]);
				}
				//Matrix solution = QRd(firstMatrix, secondMatrix);
				y = transpose(solution);
				if (verbose >= 3){
					mxLog("y is: \n");
					for (i = 0; i < y.cols; i++) mxLog("%f",y.t[i]);
				}
                //yMatrix = subset(solution, 0, 0, nc-1);
                yMatrix = duplicateIt(solution);
				if (verbose >= 3){
					mxLog("yMatrix is: \n");
					for (i = 0; i < yMatrix.cols; i++) mxLog("%f",yMatrix.t[i]);
				}
				//u = -cz %*% (yg - ( t(cz) %*% t(a) ) %*% y)
				Matrix minuscz = multiplyByScalar2D(cz, -1.0);
				Matrix toSubtract = timess(firstMatrix, yMatrix);
				Matrix partU = subtract(secondMatrix, toSubtract);
                
				u = timess(minuscz, partU);
				if (verbose >= 3){
					mxLog("u is: \n");
					for (i = 0; i < u.cols; i++) mxLog("%f",u.t[i]);
				}
				u = transpose(u);
			}
            
            if (verbose >= 3){
				mxLog("p is: \n");
				for (i = 0; i < p.cols; i++) mxLog("%f",p.t[i]);
			}
			p0 = add(subset(u, 0, 0, npic-1), p);
			if (verbose >= 3){
				mxLog("p0 is: \n");
				for (i = 0; i < p0.cols; i++) mxLog("%f",p0.t[i]);
			}
            
			if (M(ind, 10, 0) <= 0.5){
				go = 1;
			} else{
				Matrix listPartOne = subtract(subset(p0, 0, 0, mm-1), getColumn(pb, 0));

				Matrix listPartTwo = subtract(getColumn(pb, 1), subset(p0, 0, 0, mm-1));

				Matrix llist = copy(listPartOne, listPartTwo);

				go = findMin(llist);
				lambdaValue = 3 * lambdaValue;
				if (verbose >= 3){
					mxLog("go is: \n");
					mxLog("%.20f", go);
                    mxLog("lambdaValue is: \n");
                    mxLog("%.20f", lambdaValue);

				}
			}
		} // end while(go <= 0){
        
        
		alp[0] = 0;
		Matrix ob1 = duplicateIt(ob);
		Matrix ob2 = duplicateIt(ob1);
        
		sob[0] = j;
		sob[1] = j;
        
        
		if (verbose >= 3){
			mxLog("sob is is: \n");
			for (i = 0; i < sob.size(); i++) mxLog("%f", sob[i]);
		}
		ptt = copy(transpose(p), transpose(p));
        
		alp[2] = 1.0;
		if (verbose >= 3){
			mxLog("ptt is: \n");
			for (i = 0; i < ptt.cols; i++) mxLog("%f", ptt.t[i]);
		}
        
		ptt = copy(ptt, transpose(p0));
        
		if (verbose >= 3){
			mxLog("ptt2 is: \n");
			for (i = 0; i < ptt.cols; i++) mxLog("%f",ptt.t[i]);
		}
        
		bool condif1, condif2, condif3;
        
		Matrix pttCol = getColumn(ptt, 2);
        
		tmpv = multiply(subset(pttCol, 0, nineq, (npic-1)), subset(vscale, 0, (nc+1), (nc+np)));
        
		if (verbose >= 3){
			mxLog("tmpv is: \n");
			for (i = 0; i < tmpv.cols; i++) mxLog("%f",tmpv.t[i]);
		}
		if (verbose >= 2){        
			//printf("10th call is \n");
		}
		funv = solFun(tmpv, mode, verbose);
		if (verbose >= 3){
            mxLog("hessv is: \n");
            for (i = 0; i < hessv.cols; i++) mxLog("%f",hessv.t[i]);
        
            mxLog("g is: \n");
            for (i = 0; i < g.cols; i++) mxLog("%f",g.t[i]);

			mxLog("funv is: \n");
			mxLog("%.20f", funv);
		}
        
        if (*mode == -1)
        {
            funv = 1e24;
            *mode = 0;
        }

		eqv = solEqBFun(verbose);

		ineqv = myineqFun(verbose);
        
		solnp_nfn = solnp_nfn + 1;
		Matrix firstPart, secondPart, firstPartt;
        
		if (M(ineqv, 0, 0) != EMPTY){
			if(M(eqv,0,0) != EMPTY)
            {
                firstPartt = copy(fill(1, 1, funv), eqv);
                firstPart = copy(firstPartt, ineqv);
            }
			else{
				firstPart = copy(fill(1, 1, funv), ineqv);
			}
		}
		else if (M(eqv,0,0) != EMPTY){
			firstPart = copy(fill(1, 1, funv), eqv);
		}
		else {firstPart = fill(1, 1, funv);}
        
		secondPart = subset(vscale, 0, 0, nc);
        
		Matrix ob3 = divide(firstPart, secondPart);
		if (verbose >= 3){
			mxLog("ob3 is: \n");
			for (i = 0; i < ob3.cols; i++) mxLog("%f",ob3.t[i]);
		}
        
		sob[2] = M(ob3, 0, 0);
        
		if (M(ind, 3, 0) > 0.5){
			// ob3[ (neq + 2):(nc + 1) ] = ob3[ (neq + 2):(nc + 1) ] - ptt[ 1:nineq, 3 ]
			Matrix diff = fill((nineq+1), 1, (double)0.0);
			Matrix partOne = subset(ob3, 0, neq+1, nc);
			Matrix tempPttCol = getColumn(ptt, 2);
			Matrix partTwo = subset(tempPttCol, 0, 0, nineq-1);
			diff = subtract(partOne, partTwo);
			copyIntoInplace(ob3, diff, 0, neq+1, nc);
		}
        
		if (nc > 0){
			//sob[ 3 ] = ob3[ 1 ] - t(yy) %*% ob3[ 2:(nc + 1) ] + rho * .vnorm(ob3[ 2:(nc + 1) ]) ^ 2
            Matrix firstp = subtract(subset(ob3, 0, 1, nc), transpose(timess(a, transpose(getColumn(ptt, 2)))));
            
            copyIntoInplace(ob3, add(firstp, b), 0, 1, nc);
            Matrix temp = subset(ob3, 0, 1, nc);
            
            double vnormTerm = vnorm(temp) * vnorm(temp);
            
            Matrix yyTerm = transpose(yy);
            
            double dotProductTerm = dotProduct(getRow(yyTerm, 0), getRow(temp, 0));
            
            sob[2] = M(ob3, 0, 0) - dotProductTerm + (rho * vnormTerm);
		}
        
		go = 1;
        
		while(go > tol){
            
			alp[1] = (alp[0] + alp[2]) / 2.0;
            
			Matrix colValues = add(multiplyByScalar2D(p, (1.0 - alp[1])), multiplyByScalar2D(p0, alp[1]));
			setColumnInplace(ptt, colValues, 1);
            
			Matrix pttColOne = getColumn(ptt, 1);
            
			tmpv = multiply(subset(pttColOne, 0, nineq, (npic-1)),
                            subset(vscale, 0, (nc+1), (nc+np)));
			if (verbose >= 3){
				mxLog("tmpv is: \n");
				for (i = 0; i < tmpv.cols; i++) mxLog("%f",tmpv.t[i]);
			}
            
			if (verbose >= 3){
				mxLog("11th call is \n");
			}
            
			funv = solFun(tmpv, mode, verbose);
			if (verbose >= 3){
				mxLog("funv is: \n");
				mxLog("%2f", funv);
			}
            
            if (*mode == -1)
            {
                funv = 1e24;
                *mode = 0;
            }

			eqv = solEqBFun(verbose);
            
			ineqv = myineqFun(verbose);
			solnp_nfn = solnp_nfn + 1;
			Matrix firstPart, secondPart, firstPartt;
			if (M(ineqv, 0, 0) != EMPTY){
				if (M(eqv,0,0) != EMPTY)
                {
                    firstPartt = copy(fill(1, 1, funv), eqv);
                    firstPart = copy( firstPartt, ineqv);
                }
				else{
					firstPart = copy(fill(1, 1, funv), ineqv);
				}
			}
			else if (M(eqv,0,0) != EMPTY){
				firstPart = copy(fill(1, 1, funv), eqv);
			}
			else firstPart = fill(1, 1, funv);
            
			secondPart = subset(vscale, 0, 0, nc);
            
            ob2 = divide(firstPart, secondPart);
			if (verbose >= 3){
				mxLog("ob2 is: \n");
				for (i = 0; i < ob2.cols; i++) mxLog("%f",ob2.t[i]);
			}
            
			sob[1] = M(ob2, 0, 0);
			if (verbose >= 3){
				mxLog("sob is: \n");
				for (i = 0; i < sob.size(); i++) mxLog("%f",sob[i]);
			}
            //exit(0);
			if (M(ind, 3, 0) > 0.5){
				Matrix diff = fill(nineq+1, 1, (double)0.0);
				Matrix partOne = subset(ob2, 0, neq+1, nc);
				Matrix tempPttCol = getColumn(ptt, 1);
				Matrix partTwo = subset(tempPttCol, 0, 0, nineq-1);
				diff = subtract(partOne, partTwo);
				copyIntoInplace(ob2, diff, 0, neq+1, nc);
                
			}
            
			if (nc > 0){
				copyIntoInplace(ob2, add(subtract(subset(ob2, 0, 1, nc), transpose(timess(a, transpose(getColumn(ptt, 1))))), b), 0, 1, nc);
				Matrix temp = subset(ob2, 0, 1, nc);
				double vnormTerm = vnorm(temp) * vnorm(temp);
				Matrix yyTerm = transpose(yy);
				double dotProductTerm = dotProduct(getRow(yyTerm, 0), getRow(temp, 0));
				sob[1] = M(ob2, 0, 0) - dotProductTerm + rho * vnormTerm;
			}
			if (verbose >= 3){
				mxLog("sob is: \n");
				for (i = 0; i < sob.size(); i++) mxLog("%f",sob[i]);
			}
			M(obm, 0, 0) = sob.maxCoeff();
			if (verbose >= 3){
				mxLog("obm is: \n");
				for (i = 0; i < obm.cols; i++) mxLog("%f",obm.t[i]);
			}
			if (M(obm, 0, 0) < j){
				double obn = sob.minCoeff();
				go = tol * (M(obm, 0, 0) - obn) / (j - M(obm, 0, 0));
			}
            
            
			condif1 = sob[1] >= sob[0];
			condif2 = (sob[0] <= sob[2]) && (sob[1] < sob[0]);
			condif3 = (sob[1] <  sob[0]) && (sob[0] > sob[2]);
            
			if (condif1){
				sob[2] = sob[1];
                
				ob3 = duplicateIt(ob2);
                
				alp[2] = alp[1];
                
				Matrix tempCol = getColumn(ptt, 1);
                
				setColumnInplace(ptt, tempCol, 2);
                
				if (verbose >= 3){
					mxLog("sob is: \n");
					for (i = 0; i < sob.size(); i++) mxLog("%f",sob[i]);
					mxLog("ob3 is: \n");
					for (i = 0; i < ob3.cols; i++) mxLog("%f",ob3.t[i]);
					mxLog("alp is: \n");
					for (i = 0; i < alp.size(); i++) mxLog("%f",alp[i]);
					mxLog("ptt is: \n");
					for (i = 0; i < ptt.cols; i++) mxLog("%f",ptt.t[i]);
				}
                
			}
            
			if (condif2){
				sob[2] = sob[1];
				ob3 = duplicateIt(ob2);
				alp[2] = alp[1];
				Matrix tempCol = getColumn(ptt, 1);
				setColumnInplace(ptt, tempCol, 2);
                
				if (verbose >= 3){
					mxLog("sob is: \n");
					for (i = 0; i < sob.size(); i++) mxLog("%f",sob[i]);
					mxLog("ob3 is: \n");
					for (i = 0; i < ob3.cols; i++) mxLog("%f",ob3.t[i]);
					mxLog("alp is: \n");
					for (i = 0; i < alp.size(); i++) mxLog("%f",alp[i]);
					mxLog("ptt is: \n");
					for (i = 0; i < ptt.cols; i++) mxLog("%f",ptt.t[i]);				}
			}
            
			if (condif3){
				sob[0] = sob[1];
				ob1 = duplicateIt(ob2);
				alp[0] = alp[1];
				Matrix tempCol = getColumn(ptt, 1);
				setColumnInplace(ptt, tempCol, 0);
				if (verbose >= 3){
					mxLog("sob is: \n");
					for (i = 0; i < sob.size(); i++) mxLog("%f",sob[i]);
					mxLog("ob3 is: \n");
					for (i = 0; i < ob3.cols; i++) mxLog("%f",ob3.t[i]);
					mxLog("alp is: \n");
					for (i = 0; i < alp.size(); i++) mxLog("%f",alp[i]);
					mxLog("ptt is: \n");
					for (i = 0; i < ptt.cols; i++) mxLog("%f",ptt.t[i]);				}
			}
            
			if (go >= tol){
				go = alp[2] - alp[0];
				if (verbose >= 3){
					mxLog("go is: \n");
					mxLog("%.20f", go);
				}
			}
		} // 	while(go > tol){
        //exit(0);
		if (verbose >= 3){
			mxLog("go is: \n");
			mxLog("%.16f", go);
		}
        sx_Matrix = duplicateIt(sx, FALSE);
		sx = duplicateIt(p);
		yg = duplicateIt(g);
		if (verbose >= 3){
			mxLog("sx is: \n");
			for (i = 0; i < sx.cols; i++) mxLog("%f",sx.t[i]);
			mxLog("yg is: \n");
			for (i = 0; i < yg.cols; i++) mxLog("%f",yg.t[i]);
		}
		ch = 1;
        
		double obn = sob.minCoeff();
		if (verbose >= 3){
			mxLog("obn is: \n");
			mxLog("%.20f", obn);
		}
		if (j <= obn){
			maxit = minit;
		}
		if (verbose >= 3){
			mxLog("j is: \n");
			mxLog("%.20f", j);
		}
		double reduce = (j - obn) / ((double)1.0 + (double)fabs(j));
		if (verbose >= 3){
			mxLog("reduce is: \n");
			mxLog("%.20f", reduce);
		}
		if (reduce < tol){
			maxit = minit;
		}
        
		condif1 = (sob[0] <  sob[1]);
		condif2 = (sob[2] <  sob[1]) && (sob[0] >=  sob[1]);
		condif3 = (sob[0] >= sob[1]) && (sob[2] >= sob[1]);
        
		if (condif1){
			j = sob[0];
			p = getColumn(ptt, 0);
			ob = duplicateIt(ob1);
			if (verbose >= 3){
                mxLog("condif1\n");
				mxLog("j is: \n");
				mxLog("%2f", j);
				mxLog("p is: \n");
				for (i = 0; i < p.cols; i++) mxLog("%f",p.t[i]);
				mxLog("ob is: \n");
				for (i = 0; i < ob.cols; i++) mxLog("%f",ob.t[i]);
			}
		}
        
		if (condif2){
            
			j = sob[2];
			p = getColumn(ptt, 2);
			ob = duplicateIt(ob3);
			if (verbose >= 3){
                mxLog("condif2\n");
				mxLog("j is: \n");
				mxLog("%2f", j);
				mxLog("p is: \n");
				for (i = 0; i < p.cols; i++) mxLog("%f",p.t[i]);
				mxLog("ob is: \n");
				for (i = 0; i < ob.cols; i++) mxLog("%f",ob.t[i]);
			}
            
		}
        
		if (condif3){
			j = sob[1];
			p = getColumn(ptt, 1);
			ob = duplicateIt(ob2);
			if (verbose >= 3){
                mxLog("condif3\n");
				mxLog("j is: \n");
				mxLog("%2f", j);
				mxLog("p is: \n");
				for (i = 0; i < p.cols; i++) mxLog("%f",p.t[i]);
				mxLog("ob is: \n");
				for (i = 0; i < ob.cols; i++) mxLog("%f",ob.t[i]);
			}
		}
		if (verbose >= 3){
			mxLog("yg\n");
			for (i = 0; i < yg.cols; i++) mxLog("%f",yg.t[i]);
		}
	} // end while (minit < maxit){
        
    M(yg_rec, 1, 0) = vnorm(yg);
    if(M(yg_rec, 0, 0) / M(yg_rec, 1, 0) > 1000)  flag_NormgZ = 1;
    
    minr_rec = minit;
	
    if (all(subtract(getColumn(ptt, 1), getColumn(ptt, 0))) || all(subtract(getColumn(ptt, 1), getColumn(ptt, 2)))) flag_step = 1;
	//p = p * vscale[ (neq + 2):(nc + np + 1) ]  # unscale the parameter vector
	Matrix vscalePart = subset(vscale, 0, (neq+1), (nc+np));
    
	p = multiply(p, vscalePart);
    
	if (nc > 0){
		//y = vscale[ 0 ] * y / vscale[ 2:(nc + 1) ] # unscale the lagrange multipliers
		y = multiplyByScalar2D(y, M(vscale,0,0));
		y = divide(y, subset(vscale, 0, 1, nc));
	}
    
	// hessv = vscale[ 1 ] * hessv / (vscale[ (neq + 2):(nc + np + 1) ] %*%
	//                                t(vscale[ (neq + 2):(nc + np + 1) ]) )
    
	Matrix transposePart = transpose2D(subset(vscale, 0, (neq+1), (nc+np)));
	hessv = divide(hessv, transposePart);
	hessv = multiplyByScalar2D(hessv, M(vscale,0,0));
    
	if (verbose >= 1 && reduce > tol) {
		mxLog("m3 solnp Rf_error message being reported.");
    }
	
	resP = duplicateIt(p, FALSE);
	resY = duplicateIt(transpose(subset(y, 0, 0, (yyRows-1))), FALSE);
    
	resHessv = duplicateIt(hessv, FALSE);
    
	resLambda = lambdaValue;
    
	if (DEBUG && outerIter==4){
		mxLog("------------------------RETURNING FROM SUBNP------------------------");
		mxLog("p information: ");
		for (i = 0; i < resP.cols; i++) mxLog("%f",resP.t[i]);
		mxLog("y information: ");
		for (i = 0; i < resY.cols; i++) mxLog("%f",resY.t[i]);
		mxLog("hessv information: ");
		for (i = 0; i < resHessv.cols; i++) mxLog("%f",resHessv.t[i]);
		mxLog("lambda information: ");
		mxLog("%f", resLambda);
		mxLog("minit information: ");
		mxLog("%d", minit);
		mxLog("------------------------END RETURN FROM SUBNP------------------------");
	}
    
    freeMatrices();
	return g;
	
} // end subnp
















