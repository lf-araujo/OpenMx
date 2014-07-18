/*
 *  Copyright 2013 The OpenMx Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <algorithm>
#include <stdarg.h>

//#include <iostream>

#include "Compute.h"
#include "omxState.h"
#include "omxExportBackendState.h"
#include "omxRFitFunction.h"
#include "matrix.h"
#include "omxBuffer.h"
#include "omxState.h"

void pda(const double *ar, int rows, int cols);

void FitContext::queue(HessianBlock *hb)
{
	if (hb->vars.size() == 0) {
		delete hb;
		return;
	}

	// std::cout << "queue " << allBlocks.size() << " var map: ";
	// for (int vm=0; vm < (int) hb->vars.size(); ++vm) {
	// 	std::cout << vm << ":" << hb->vars[vm] << ",";
	// }
	// std::cout << "\n" << hb->mat << "\n";

	if (OMX_DEBUG) {
		std::vector<int>::iterator it = std::unique(hb->vars.begin(), hb->vars.end());
		if (std::distance(hb->vars.end(),it) != 0) {
			Rf_error("HessianBlock var mapping is not 1-to-1");
		}
	}

	minBlockSize = std::max(int(hb->vars.size()), minBlockSize);
	allBlocks.push_back(hb);
}

void FitContext::analyzeHessianBlock(HessianBlock *hb)
{
	for (size_t vx=0; vx < hb->vars.size(); ++vx) {
		HessianBlock *hb2 = blockByVar[ hb->vars[vx] ];
		if (!hb2) continue;

		for (size_t vx=0; vx < hb2->vars.size(); ++vx) {
			blockByVar[ hb2->vars[vx] ] = NULL;
		}
		estNonZero -= hb2->estNonZero();

		if (hb->vars.size() < hb2->vars.size()) std::swap(hb, hb2);

		// std::cout << "hb: ";
		// for (int i : hb->vars) std::cout << i << ' ';
		// std::cout << "\nhb2: ";
		// for (int i : hb2->vars) std::cout << i << ' ';
		// std::cout << "\n";
		bool inc = std::includes(hb->vars.begin(), hb->vars.end(), hb2->vars.begin(), hb2->vars.end());
		//std::cout << "includes " << inc << "\n";
		if (inc) {
			hb->subBlocks.push_back(hb2);
		} else {
			HessianBlock *parent = new HessianBlock;
			mergeBlocks.push_back(parent);
			parent->merge = true;
			parent->vars = hb->vars;
			parent->vars.insert(parent->vars.end(), hb2->vars.begin(), hb2->vars.end());
			std::inplace_merge(parent->vars.begin(), parent->vars.begin() + hb->vars.size(), parent->vars.end());
			int nsize = std::unique(parent->vars.begin(), parent->vars.end()) - parent->vars.begin();
			parent->vars.resize(nsize);
			parent->mat.resize(nsize, nsize);
			parent->mat.triangularView<Eigen::Upper>().setZero();
			parent->subBlocks.push_back(hb);
			parent->subBlocks.push_back(hb2);
			analyzeHessianBlock(parent);
			return;
		}
	}

	for (size_t vx=0; vx < hb->vars.size(); ++vx) {
		blockByVar[ hb->vars[vx] ] = hb;
	}
	estNonZero += hb->estNonZero();
	maxBlockSize = std::max(int(hb->vars.size()), maxBlockSize);
	//mxLog("queue maxBlocksize %d sparse %f", maxBlockSize, estNonZero / double(numParam * numParam));
}

void FitContext::analyzeHessian()
{
	// If we knew the minBlockSize was large then we wouldn't even
	// try to build merge blocks. 
	// If maxBlockSize is greater than some threshold then we should
	// also give up.

	if (blockByVar.size()) return;

	blockByVar.assign(numParam, NULL);

	for (size_t bx=0; bx < allBlocks.size(); ++bx) {
		analyzeHessianBlock(allBlocks[bx]);
	}
}

void FitContext::negateHessian()
{
	// this assumes that we haven't processed the blocks yet, need to check TODO
	for (size_t bx=0; bx < allBlocks.size(); ++bx) {
		allBlocks[bx]->mat *= -1.0;
	}
}

void FitContext::refreshDenseHess()
{
	if (haveDenseHess) return;

	hess.triangularView<Eigen::Upper>().setZero();

	for (size_t bx=0; bx < allBlocks.size(); ++bx) {
		HessianBlock *hb = allBlocks[bx];

		std::vector<int> &map = hb->vars;
		size_t bsize = map.size();

		for (size_t v1=0; v1 < bsize; ++v1) {
			for (size_t v2=0; v2 <= v1; ++v2) {
				hess(map[v2], map[v1]) += hb->mat(v2, v1);
			}
		}
	}

	haveDenseHess = true;
}

void FitContext::copyDenseHess(double *dest)
{
	refreshDenseHess();
	for (size_t v1=0; v1 < numParam; ++v1) {
		for (size_t v2=0; v2 <= v1; ++v2) {
			double coef = hess.selfadjointView<Eigen::Upper>()(v2,v1);
			if (v1==v2) {
				dest[v1 * numParam + v2] = coef;
			} else {
				dest[v1 * numParam + v2] = coef;
				dest[v2 * numParam + v1] = coef;
			}
		}
	}
}

double *FitContext::getDenseHessUninitialized()
{
	// Assume the caller is going to fill it out
	haveDenseHess = true;
	haveDenseIHess = false;
	return hess.data();
}

static void InvertSymmetricNR(Eigen::MatrixXd &hess, Eigen::MatrixXd &ihess)
{
	ihess = hess;
	Matrix ihessMat(ihess.data(), ihess.rows(), ihess.cols());

	if (0) {
		// for comparison
		InvertSymmetricIndef(ihessMat, 'U');
		return;
	}

	if (!InvertSymmetricPosDef(ihessMat, 'U')) return;

	int numParams = hess.rows();
	omxBuffer<double> hessWork(numParams * numParams);
	memcpy(hessWork.data(), hess.data(), sizeof(double) * numParams * numParams);
	//pda(hess.data(), numParams, numParams);

	char jobz = 'V';
	char range = 'A';
	char uplo = 'U';
	double abstol = 0;
	int m;
	omxBuffer<double> w(numParams);
	omxBuffer<double> z(numParams * numParams);
	double optWork;
	int optIwork;
	int lwork = -1;
	int liwork = -1;
	int info;
	double realIgn = 0;
	int intIgn = 0;
	omxBuffer<int> isuppz(numParams * 2);

	F77_CALL(dsyevr)(&jobz, &range, &uplo, &numParams, hessWork.data(),
			 &numParams, &realIgn, &realIgn, &intIgn, &intIgn, &abstol, &m, w.data(),
			 z.data(), &numParams, isuppz.data(), &optWork, &lwork, &optIwork, &liwork, &info);

	lwork = optWork;
	omxBuffer<double> work(lwork);
	liwork = optIwork;
	omxBuffer<int> iwork(liwork);
	F77_CALL(dsyevr)(&jobz, &range, &uplo, &numParams, hessWork.data(),
			 &numParams, &realIgn, &realIgn, &intIgn, &intIgn, &abstol, &m, w.data(),
			 z.data(), &numParams, isuppz.data(), work.data(), &lwork, iwork.data(), &liwork, &info);
	if (info < 0) {
		Rf_error("dsyevr %d", info);
	} else if (info) {
		mxLog("Eigen decomposition failed %d", info);
		ihess = Eigen::MatrixXd::Zero(numParams, numParams);
		return;
	}

	std::vector<double> evalDiag(numParams * numParams);
	for (int px=0; px < numParams; ++px) {
		evalDiag[px * numParams + px] = 1/fabs(w[px]);
	}

	Matrix evMat(z.data(), numParams, numParams);
	Matrix edMat(evalDiag.data(), numParams, numParams);
	omxBuffer<double> prod1(numParams * numParams);
	Matrix p1Mat(prod1.data(), numParams, numParams);
	SymMatrixMultiply('R', 'U', 1.0, 0, edMat, evMat, p1Mat);
	char transa = 'N';
	char transb = 'T';
	double alpha = 1.0;
	double beta = 0;
	F77_CALL(dgemm)(&transa, &transb, &numParams, &numParams, &numParams, &alpha,
			prod1.data(), &numParams, z.data(), &numParams, &beta, ihess.data(), &numParams);
	//pda(ihess.data(), numParams, numParams);
}

void FitContext::refreshDenseIHess()
{
	if (haveDenseIHess) return;

	refreshDenseHess();

	ihess = hess;
	Matrix ihessMat(ihess.data(), ihess.rows(), ihess.cols());
	InvertSymmetricIndef(ihessMat, 'U');

	haveDenseIHess = true;
}

void FitContext::refreshSparseHess()
{
	if (haveSparseHess) return;

	sparseHess.resize(numParam, numParam);
	sparseHess.setZero();

	// need to sort allBlocks for performance TODO
	for (size_t bx=0; bx < allBlocks.size(); ++bx) {
		HessianBlock *hb = allBlocks[bx];

		std::vector<int> &map = hb->vars;
		size_t bsize = map.size();

		for (size_t v1=0; v1 < bsize; ++v1) {
			for (size_t v2=0; v2 <= v1; ++v2) {
				sparseHess.coeffRef(map[v2], map[v1]) += hb->mat(v2, v1);
			}
		}
	}

	haveSparseHess = true;
}

static double norm1(const Eigen::SparseMatrix<double> &mat)
{
	// norm_1, maximum column sum
	double maxCol = 0;
	for (int k=0; k < mat.outerSize(); ++k) {
		double col = 0;
		for (Eigen::SparseMatrix<double>::InnerIterator it(mat,k); it; ++it) {
			col += fabs(it.value());
		}
		maxCol = std::max(maxCol, col);
	}
	return maxCol;
}

static double frobeniusNorm(const Eigen::SparseMatrix<double> &mat)
{
	double total=0;
	for (int k=0; k < mat.outerSize(); ++k) {
		for (Eigen::SparseMatrix<double>::InnerIterator it(mat,k); it; ++it) {
			total += it.value() * it.value();
		}
	}
	return total / mat.nonZeros();
}

static bool soleymani2013(const Eigen::SparseMatrix<double> &mat, Eigen::SparseMatrix<double> &imat)
{
       imat.setIdentity();
       imat /= frobeniusNorm(mat);

       Eigen::SparseMatrix<double> I1(mat.rows(), mat.cols());
       I1.setIdentity();
       Eigen::SparseMatrix<double> I7(mat.rows(), mat.cols());
       I7 = I1 * 7;
       Eigen::SparseMatrix<double> I21(mat.rows(), mat.cols());
       I21 = I1 * 21;
       Eigen::SparseMatrix<double> I35(mat.rows(), mat.cols());
       I35 = I1 * 35;

       double maxCol;
       int iter = 0;
       Eigen::SparseMatrix<double> AV = mat.selfadjointView<Eigen::Upper>() * imat;
       while (++iter < 100) {
               // try prune() on the multiplications TODO
               // try selfadjointView TODO
               Eigen::SparseMatrix<double> rpart =
                       (AV * (AV * (AV * (AV * (AV * (AV - I7) + I21) -I35) + I35) -I21) + I7);

               Eigen::SparseMatrix<double> prev = imat;
               imat = prev * rpart;
               AV = mat.selfadjointView<Eigen::Upper>() * imat;

               Eigen::SparseMatrix<double> diff = I1 - AV;
               maxCol = norm1(diff);
               if (maxCol < 1e-6) {
                       break;
               }
       }
       //mxLog("soleymani2013 invert in %d iter, maxCol %.2g", iter, maxCol);
       return false;
}

SEXP sparseInvert_wrapper(SEXP Rmat)
{
	omxManageProtectInsanity mpi;

	SEXP matrixDims;
	Rf_protect(matrixDims = Rf_getAttrib(Rmat, R_DimSymbol));
	int *dimList = INTEGER(matrixDims);
	int rows = dimList[0];
	int cols = dimList[1];
	if (rows != cols) Rf_error("Must be square");

	double *matData = REAL(Rmat);

	Eigen::SparseMatrix<double> mat(rows,cols);
	for (int cx=0; cx < cols; ++cx) {
		for (int rx=0; rx < rows; ++rx) {
			double val = matData[cx * rows + rx];
			if (val == 0) continue;
			mat.coeffRef(rx, cx) = val;
		}
	}

	Eigen::SparseMatrix<double> imat(rows,cols);
	if (soleymani2013(mat, imat)) Rf_error("Invert failed");
	
	SEXP ret;
	Rf_protect(ret = Rf_allocMatrix(REALSXP, rows, cols));
	double *retData = REAL(ret);
	for (int cx=0; cx < cols; ++cx) {
		for (int rx=0; rx < rows; ++rx) {
			retData[cx * rows + rx] = imat.coeff(rx, cx);
		}
	}

	return ret;
}

void FitContext::testMerge()
{
	const int UseId = 2;
	
	analyzeHessian();

	//std::cout << "block count " << allBlocks.size() << std::endl;

	sparseHess.resize(numParam, numParam);
	sparseHess.setZero();

	for (size_t vx=0; vx < numParam; ++vx) {
		HessianBlock *hb = blockByVar[vx];
		hb->addSubBlocks();
	}

	for (size_t vx=0; vx < numParam; ++vx) {
		HessianBlock *hb = blockByVar[vx];
		if (hb->useId == UseId) continue;
		hb->useId = UseId;

		// std::cout << "add block " << vx << "\nvar map: ";
		// for (int vm=0; vm < (int) hb->vars.size(); ++vm) {
		// 	std::cout << vm << ":" << hb->vars[vm] << ",";
		// }
		// std::cout << "\n" << hb->mmat << "\n";

		size_t size = hb->mmat.rows();
		for (size_t col=0; col < size; ++col) {
			for (size_t row=0; row <= col; ++row) {
				int vr = hb->vars[row];
				int vc = hb->vars[col];
				sparseHess.coeffRef(vr,vc) = hb->mmat(row,col);
			}
		}
	}

	refreshDenseHess();
	Eigen::MatrixXd dense = sparseHess;
	Eigen::MatrixXd diff = (dense - hess).selfadjointView<Eigen::Upper>();
	// std::cout << "difference\n" << diff << std::endl;
	// std::cout << "sparse\n" << dense << std::endl;
	// std::cout << "dense\n" << hess << std::endl;
	double bad = diff.cwiseAbs().maxCoeff();
	if (bad > .0001) Rf_error("Hess: dense sparse mismatch %f", bad);
}

bool FitContext::refreshSparseIHess()
{
	if (haveSparseIHess) return true;

	const int AcceptableDenseInvertSize = 100;
	const bool checkResult = OMX_DEBUG;

	//testMerge();

	sparseIHess.resize(numParam, numParam);
	sparseIHess.setZero();

	// sparseness by size simulation
	//mxLog("minBlockSize %d maxBlocksize %d sparse %f", maxBlockSize, estNonZero / double(numParam * numParam));

	if (minBlockSize < AcceptableDenseInvertSize) {
		analyzeHessian();
	}
	if (maxBlockSize < std::min(int(numParam), AcceptableDenseInvertSize)) {
		const int UseId = 1;
		for (size_t vx=0; vx < numParam; ++vx) {
			HessianBlock *hb = blockByVar[vx];
			if (hb->useId == UseId) continue;
			hb->useId = UseId;

			hb->addSubBlocks();
			size_t size = hb->mmat.rows();

			InvertSymmetricNR(hb->mmat, hb->imat);
		
			for (size_t col=0; col < size; ++col) {
				for (size_t row=0; row <= col; ++row) {
					int vr = hb->vars[row];
					int vc = hb->vars[col];
					sparseIHess.coeffRef(vr,vc) = hb->imat(row,col);
				}
			}
		}
	} else {
		return false;

		// Needs more work TODO
		// if (estNonZero / double(numParam * numParam) < .2)
		refreshSparseHess();
		if (soleymani2013(sparseHess, sparseIHess)) {
			sparseIHess.setZero();  // NR will try steepest descent
		}
	}

	if (checkResult) {
		refreshDenseHess();
		InvertSymmetricNR(hess, ihess);
		ihess.triangularView<Eigen::Lower>() = ihess.transpose().triangularView<Eigen::Lower>();
		Eigen::MatrixXd denseI = sparseIHess;
		denseI.triangularView<Eigen::Lower>() = denseI.transpose().triangularView<Eigen::Lower>();
		Eigen::MatrixXd resid = ihess - denseI;
		double bad = resid.cwiseAbs().maxCoeff();
		if (bad > .01) {
			// std::cout << "dense\n" << ihess << std::endl;
			// std::cout << "sparse\n" << denseI << std::endl;
			Rf_error("IHess: dense sparse mismatch %f", bad);
		}

		if (0) {
			int nonZero = 0;
			for (size_t cx=0; cx < numParam; ++cx) {
				for (size_t rx=0; rx <= cx; ++rx) {
					if (hess(rx,cx) != 0) {
						if (rx == cx) nonZero += 1;
						else nonZero += 2;
					}
				}
			}
			mxLog("done %f maxBlocksize %d est sparse %f actual sparse %f", bad, maxBlockSize,
			      estNonZero / double(numParam * numParam),
			      nonZero / double(numParam * numParam));
		}
	}

        haveSparseIHess = true;
	return true;
}

Eigen::VectorXd FitContext::ihessGradProd()
{
	if (refreshSparseIHess()) {
		return sparseIHess.selfadjointView<Eigen::Upper>() * grad;
	} else {
		refreshDenseHess();
		InvertSymmetricNR(hess, ihess);
		return ihess.selfadjointView<Eigen::Upper>() * grad;
	}
}

Eigen::VectorXd FitContext::ihessDiag()
{
	refreshDenseIHess();
	return ihess.diagonal();
}

double *FitContext::getDenseIHessUninitialized()
{
	// Assume the caller is going to fill it out
	haveDenseIHess = true;
	haveDenseHess = false;
	return ihess.data();
}

void FitContext::copyDenseIHess(double *dest)
{
	refreshDenseIHess();

	for (size_t v1=0; v1 < numParam; ++v1) {
		for (size_t v2=0; v2 <= v1; ++v2) {
			double coef = ihess.selfadjointView<Eigen::Upper>()(v2,v1);
			if (v1==v2) {
				dest[v1 * numParam + v2] = coef;
			} else {
				dest[v1 * numParam + v2] = coef;
				dest[v2 * numParam + v1] = coef;
			}
		}
	}
}

double *FitContext::getDenseHessianish()
{
	if (haveDenseHess) return hess.data();
	if (haveDenseIHess) return ihess.data();
	return NULL;
}

HessianBlock *HessianBlock::clone()
{
	HessianBlock *hb = new HessianBlock;
	hb->vars = vars;
	hb->mat.resize(vars.size(), vars.size());
	return hb;
}

void HessianBlock::addSubBlocks()
{
	if (mmat.rows()) return;

	mmat = mat;

	std::vector<int> vmap; // we could localize the vars map instead of computing the mapping each time TODO

	for (size_t bx=0; bx < subBlocks.size(); ++bx) {
		HessianBlock *sb = subBlocks[bx];
		sb->addSubBlocks();
	}

	//std::cout << "initial " << id << "\n" << mmat << std::endl;

	for (size_t bx=0; bx < subBlocks.size(); ++bx) {
		HessianBlock *sb = subBlocks[bx];
		
		//std::cout << "subblock " << sb->id << "\n" << sb->mmat << std::endl;

		size_t numVars = sb->vars.size();
		vmap.resize(numVars);
		for (size_t vx=0; vx < numVars; ++vx) {
			// std::set has special method for this TODO
			int px = std::lower_bound(vars.begin(), vars.end(), sb->vars[vx]) - vars.begin();
			vmap[vx] = px;
		}

		for (size_t v1=0; v1 < numVars; ++v1) {
			for (size_t v2=0; v2 <= v1; ++v2) {
				mmat(vmap[v2], vmap[v1]) += sb->mmat(v2, v1);
			}
		}
	}
	
	//std::cout << "result " << id << "\n" << mmat << std::endl;
}

int HessianBlock::estNonZero() const
{
	if (!merge) {
		return int(vars.size() * vars.size());
	} else {
		int total = 0;
		for (size_t bx=0; bx < subBlocks.size(); ++bx) {
			HessianBlock *sb = subBlocks[bx];
			total += sb->estNonZero();
		}
		return std::min(total, int(vars.size() * vars.size()));
	}
}

void FitContext::init()
{
	numParam = varGroup->vars.size();
	wanted = 0;
	mac = parent? parent->mac : 0;
	fit = parent? parent->fit : NA_REAL;
	est = new double[numParam];
	infoDefinite = NA_LOGICAL;
	infoCondNum = NA_REAL;
	infoA = NULL;
	infoB = NULL;
	stderrs = NULL;
	inform = INFORM_UNINITIALIZED;
	iterations = 0;

	hess.resize(numParam, numParam);
	ihess.resize(numParam, numParam);
	clearHessian();
}

void FitContext::clearHessian()
{
	for (size_t bx=0; bx < mergeBlocks.size(); ++bx) {
		delete mergeBlocks[bx];
	}
	for (size_t bx=0; bx < allBlocks.size(); ++bx) {
		delete allBlocks[bx];
	}

	allBlocks.clear();
	mergeBlocks.clear();
	blockByVar.clear();
	haveSparseHess = false;
	haveSparseIHess = false;
	haveDenseHess = false;
	haveDenseIHess = false;
	estNonZero = 0;
	minBlockSize = 0;
	maxBlockSize = 0;
}

void FitContext::allocStderrs()
{
	if (stderrs) return;

	size_t numParam = varGroup->vars.size();
	stderrs = new double[numParam];

	for (size_t px=0; px < numParam; ++px) {
		stderrs[px] = NA_REAL;
	}
}

FitContext::FitContext(std::vector<double> &startingValues)
{
	parent = NULL;
	varGroup = Global->freeGroup[FREEVARGROUP_ALL];
	init();

	size_t numParam = varGroup->vars.size();
	if (startingValues.size() != numParam) {
		Rf_error("Got %d starting values for %d parameters",
		      startingValues.size(), numParam);
	}
	memcpy(est, startingValues.data(), sizeof(double) * numParam);
}

FitContext::FitContext(FitContext *parent, FreeVarGroup *varGroup)
{
	this->parent = parent;
	this->varGroup = varGroup;
	init();

	FreeVarGroup *src = parent->varGroup;
	FreeVarGroup *dest = varGroup;
	size_t dvars = varGroup->vars.size();
	if (dvars == 0) return;
	mapToParent.resize(dvars);

	size_t d1 = 0;
	for (size_t s1=0; s1 < src->vars.size(); ++s1) {
		if (src->vars[s1] != dest->vars[d1]) continue;
		mapToParent[d1] = s1;
		est[d1] = parent->est[s1];
		if (++d1 == dvars) break;
	}
	if (d1 != dvars) Rf_error("Parent free parameter group (id=%d) is not a superset of %d",
			       src->id[0], dest->id[0]);

	wanted = parent->wanted;
	infoDefinite = parent->infoDefinite;
	infoCondNum = parent->infoCondNum;
	IterationError = parent->IterationError;
	iterations = parent->iterations;
}

void FitContext::copyParamToModel(omxMatrix *mat)
{ copyParamToModel(mat->currentState); }

void FitContext::copyParamToModel(omxMatrix *mat, double *at)
{ copyParamToModel(mat->currentState, at); }

void FitContext::updateParent()
{
	FreeVarGroup *src = varGroup;
	FreeVarGroup *dest = parent->varGroup;
	size_t svars = varGroup->vars.size();

	parent->wanted |= wanted;
	parent->fit = fit;
	parent->IterationError = IterationError;
	parent->mac = mac;
	parent->infoDefinite = infoDefinite;
	parent->infoCondNum = infoCondNum;
	parent->iterations = iterations;

	// rewrite using mapToParent TODO

	if (svars > 0) {
		size_t s1 = 0;
		for (size_t d1=0; d1 < dest->vars.size(); ++d1) {
			if (dest->vars[d1] != src->vars[s1]) continue;
			parent->est[d1] = est[s1];
			if (++s1 == svars) break;
		}
		if (stderrs) {
			parent->allocStderrs();
			for (size_t s1=0; s1 < src->vars.size(); ++s1) {
				parent->stderrs[mapToParent[s1]] = stderrs[s1];
			}
		}
	}
	
	// pda(est, 1, svars);
	// pda(parent->est, 1, dvars);
}

void FitContext::updateParentAndFree()
{
	updateParent();
	delete this;
}

void FitContext::log(int what)
{
	size_t count = varGroup->vars.size();
	std::string buf;
	if (what & FF_COMPUTE_MAXABSCHANGE) buf += string_snprintf("MAC: %.5f\n", mac);
	if (what & FF_COMPUTE_FIT) buf += string_snprintf("fit: %.5f (scale %f)\n", fit, Global->llScale);
	if (what & FF_COMPUTE_ESTIMATE) {
		buf += string_snprintf("est %d: c(", (int) count);
		for (size_t vx=0; vx < count; ++vx) {
			buf += string_snprintf("%.5f", est[vx]);
			if (vx < count - 1) buf += ", ";
		}
		buf += ")\n";
	}
	if (what & FF_COMPUTE_GRADIENT) {
		buf += string_snprintf("gradient %d: c(", (int) count);
		for (size_t vx=0; vx < count; ++vx) {
			buf += string_snprintf("%.5f", grad[vx]);
			if (vx < count - 1) buf += ", ";
		}
		buf += ")\n";
	}
	if (what & FF_COMPUTE_HESSIAN) {
		refreshDenseHess();
		buf += string_snprintf("hessian %d x %d: c(\n", (int) count, (int) count);
		for (size_t v1=0; v1 < count; ++v1) {
			for (size_t v2=0; v2 < count; ++v2) {
				double coef;
				if (v1 > v2) {
					coef = hess.selfadjointView<Eigen::Upper>()(v2,v1);
				} else {
					coef = hess.selfadjointView<Eigen::Upper>()(v1,v2);
				}
				buf += string_snprintf("%.5f", coef);
				if (v1 < count - 1 || v2 < count - 1) buf += ", ";
			}
			buf += "\n";
		}
		buf += ")\n";
	}
	mxLogBig(buf);
}

void FitContext::recordIterationError(const char* msg, ...)
{
	// could record the parameter vector here for a better error message TODO
	va_list ap;
	va_start(ap, msg);
	std::string str = string_vsnprintf(msg, ap);
	va_end(ap);

#pragma omp critical(IterationError)
	IterationError = str;
}

std::string FitContext::getIterationError()
{
	return IterationError;
}

static void _fixSymmetry(const char *name, double *mat, size_t numParam, bool force)
{
	for (size_t h1=1; h1 < numParam; h1++) {
		for (size_t h2=0; h2 < h1; h2++) {
			if (!force && mat[h2 * numParam + h1] != 0) {
				omxRaiseErrorf("%s is not upper triangular", name);
				break;
			}
			mat[h2 * numParam + h1] = mat[h1 * numParam + h2];
		}
	}
}

static void omxRepopulateRFitFunction(omxFitFunction* oo, double* x, int n)
{
	omxRFitFunction* rFitFunction = (omxRFitFunction*)oo->argStruct;

	SEXP theCall, estimate;

	Rf_protect(estimate = Rf_allocVector(REALSXP, n));
	double *est = REAL(estimate);
	for(int i = 0; i < n ; i++) {
		est[i] = x[i];
	}

	Rf_protect(theCall = Rf_allocVector(LANGSXP, 4));

	SETCAR(theCall, Rf_install("imxUpdateModelValues"));
	SETCADR(theCall, rFitFunction->model);
	SETCADDR(theCall, rFitFunction->flatModel);
	SETCADDDR(theCall, estimate);

	R_Reprotect(rFitFunction->model = Rf_eval(theCall, R_GlobalEnv), rFitFunction->modelIndex);

	Rf_unprotect(2); // theCall, estimate

	omxMarkDirty(oo->matrix);
}

void FitContext::copyParamToModel(omxState* os)
{
	copyParamToModel(os, est);
}

void FitContext::copyParamToModel(omxState* os, double *at)
{
	size_t numParam = varGroup->vars.size();

	if(numParam == 0) return;

	// Confidence Intervals & Hessian Calculation probe the parameter space
	// near the best estimate. If stale, we need to restore the best estimate
	// before returning results to the user.
	os->stale = at != est;

	if(OMX_VERBOSE) {
		std::string buf;
		buf += string_snprintf("copyParamToModel: %d(%d) ", iterations, Global->computeCount);
		buf += ("Estimates: [");
		for(size_t k = 0; k < numParam; k++) {
			buf += string_snprintf(" %f", at[k]);
		}
		buf += ("]\n");
		mxLogBig(buf);
	}

	for(size_t k = 0; k < numParam; k++) {
		omxFreeVar* freeVar = varGroup->vars[k];
		for(size_t l = 0; l < freeVar->locations.size(); l++) {
			omxFreeVarLocation *loc = &freeVar->locations[l];
			omxMatrix *matrix = os->matrixList[loc->matrix];
			int row = loc->row;
			int col = loc->col;
			omxSetMatrixElement(matrix, row, col, at[k]);
			if(OMX_DEBUG) {
				mxLog("free var %d, matrix %s[%d, %d] = %f",
				      (int) k, matrix->name, row, col, at[k]);
			}
		}
	}

	if (RFitFunction) omxRepopulateRFitFunction(RFitFunction, at, numParam);

	varGroup->markDirty(os);

	if (os->childList.size() == 0) return;

	for(size_t i = 0; i < os->childList.size(); i++) {
		copyParamToModel(os->childList[i], at);
	}
}

double *FitContext::take(int want)
{
	if (!(want & (wanted | FF_COMPUTE_ESTIMATE))) {
		Rf_error("Attempt to take %d but not available", want);
	}

	double *ret = NULL;
	switch(want) {
	case FF_COMPUTE_ESTIMATE:
		ret = est;
		est = NULL;
		break;
	default:
		Rf_error("Taking of %d is not implemented", want);
	}
	if (!ret) Rf_error("Attempt to take %d, already taken", want);
	return ret;
}

// Rethink this whole design TODO
// If we compute things blockwise then most of this can go away?
void FitContext::preInfo()
{
	size_t npsq = numParam * numParam;

	if (!infoA) infoA = new double[npsq];
	if (!infoB) infoB = new double[npsq];

	switch (infoMethod) {
	case INFO_METHOD_SANDWICH:
	case INFO_METHOD_MEAT:
		OMXZERO(infoB, npsq);
	case INFO_METHOD_BREAD:
		OMXZERO(infoA, npsq);
		break;
	case INFO_METHOD_HESSIAN:
		clearHessian();
		break;
	default:
		Rf_error("Unknown information matrix estimation method %d", infoMethod);
	}
}

void FitContext::postInfo()
{
	size_t numParam = varGroup->vars.size();
	switch (infoMethod) {
	case INFO_METHOD_SANDWICH:{
		// move into FCDeriv TODO
		omxBuffer<double> work(numParam * numParam);
		Matrix amat(infoA, numParam, numParam);
		InvertSymmetricIndef(amat, 'U');
		_fixSymmetry("InfoB", infoB, numParam, false);
		Matrix bmat(infoB, numParam, numParam);
		Matrix wmat(work.data(), numParam, numParam);
		Matrix hmat(getDenseIHessUninitialized(), numParam, numParam);
		SymMatrixMultiply('L', 'U', 1, 0, amat, bmat, wmat);
		SymMatrixMultiply('R', 'U', 1, 0, amat, wmat, hmat);
		wanted |= FF_COMPUTE_IHESSIAN;
		break;}
	case INFO_METHOD_MEAT:{
		memcpy(getDenseHessUninitialized(), infoB, sizeof(double) * numParam * numParam); // avoid copy TODO
		wanted |= FF_COMPUTE_HESSIAN;
		break;}
	case INFO_METHOD_BREAD:{
		memcpy(getDenseHessUninitialized(), infoA, sizeof(double) * numParam * numParam); // avoid copy TODO
		wanted |= FF_COMPUTE_HESSIAN;
		break;}
	case INFO_METHOD_HESSIAN:
		if (Global->llScale > 0) negateHessian();
		wanted |= FF_COMPUTE_HESSIAN;
		break;
	default:
		Rf_error("Unknown information matrix estimation method %d", infoMethod);
	}
}

FitContext::~FitContext()
{
	clearHessian();
	if (est) delete [] est;
	if (stderrs) delete [] stderrs;
	if (infoA) delete [] infoA;
	if (infoB) delete [] infoB;
}

omxFitFunction *FitContext::RFitFunction = NULL;

void FitContext::setRFitFunction(omxFitFunction *rff)
{
	if (rff) {
		Global->numThreads = 1;
		if (RFitFunction) {
			Rf_error("You can only create 1 MxRFitFunction per independent model");
		}
	}
	RFitFunction = rff;
}

Ramsay1975::Ramsay1975(FitContext *fc, const char *flavor, int verbose, double minCaution) :
	fc(fc), flavor(flavor), verbose(verbose), minCaution(minCaution)
{
	if (!flavor) Rf_error("Ramsay: flavor cannot be NULL");

	maxCaution = 0.0;
	boundsHit = 0;
	caution = 0;
	highWatermark = std::max(0.5, caution);  // arbitrary guess

	numParam = fc->varGroup->vars.size();
	for (size_t px=0; px < numParam; ++px) {
		if (strcmp(fc->flavor[px], flavor) != 0) continue;
		vars.push_back(px);
	}

	prevAdj1.assign(numParam, 0);
	prevAdj2.resize(numParam);
	prevEst.resize(numParam);
	memcpy(prevEst.data(), fc->est, sizeof(double) * numParam);

	if (verbose >= 2) {
		mxLog("Ramsay[%10s]: %d parameters, caution %f, min caution %f",
		      flavor, (int)vars.size(), caution, minCaution);
	}
}

void Ramsay1975::recordEstimate(int px, double newEst)
{
	omxFreeVar *fv = fc->varGroup->vars[px];
	bool hitBound=false;
	double param = newEst;
	if (param < fv->lbound) {
		hitBound=true;
		param = prevEst[px] - (prevEst[px] - fv->lbound) / 2;
	}
	if (param > fv->ubound) {
		hitBound=true;
		param = prevEst[px] + (fv->ubound - prevEst[px]) / 2;
	}
	boundsHit += hitBound;
	
	prevAdj2[px] = prevAdj1[px];
	prevAdj1[px] = param - prevEst[px];
	
	if (verbose >= 4) {
		std::string buf;
		buf += string_snprintf("Ramsay[%10s]: %d~%s %.4f -> %.4f", flavor, px, fv->name, prevEst[px], param);
		if (hitBound) {
			buf += string_snprintf(" wanted %.4f but hit bound", newEst);
		}
		if (prevAdj1[px] * prevAdj2[px] < 0) {
			buf += " *OSC*";
		}
		buf += "\n";
		mxLogBig(buf);
	}

	fc->est[px] = param;
	prevEst[px] = param;
}

void Ramsay1975::apply()
{
	for (size_t px=0; px < vars.size(); ++px) {
		int vx = vars[px];
		recordEstimate(vx, (1 - caution) * fc->est[vx] + caution * prevEst[vx]);
	}
}

void Ramsay1975::recalibrate(bool *restart)
{
	if (vars.size() == 0) return;

	double normPrevAdj2 = 0;
	double normAdjDiff = 0;
	std::vector<double> adjDiff(numParam);

	// The choice of norm is also arbitrary. Other norms might work better.
	for (size_t vx=0; vx < vars.size(); ++vx) {
		int px = vars[vx];
		adjDiff[px] = prevAdj1[px] - prevAdj2[px];
		normPrevAdj2 += prevAdj2[px] * prevAdj2[px];
	}

	for (size_t vx=0; vx < vars.size(); ++vx) {
		int px = vars[vx];
		normAdjDiff += adjDiff[px] * adjDiff[px];
	}
	if (normAdjDiff == 0) {
		return;
		//Rf_error("Ramsay: no free variables of flavor %d", flavor);
	}

	double ratio = sqrt(normPrevAdj2 / normAdjDiff);
	//if (verbose >= 3) mxLog("Ramsay[%d]: sqrt(%.5f/%.5f) = %.5f",
	// flavor, normPrevAdj2, normAdjDiff, ratio);

	double newCaution = 1 - (1-caution) * ratio / (1+boundsHit);
	if (newCaution > .95) newCaution = .95;  // arbitrary guess
	if (newCaution < 0) newCaution /= 2;     // don't get overconfident
	if (newCaution < minCaution) newCaution = minCaution;
	if (newCaution < caution) {
		caution = newCaution/3 + 2*caution/3;  // don't speed up too fast, arbitrary ratio
	} else {
		caution = newCaution;
	}
	maxCaution = std::max(maxCaution, caution);
	goingWild = false;
	if (caution < highWatermark || (normPrevAdj2 < 1e-3 && normAdjDiff < 1e-3)) {
		if (verbose >= 3) mxLog("Ramsay[%10s]: %.2f caution", flavor, caution);
	} else {
		if (verbose >= 3) {
			mxLog("Ramsay[%10s]: caution %.2f > %.2f, extreme oscillation, restart recommended",
			      flavor, caution, highWatermark);
		}
		*restart = TRUE;
		goingWild = true;
	}
	highWatermark += .02; // arbitrary guess
	boundsHit = 0;
}

void Ramsay1975::restart(bool myFault)
{
	memcpy(prevEst.data(), fc->est, sizeof(double) * numParam);
	prevAdj1.assign(numParam, 0);
	prevAdj2.assign(numParam, 0);
	myFault |= goingWild;
	if (myFault) {
		highWatermark = 1 - (1 - highWatermark) * .5; // arbitrary guess
		caution = std::max(caution, highWatermark);   // arbitrary guess
		maxCaution = std::max(maxCaution, caution);
		highWatermark = caution;
	}
	if (vars.size() && verbose >= 3) {
		mxLog("Ramsay[%10s]: restart%s with %.2f caution %.2f highWatermark",
		      flavor, myFault? " (my fault)":"", caution, highWatermark);
	}
}

omxCompute::omxCompute()
{
	varGroup = NULL;
}

void omxCompute::collectResultsHelper(FitContext *fc, std::vector< omxCompute* > &clist,
				      LocalComputeResult *lcr, MxRList *out)
{
	for (std::vector< omxCompute* >::iterator it = clist.begin(); it != clist.end(); ++it) {
		omxCompute *c1 = *it;
		c1->collectResults(fc, lcr, out);
	}
}

void omxCompute::collectResults(FitContext *fc, LocalComputeResult *lcr, MxRList *out)
{
	MxRList *slots = new MxRList();
        reportResults(fc, slots, out);
	if (slots->size()) {
		lcr->push_back(std::make_pair(computeId, slots));
	} else {
		delete slots;
	}
}

omxCompute::~omxCompute()
{}

void omxCompute::initFromFrontend(SEXP rObj)
{
	SEXP slotValue;
	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("id")));
	if (Rf_length(slotValue) != 1) Rf_error("MxCompute has no ID");

	computeId = INTEGER(slotValue)[0];
	varGroup = Global->findVarGroup(computeId);

	if (!varGroup) {
		Rf_protect(slotValue = R_do_slot(rObj, Rf_install("freeSet")));
		if (Rf_length(slotValue) == 0) {
			varGroup = Global->findVarGroup(FREEVARGROUP_NONE);
		} else if (strcmp(CHAR(STRING_ELT(slotValue, 0)), ".")==0) {
			varGroup = Global->freeGroup[FREEVARGROUP_ALL];
		} else {
			Rf_warning("MxCompute ID %d references matrix '%s' in its freeSet "
				"but this matrix contains no free parameters",
				computeId, CHAR(STRING_ELT(slotValue, 0)));
			varGroup = Global->findVarGroup(FREEVARGROUP_NONE);
		}
	}
	if (OMX_DEBUG) {
		mxLog("MxCompute id %d assigned to var group %d", computeId, varGroup->id[0]);
	}
}

void omxCompute::compute(FitContext *fc)
{
	ComputeInform origInform = fc->inform;
	FitContext *narrow = fc;
	if (fc->varGroup != varGroup) narrow = new FitContext(fc, varGroup);
	narrow->inform = INFORM_UNINITIALIZED;
	computeImpl(narrow);
	fc->inform = std::max(origInform, narrow->inform);
	if (fc->varGroup != varGroup) narrow->updateParentAndFree();
	Global->checkpointMessage(fc, fc->est, "%s", name);
}

class ComputeContainer : public omxCompute {
	typedef omxCompute super;
protected:
	std::vector< omxCompute* > clist;
public:
	virtual void collectResults(FitContext *fc, LocalComputeResult *lcr, MxRList *out);
};

void ComputeContainer::collectResults(FitContext *fc, LocalComputeResult *lcr, MxRList *out)
{
	super::collectResults(fc, lcr, out);
	collectResultsHelper(fc, clist, lcr, out);
}

class omxComputeSequence : public ComputeContainer {
	typedef ComputeContainer super;

 public:
	virtual void initFromFrontend(SEXP rObj);
        virtual void computeImpl(FitContext *fc);
	virtual ~omxComputeSequence();
};

class omxComputeIterate : public ComputeContainer {
	typedef ComputeContainer super;
	int maxIter;
	double tolerance;
	int verbose;

 public:
        virtual void initFromFrontend(SEXP rObj);
        virtual void computeImpl(FitContext *fc);
	virtual ~omxComputeIterate();
};

class omxComputeOnce : public omxCompute {
	typedef omxCompute super;
	std::vector< omxMatrix* > algebras;
	std::vector< omxExpectation* > expectations;
	std::vector< const char* > predict;
	const char *how;
	int verbose;
	bool mac;
	bool starting;
	bool fit;
	bool gradient;
	bool hessian;
	bool ihessian;
	bool infoMat;
	enum ComputeInfoMethod infoMethod;
	bool hgprod;
	bool isBestFit; // for backward compatibility

 public:
        virtual void initFromFrontend(SEXP rObj);
        virtual void computeImpl(FitContext *fc);
        virtual void reportResults(FitContext *fc, MxRList *slots, MxRList *out);
};

class ComputeEM : public omxCompute {
	typedef omxCompute super;
	std::vector< omxExpectation* > expectations;
	const char *predict;
	omxCompute *fit1;  // maybe rename to mstep TODO
	omxMatrix *fit3;   // rename to observedFit
	int EMcycles;
	int maxIter;
	int mstepIter;
	int totalMstepIter;
	double tolerance;
	double semTolerance;
	int verbose;
	bool useRamsay;
	bool information;
	std::vector< omxMatrix * > semFitFunction;
	enum ComputeInfoMethod infoMethod;
	enum SEMMethod { ClassicSEM, TianSEM, GridSEM, AgileSEM } semMethod;
	double *semMethodData;
	int semMethodLen;
	bool semDebug;
	bool semFixSymmetry;
	bool semForcePD;
	int agileMaxIter;
	SEXP rateMatrix; //debug
	SEXP inputInfoMatrix; //debug
	SEXP outputInfoMatrix; //debug
	SEXP origEigenvalues; //debug
	std::vector<Ramsay1975*> ramsay;
	double noiseTarget;
	double noiseTolerance;
	std::vector<double*> estHistory;
	std::vector<double> probeOffset;
	std::vector<double> diffWork;
	std::vector<int> paramHistLen;
	std::vector<double> optimum;
	double bestFit;
 	static const double MIDDLE_START;
	static const double MIDDLE_END;
	size_t maxHistLen;
	int semProbeCount;

	void setExpectationPrediction(const char *context);
	void probeEM(FitContext *fc, int vx, double offset, std::vector<double> *rijWork);
	void recordDiff(FitContext *fc, int v1, std::vector<double> &rijWork,
			double *stdDiff, bool *mengOK);

 public:
        virtual void initFromFrontend(SEXP rObj);
        virtual void computeImpl(FitContext *fc);
	virtual void collectResults(FitContext *fc, LocalComputeResult *lcr, MxRList *out);
        virtual void reportResults(FitContext *fc, MxRList *slots, MxRList *out);
	virtual ~ComputeEM();
};

const double ComputeEM::MIDDLE_START = 0.105360515657826281366; // -log(.9) constexpr
const double ComputeEM::MIDDLE_END = 0.001000500333583534363566; // -log(.999) constexpr

class ComputeStandardError : public omxCompute {
	typedef omxCompute super;
 public:
        virtual void reportResults(FitContext *fc, MxRList *slots, MxRList *out);
};

class ComputeHessianQuality : public omxCompute {
	typedef omxCompute super;
 public:
        virtual void reportResults(FitContext *fc, MxRList *slots, MxRList *out);
};

class ComputeReportDeriv : public omxCompute {
	typedef omxCompute super;
 public:
        virtual void reportResults(FitContext *fc, MxRList *slots, MxRList *out);
};

static class omxCompute *newComputeSequence()
{ return new omxComputeSequence(); }

static class omxCompute *newComputeIterate()
{ return new omxComputeIterate(); }

static class omxCompute *newComputeOnce()
{ return new omxComputeOnce(); }

static class omxCompute *newComputeEM()
{ return new ComputeEM(); }

static class omxCompute *newComputeStandardError()
{ return new ComputeStandardError(); }

static class omxCompute *newComputeHessianQuality()
{ return new ComputeHessianQuality(); }

static class omxCompute *newComputeReportDeriv()
{ return new ComputeReportDeriv(); }

struct omxComputeTableEntry {
        char name[32];
        omxCompute *(*ctor)();
};

static const struct omxComputeTableEntry omxComputeTable[] = {
        {"MxComputeNumericDeriv", &newComputeNumericDeriv},
        {"MxComputeGradientDescent", &newComputeGradientDescent},
	{"MxComputeSequence", &newComputeSequence },
	{"MxComputeIterate", &newComputeIterate },
	{"MxComputeOnce", &newComputeOnce },
        {"MxComputeNewtonRaphson", &newComputeNewtonRaphson},
        {"MxComputeEM", &newComputeEM },
	{"MxComputeStandardError", &newComputeStandardError},
	{"MxComputeHessianQuality", &newComputeHessianQuality},
	{"MxComputeReportDeriv", &newComputeReportDeriv},
	{"MxComputeConfidenceInterval", &newComputeConfidenceInterval}
};

omxCompute *omxNewCompute(omxState* os, const char *type)
{
        omxCompute *got = NULL;

        for (size_t fx=0; fx < OMX_STATIC_ARRAY_SIZE(omxComputeTable); fx++) {
                const struct omxComputeTableEntry *entry = omxComputeTable + fx;
                if(strcmp(type, entry->name) == 0) {
                        got = entry->ctor();
			got->name = entry->name;
                        break;
                }
        }

        if (!got) Rf_error("Compute %s is not implemented", type);

        return got;
}

void omxComputeSequence::initFromFrontend(SEXP rObj)
{
	super::initFromFrontend(rObj);

	SEXP slotValue;
	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("steps")));

	for (int cx = 0; cx < Rf_length(slotValue); cx++) {
		SEXP step = VECTOR_ELT(slotValue, cx);
		SEXP s4class;
		Rf_protect(s4class = STRING_ELT(Rf_getAttrib(step, Rf_install("class")), 0));
		omxCompute *compute = omxNewCompute(globalState, CHAR(s4class));
		compute->initFromFrontend(step);
		if (isErrorRaised()) break;
		clist.push_back(compute);
	}
}

void omxComputeSequence::computeImpl(FitContext *fc)
{
	for (size_t cx=0; cx < clist.size(); ++cx) {
		clist[cx]->compute(fc);
		if (isErrorRaised()) break;
	}
}

omxComputeSequence::~omxComputeSequence()
{
	for (size_t cx=0; cx < clist.size(); ++cx) {
		delete clist[cx];
	}
}

void omxComputeIterate::initFromFrontend(SEXP rObj)
{
	SEXP slotValue;

	super::initFromFrontend(rObj);

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("maxIter")));
	maxIter = INTEGER(slotValue)[0];

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("tolerance")));
	tolerance = REAL(slotValue)[0];
	if (tolerance <= 0) Rf_error("tolerance must be positive");

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("steps")));

	for (int cx = 0; cx < Rf_length(slotValue); cx++) {
		SEXP step = VECTOR_ELT(slotValue, cx);
		SEXP s4class;
		Rf_protect(s4class = STRING_ELT(Rf_getAttrib(step, Rf_install("class")), 0));
		omxCompute *compute = omxNewCompute(globalState, CHAR(s4class));
		compute->initFromFrontend(step);
		if (isErrorRaised()) break;
		clist.push_back(compute);
	}

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("verbose")));
	verbose = Rf_asInteger(slotValue);
}

void omxComputeIterate::computeImpl(FitContext *fc)
{
	int iter = 0;
	double prevFit = 0;
	double mac = tolerance * 10;
	while (1) {
		++fc->iterations;
		for (size_t cx=0; cx < clist.size(); ++cx) {
			clist[cx]->compute(fc);
			if (isErrorRaised()) break;
		}
		if (fc->wanted & FF_COMPUTE_MAXABSCHANGE) {
			if (fc->mac < 0) {
				Rf_warning("MAC estimated at %.4f; something is wrong", fc->mac);
				break;
			} else {
				mac = fc->mac;
				if (verbose) mxLog("ComputeIterate: mac %.9g", mac);
			}
		}
		if (fc->wanted & FF_COMPUTE_FIT) {
			if (fc->fit == 0) {
				Rf_warning("Fit estimated at 0; something is wrong");
				break;
			}
			if (prevFit != 0) {
				double change = prevFit - fc->fit;
				if (verbose) mxLog("ComputeIterate: fit %.9g change %.9g", fc->fit, change);
				mac = fabs(change);
			} else {
				if (verbose) mxLog("ComputeIterate: initial fit %.9g", fc->fit);
			}
			prevFit = fc->fit;
		}
		if (!(fc->wanted & (FF_COMPUTE_MAXABSCHANGE | FF_COMPUTE_FIT))) {
			omxRaiseErrorf("ComputeIterate: neither MAC nor fit available");
		}
		if (isErrorRaised() || ++iter > maxIter || mac < tolerance) break;
	}
}

omxComputeIterate::~omxComputeIterate()
{
	for (size_t cx=0; cx < clist.size(); ++cx) {
		delete clist[cx];
	}
}

void ComputeEM::initFromFrontend(SEXP rObj)
{
	SEXP slotValue;
	SEXP s4class;

	super::initFromFrontend(rObj);

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("expectation")));
	for (int wx=0; wx < Rf_length(slotValue); ++wx) {
		int objNum = INTEGER(slotValue)[wx];
		omxExpectation *expectation = globalState->expectationList[objNum];
		setFreeVarGroup(expectation, varGroup);
		omxCompleteExpectation(expectation);
		expectations.push_back(expectation);
	}

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("predict")));
	{
		// Should accept a vector here TODO
		if (Rf_length(slotValue) != 1) Rf_error("Not implemented");
		SEXP elem;
		Rf_protect(elem = STRING_ELT(slotValue, 0));
		predict = CHAR(elem);
	}

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("mstep")));
	Rf_protect(s4class = STRING_ELT(Rf_getAttrib(slotValue, Rf_install("class")), 0));
	fit1 = omxNewCompute(globalState, CHAR(s4class));
	fit1->initFromFrontend(slotValue);

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("observedFit")));
	fit3 = globalState->algebraList[ INTEGER(slotValue)[0] ];
	if (fit3->fitFunction) {
		setFreeVarGroup(fit3->fitFunction, varGroup);
		omxCompleteFitFunction(fit3);
	}

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("maxIter")));
	maxIter = INTEGER(slotValue)[0];

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("tolerance")));
	tolerance = REAL(slotValue)[0];
	if (tolerance <= 0) Rf_error("tolerance must be positive");

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("verbose")));
	verbose = Rf_asInteger(slotValue);

	useRamsay = false;
	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("accel")));
	const char *accelName = CHAR(STRING_ELT(slotValue, 0));
	if (strEQ(accelName, "ramsay1975")) {
		useRamsay = true;
	} else if (STRING_ELT(slotValue, 0) == NA_STRING || strEQ(accelName, "")) {
		// OK
	} else {
		Rf_warning("%s: unknown acceleration method %s ignored", name, accelName);
	}

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("information")));
	const char *infoName = CHAR(STRING_ELT(slotValue, 0));
	information = false;
	if (STRING_ELT(slotValue, 0) == NA_STRING) {
		// ok
	} else if (strEQ(infoName, "mr1991")) {
		information = true;
	} else {
		Rf_warning("%s: unknown information method %s ignored", name, infoName);
	}

	if (information) {
		infoMethod = INFO_METHOD_HESSIAN;
		semMethod = AgileSEM;
		agileMaxIter = 1;
		semDebug = false;
		semFixSymmetry = true;
		semForcePD = false;
		noiseTarget = exp(-5.0); //constexpr
		noiseTolerance = exp(3.3); //constexpr
		semTolerance = sqrt(tolerance);  // override needed?

		SEXP infoArgs, argNames;
		Rf_protect(infoArgs = R_do_slot(rObj, Rf_install("infoArgs")));
		Rf_protect(argNames = Rf_getAttrib(infoArgs, R_NamesSymbol));

		for (int ax=0; ax < Rf_length(infoArgs); ++ax) {
			const char *key = R_CHAR(STRING_ELT(argNames, ax));
			slotValue = VECTOR_ELT(infoArgs, ax);
			if (strEQ(key, "fitfunction")) {
				for (int fx=0; fx < Rf_length(slotValue); ++fx) {
					omxMatrix *ff = globalState->algebraList[INTEGER(slotValue)[fx]];
					if (!ff->fitFunction) Rf_error("infoArgs$fitfunction is %s, not a fitfunction", ff->name);
					semFitFunction.push_back(ff);
				}
			} else if (strEQ(key, "inputInfo")) {
				infoMethod = stringToInfoMethod(CHAR(slotValue));
			} else if (strEQ(key, "semMethod")) {
				semMethodLen = Rf_length(slotValue);
				if (semMethodLen == 0) {
					semMethod = AgileSEM;
					semMethodData = NULL;
				} else {
					semMethodData = REAL(slotValue);
					if (semMethodLen > 1) {
						semMethod = GridSEM;
					} else if (semMethodData[0] == 1) {
						semMethod = ClassicSEM;
					} else if (semMethodData[0] == 2) {
						semMethod = TianSEM;
					} else if (semMethodData[0] == 3) {
						semMethod = AgileSEM;
					} else {
						Rf_error("Unknown SEM method %f", semMethodData[0]);
					}
				}
			} else if (strEQ(key, "agileMaxIter")) {
				agileMaxIter = INTEGER(slotValue)[0];
			} else if (strEQ(key, "semDebug")) {
				semDebug = Rf_asLogical(slotValue);
			} else if (strEQ(key, "semFixSymmetry")) {
				semFixSymmetry = Rf_asLogical(slotValue);
			} else if (strEQ(key, "semForcePD")) {
				semForcePD = Rf_asLogical(slotValue);
			} else if (strEQ(key, "noiseTarget")) {
				noiseTarget = REAL(slotValue)[0];
				if (noiseTarget <= 0) Rf_error("noiseTarget must be positive");
			} else if (strEQ(key, "noiseTolerance")) {
				noiseTolerance = REAL(slotValue)[0];
				if (noiseTolerance < 1) Rf_error("noiseTolerance must be >=1");
			} else {
				mxLog("%s: unknown key %s", name, key);
			}
		}
		if (!semFixSymmetry && semForcePD) {
			Rf_warning("%s: semFixSymmetry must be enabled for semForcePD", name);
			semForcePD = false;
		}
	}

	inputInfoMatrix = NULL;
	outputInfoMatrix = NULL;
	rateMatrix = NULL;
	origEigenvalues = NULL;
}

void ComputeEM::setExpectationPrediction(const char *context)
{
	for (size_t wx=0; wx < expectations.size(); ++wx) {
		omxExpectation *expectation = expectations[wx];
		if (verbose >= 4) mxLog("ComputeEM: expectation[%d] %s predict %s", (int) wx, expectation->name, context);
		omxExpectationCompute(expectation, context);
	}
}

void ComputeEM::probeEM(FitContext *fc, int vx, double offset, std::vector<double> *rijWork)
{
	const size_t freeVars = fc->varGroup->vars.size();
	const int base = paramHistLen[vx] * freeVars;
	probeOffset[vx * maxHistLen + paramHistLen[vx]] = offset;
	paramHistLen[vx] += 1;

	memcpy(fc->est, optimum.data(), sizeof(double) * freeVars);
	fc->est[vx] += offset;
	fc->copyParamToModel(globalState);

	setExpectationPrediction(predict);
	int informSave = fc->inform;  // not sure if we want to hide inform here TODO
	fit1->compute(fc);
	fc->inform = informSave;
	setExpectationPrediction("nothing");

	if (verbose >= 3) mxLog("ComputeEM: probe %d of param %d offset %.6f",
				paramHistLen[vx], vx, offset);

	for (size_t v1=0; v1 < freeVars; ++v1) {
		double got = (fc->est[v1] - optimum[v1]) / offset;
		(*rijWork)[base + v1] = got;
	}
	//pda(rij.data() + base, 1, freeVars);
	++semProbeCount;
}

void ComputeEM::recordDiff(FitContext *fc, int v1, std::vector<double> &rijWork,
			   double *stdDiff, bool *mengOK)
{
	const size_t freeVars = fc->varGroup->vars.size();
	int h1 = paramHistLen[v1]-2;
	int h2 = paramHistLen[v1]-1;
	double *rij1 = rijWork.data() + h1 * freeVars;
	double *rij2 = rijWork.data() + h2 * freeVars;
	double diff = 0;
	*mengOK = true;
	for (size_t v2=0; v2 < freeVars; ++v2) {
		double diff1 = fabs(rij1[v2] - rij2[v2]);
		if (diff1 >= semTolerance) *mengOK = false;
		diff += diff1;
	}
	double p1 = probeOffset[v1 * maxHistLen + h1];
	double p2 = probeOffset[v1 * maxHistLen + h2];
	double dist = fabs(p1 - p2);
	if (dist < tolerance/4) Rf_error("SEM: invalid probe offset distance %.9f", dist);
	*stdDiff = diff / (freeVars * dist);
	diffWork[v1 * maxHistLen + h1] = *stdDiff;
	if (verbose >= 2) mxLog("ComputeEM: (%f,%f) mengOK %d diff %f stdDiff %f",
				p1, p2, *mengOK, diff / freeVars, *stdDiff);
}

void ComputeEM::computeImpl(FitContext *fc)
{
	const double Scale = fabs(Global->llScale);
	double prevFit = 0;
	double mac = tolerance * 10;
	bool converged = false;
	const size_t freeVars = fc->varGroup->vars.size();
	bool in_middle = false;
	maxHistLen = 0;
	EMcycles = 0;
	semProbeCount = 0;

	if (verbose >= 1) mxLog("ComputeEM: Welcome, tolerance=%g ramsay=%d info=%d",
				tolerance, useRamsay, information);

	// Some evidence suggests that better performance is obtained when the
	// item parameters and latent distribution parameters are split into
	// separate Ramsay1975 groups with different minimum caution limits,
	//
	// ramsay.push_back(new Ramsay1975(fc, 1+int(ramsay.size()), 0, verbose, -1.25)); // M-step param
	// ramsay.push_back(new Ramsay1975(fc, 1+int(ramsay.size()), 0, verbose, -1));    // extra param
	//
	// I had this hardcoded for a while, but in making the API more generic,
	// I'm not sure how to allow specification of the Ramsay1975 grouping.
	// One possibility is list(flavor1=c("ItemParam"), flavor2=c("mean","cov"))
	// but this doesn't allow finer grain than matrix-wise assignment. The
	// other question is whether more Ramsay1975 groups really help or not.
	// nightly/ifa-cai2009.R actually got faster with 1 Ramsay group.

	const char *flavor = "EM";
	fc->flavor.assign(freeVars, flavor);
	ramsay.push_back(new Ramsay1975(fc, flavor, verbose, -1.25));

	while (1) {
		if (verbose >= 4) mxLog("ComputeEM[%d]: E-step", EMcycles);
		setExpectationPrediction(predict);

		{
			if (verbose >= 4) mxLog("ComputeEM[%d]: M-step", EMcycles);
			int informSave = fc->inform;
			FitContext *fc1 = new FitContext(fc, fit1->varGroup);
			int startIter = fc1->iterations;
			fit1->compute(fc1);
			mstepIter = fc1->iterations - startIter;
			fc1->updateParentAndFree();
			fc->inform = informSave;
		}

		{
			if (useRamsay) {
				bool wantRestart;
				if (EMcycles > 3 && EMcycles % 3 == 0) {
					for (size_t rx=0; rx < ramsay.size(); ++rx) {
						ramsay[rx]->recalibrate(&wantRestart);
					}
				}
				for (size_t rx=0; rx < ramsay.size(); ++rx) {
					ramsay[rx]->apply();
				}
			}
			fc->copyParamToModel(globalState);
			if (verbose >= 4) mxLog("ComputeEM[%d]: observed fit", EMcycles);
			setExpectationPrediction("nothing");

			ComputeFit(fit3, FF_COMPUTE_FIT, fc);
		}

		totalMstepIter += mstepIter;

		if (!(fc->wanted & FF_COMPUTE_FIT)) {
			omxRaiseErrorf("ComputeEM: fit not available");
			break;
		}
		if (fc->fit == 0) {
			omxRaiseErrorf("Fit estimated at 0; something is wrong");
			break;
		}
		double change = 0;
		if (prevFit != 0) {
			change = prevFit - fc->fit;
			if (verbose >= 2) mxLog("ComputeEM[%d]: msteps %d fit %.9g change %.9g",
						EMcycles, mstepIter, fc->fit, change);
			mac = fabs(change);
			if (mac < MIDDLE_START * Scale) in_middle = true;
			if (mac < MIDDLE_END * Scale) in_middle = false;
		} else {
			if (verbose >= 2) mxLog("ComputeEM: msteps %d initial fit %.9g",
						mstepIter, fc->fit);
		}

		prevFit = fc->fit;
		converged = mac < tolerance;
		++fc->iterations;
		if (isErrorRaised() || ++EMcycles > maxIter || converged) break;

		if (semMethod == ClassicSEM || ((semMethod == TianSEM || semMethod == AgileSEM) && in_middle)) {
			double *estCopy = new double[freeVars];
			memcpy(estCopy, fc->est, sizeof(double) * freeVars);
			estHistory.push_back(estCopy);
		}
	}

	int wanted = FF_COMPUTE_FIT | FF_COMPUTE_BESTFIT | FF_COMPUTE_ESTIMATE;
	fc->wanted = wanted;
	bestFit = fc->fit;
	if (verbose >= 1) mxLog("ComputeEM: cycles %d/%d total mstep %d fit %f",
				EMcycles, maxIter, totalMstepIter, bestFit);

	if (!converged || !information) return;

	if (verbose >= 1) mxLog("ComputeEM: tolerance=%f semMethod=%d, semTolerance=%f ideal noise=[%f,%f]",
				tolerance, semMethod, semTolerance,
				noiseTarget/noiseTolerance, noiseTarget*noiseTolerance);

	optimum.resize(freeVars);
	memcpy(optimum.data(), fc->est, sizeof(double) * freeVars);

	if (semMethod == AgileSEM) {
		maxHistLen = 2 + agileMaxIter * 2;
	} else if (semMethod == ClassicSEM || semMethod == TianSEM) {
		maxHistLen = estHistory.size();
	} else {
		maxHistLen = semMethodLen;
	}

	probeOffset.resize(maxHistLen * freeVars);
	diffWork.resize(maxHistLen * freeVars);
	paramHistLen.assign(freeVars, 0);
	omxBuffer<double> rij(freeVars * freeVars);

	size_t semConverged=0;
	for (size_t v1=0; v1 < freeVars; ++v1) {
		std::vector<double> rijWork(freeVars * maxHistLen);
		int pick = 0;
		bool paramConverged = false;
		if (semMethod == AgileSEM) {
			const double stepSize = tolerance;

			double offset1 = tolerance * 50;
			double sign = 1;
			if (estHistory.size()) {
				int hpick = estHistory.size() /2;
				double popt = optimum[v1];
				sign = (popt < estHistory[hpick][v1])? 1 : -1;
				offset1 = fabs(estHistory[hpick][v1] - popt);
				if (offset1 < 10 * tolerance) offset1 = 10 * tolerance;
				if (offset1 > 1000 * tolerance) offset1 = 1000 * tolerance;
			}

			probeEM(fc, v1, sign * offset1, &rijWork);
			double offset2 = offset1 + stepSize;
			probeEM(fc, v1, sign * offset2, &rijWork);
			double diff;
			bool mengOK;
			recordDiff(fc, v1, rijWork, &diff, &mengOK);
			double midOffset = (offset1 + offset2) / 2;

			int iter = 0;
			omxBuffer<double> coefHist(agileMaxIter);
			while (++iter <= agileMaxIter &&
			       !(noiseTarget/noiseTolerance < diff && diff < noiseTarget*noiseTolerance)) {
				coefHist[iter-1] = diff * midOffset * midOffset;
				double coef = 0;
				for (int cx=0; cx < iter; ++cx) coef += coefHist[cx];
				coef /= iter;
				if (verbose >= 4) mxLog("ComputeEM: agile iter[%d] coef=%.6g", iter, coef);
				offset1 = sqrt(coef/noiseTarget);
				probeEM(fc, v1, sign * offset1, &rijWork);
				if (iter < agileMaxIter || semDebug) {
					offset2 = offset1 + stepSize;
					probeEM(fc, v1, sign * offset2, &rijWork);
					midOffset = (offset1 + offset2) / 2;
					recordDiff(fc, v1, rijWork, &diff, &mengOK);
				}
				pick += 2;
			}
			paramConverged = true;
		} else if (semMethod == ClassicSEM || semMethod == TianSEM) {
			if (!estHistory.size()) {
				if (verbose >= 1) mxLog("ComputeEM: no history available;"
							" Classic or Tian SEM require convergence history");
				return;
			}
			for (size_t hx=0; hx < estHistory.size(); ++hx) {
				double popt = optimum[v1];
				double offset1 = estHistory[hx][v1] - popt;
				if (paramHistLen[v1] && fabs(probeOffset[v1 * maxHistLen + paramHistLen[v1]-1] -
							     offset1) < tolerance) continue;
				if (fabs(offset1) < tolerance) continue;
				probeEM(fc, v1, offset1, &rijWork);
				if (hx == 0) continue;
				pick = hx;
				double diff;
				bool mengOK;
				recordDiff(fc, v1, rijWork, &diff, &mengOK);
				if (mengOK) {
					paramConverged = true;
					break;
				}
			}
		} else {
			for (int hx=0; hx < semMethodLen; ++hx) {
				probeEM(fc, v1, semMethodData[hx], &rijWork);
				if (hx == 0) continue;
				double diff;
				bool mengOK;
				recordDiff(fc, v1, rijWork, &diff, &mengOK);
			}
			paramConverged = true;
		}

		if (paramConverged) {
			++semConverged;
			memcpy(rij.data() + v1 * freeVars, rijWork.data() + pick*freeVars, sizeof(double) * freeVars);
			if (verbose >= 2) mxLog("ComputeEM: param %d converged in %d probes",
						(int) v1, paramHistLen[v1]);
		} else {
			if (verbose >= 2) mxLog("ComputeEM: param %d failed to converge after %d probes",
						(int) v1, paramHistLen[v1]);
			break;
		}
	}

	if (verbose >= 1) {
		if (semConverged == freeVars) {
			mxLog("ComputeEM: %d probes used to estimate Hessian", semProbeCount);
		} else {
			mxLog("ComputeEM: %d probes used for SEM but failed to converge", semProbeCount);
		}
	}
	if (semConverged < freeVars) return;

	fc->fit = bestFit;
	memcpy(fc->est, optimum.data(), sizeof(double) * freeVars);
	fc->copyParamToModel(globalState);

	if (semDebug) {
		Rf_protect(rateMatrix = Rf_allocMatrix(REALSXP, freeVars, freeVars));
		memcpy(REAL(rateMatrix), rij.data(), sizeof(double) * freeVars * freeVars);
	}

	// rij = I-rij
	for (size_t v1=0; v1 < freeVars; ++v1) {
		for (size_t v2=0; v2 < freeVars; ++v2) {
			int cell = v1 * freeVars + v2;
			double entry = rij[cell];
			if (v1 == v2) entry = 1 - entry;
			else entry = -entry;
			rij[cell] = entry;
		}
	}

	//mxLog("rij symm");
	//pda(rij.data(), freeVars, freeVars);

	setExpectationPrediction(predict);
	fc->wanted = 0;
	fc->infoMethod = infoMethod;
	fc->preInfo();
	for (size_t fx=0; fx < semFitFunction.size(); ++fx) {
		omxFitFunctionCompute(semFitFunction[fx]->fitFunction, FF_COMPUTE_INFO, fc);
	}
	fc->postInfo();
	setExpectationPrediction("nothing");

	Rf_protect(inputInfoMatrix = Rf_allocMatrix(REALSXP, freeVars, freeVars));
	double *hess = REAL(inputInfoMatrix);
	fc->copyDenseHess(hess);

	Matrix rijMat(rij.data(), freeVars, freeVars);
	Matrix hessMat(hess, freeVars, freeVars);
	omxBuffer<double> infoBuf(freeVars * freeVars);
	Matrix infoMat(infoBuf.data(), freeVars, freeVars);

	SymMatrixMultiply('L', 'U', 1, 0, hessMat, rijMat, infoMat);  // result not symmetric!

	double *ihess = fc->getDenseIHessUninitialized();
	int singular;
	if (semFixSymmetry) {
		MeanSymmetric(infoMat);
		singular = InvertSymmetricIndef(infoMat, 'U');
		memcpy(ihess, infoBuf.data(), sizeof(double) * freeVars * freeVars);
	} else {
		Matrix ihessMat(ihess, freeVars, freeVars);
		singular = MatrixSolve(infoMat, ihessMat, true);
	}
	if (singular) {
		if (verbose >= 1) mxLog("ComputeEM: SEM Hessian is singular %d", singular);
		return;
	}

	if (semForcePD) {
		double *oev = NULL;
		if (semDebug) {
			Rf_protect(origEigenvalues = Rf_allocVector(REALSXP, freeVars));
			oev = REAL(origEigenvalues);
		}
		Matrix mat(ihess, freeVars, freeVars);
		InplaceForcePosSemiDef(mat, oev, &fc->infoCondNum);
	}

	if (semDebug) {
		// ihess is always symmetric, this could be asymmetric
		Rf_protect(outputInfoMatrix = Rf_allocMatrix(REALSXP, freeVars, freeVars));
		memcpy(REAL(outputInfoMatrix), ihess, sizeof(double) * freeVars * freeVars);
	}

	fc->wanted = wanted | FF_COMPUTE_IHESSIAN;
	//pda(fc->ihess, freeVars, freeVars);
}

void ComputeEM::collectResults(FitContext *fc, LocalComputeResult *lcr, MxRList *out)
{
	super::collectResults(fc, lcr, out);

	std::vector< omxCompute* > clist(1);
	clist[0] = fit1;

	collectResultsHelper(fc, clist, lcr, out);
}

void ComputeEM::reportResults(FitContext *fc, MxRList *slots, MxRList *)
{
	size_t numFree = fc->varGroup->vars.size();
	if (!numFree) return;

	MxRList out;
	out.add("EMcycles", Rf_ScalarInteger(EMcycles));
	out.add("totalMstep", Rf_ScalarInteger(totalMstepIter));
	out.add("semProbeCount", Rf_ScalarInteger(semProbeCount));
	slots->add("output", out.asR());

	if (semDebug) {
		const int freeVars = (int) fc->varGroup->vars.size();
		MxRList dbg;

		if (probeOffset.size()) {
			SEXP Rpo;
			Rf_protect(Rpo = Rf_allocMatrix(REALSXP, maxHistLen, freeVars));
			memcpy(REAL(Rpo), probeOffset.data(), sizeof(double) * maxHistLen * freeVars);
			dbg.add("probeOffset", Rpo);
		}

		if (diffWork.size()) {
			SEXP Rdiff;
			Rf_protect(Rdiff = Rf_allocMatrix(REALSXP, maxHistLen, freeVars));
			memcpy(REAL(Rdiff), diffWork.data(), sizeof(double) * maxHistLen * freeVars);
			dbg.add("semDiff", Rdiff);
		}

		if (paramHistLen.size()) {
			SEXP Rphl;
			Rf_protect(Rphl = Rf_allocVector(INTSXP, freeVars));
			memcpy(INTEGER(Rphl), paramHistLen.data(), sizeof(int) * freeVars);
			dbg.add("paramHistLen", Rphl);
		}

		if (inputInfoMatrix) dbg.add("inputInfo", inputInfoMatrix);
		if (outputInfoMatrix) dbg.add("outputInfo", outputInfoMatrix);
		if (rateMatrix) dbg.add("rateMatrix", rateMatrix);
		if (origEigenvalues) dbg.add("origEigenvalues", origEigenvalues);

		slots->add("debug", dbg.asR());
	}
}

ComputeEM::~ComputeEM()
{
	for (size_t rx=0; rx < ramsay.size(); ++rx) {
		delete ramsay[rx];
	}
	ramsay.clear();

	delete fit1;

	for (size_t hx=0; hx < estHistory.size(); ++hx) {
		delete [] estHistory[hx];
	}
	estHistory.clear();
}

enum ComputeInfoMethod omxCompute::stringToInfoMethod(const char *iMethod)
{
	enum ComputeInfoMethod infoMethod = INFO_METHOD_DEFAULT; // to avoid gcc warning
	if (strcmp(iMethod, "sandwich")==0) {
		infoMethod = INFO_METHOD_SANDWICH;
	} else if (strcmp(iMethod, "meat")==0) {
		infoMethod = INFO_METHOD_MEAT;
	} else if (strcmp(iMethod, "bread")==0) {
		infoMethod = INFO_METHOD_BREAD;
	} else if (strcmp(iMethod, "hessian")==0) {
		infoMethod = INFO_METHOD_HESSIAN;
	} else {
		Rf_error("Unknown information matrix estimation method '%s'", iMethod);
	}
	return infoMethod;
}

void omxComputeOnce::initFromFrontend(SEXP rObj)
{
	super::initFromFrontend(rObj);

	SEXP slotValue;
	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("from")));
	for (int wx=0; wx < Rf_length(slotValue); ++wx) {
		int objNum = INTEGER(slotValue)[wx];
		if (objNum >= 0) {
			omxMatrix *algebra = globalState->algebraList[objNum];
			if (algebra->fitFunction) {
				setFreeVarGroup(algebra->fitFunction, varGroup);
				omxCompleteFitFunction(algebra);
			}
			algebras.push_back(algebra);
		} else {
			omxExpectation *expectation = globalState->expectationList[~objNum];
			setFreeVarGroup(expectation, varGroup);
			omxCompleteExpectation(expectation);
			expectations.push_back(expectation);
		}
	}

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("verbose")));
	verbose = Rf_asInteger(slotValue);

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("what")));
	int whatLen = Rf_length(slotValue);
	if (algebras.size()) {
		for (int wx=0; wx < whatLen; ++wx) {
			SEXP elem;
			Rf_protect(elem = STRING_ELT(slotValue, wx));
			const char *what = CHAR(elem);
			if      (strcmp(what, "maxAbsChange")==0) mac = true;
			else if (strcmp(what, "set-starting-values")==0) starting = true;
			else if (strcmp(what, "fit")         ==0) fit = true;
			else if (strcmp(what, "gradient")    ==0) gradient = true;
			else if (strcmp(what, "hessian")     ==0) hessian = true;
			else if (strcmp(what, "information") ==0) infoMat = true;
			else if (strcmp(what, "ihessian")    ==0) ihessian = true;
			else omxRaiseErrorf("mxComputeOnce: don't know how to compute %s", what);
		}

		if (hessian && infoMat) Rf_error("Cannot compute the Hessian and Fisher Information matrix simultaneously");
	} else {
		for (int wx=0; wx < whatLen; ++wx) {
			SEXP elem;
			Rf_protect(elem = STRING_ELT(slotValue, wx));
			predict.push_back(CHAR(elem));
		}
	}

	Rf_protect(slotValue = R_do_slot(rObj, Rf_install(".is.bestfit")));
	isBestFit = Rf_asLogical(slotValue);

	bool howConflict = false;
	Rf_protect(slotValue = R_do_slot(rObj, Rf_install("how")));
	if (Rf_length(slotValue) > 1) {
		omxRaiseErrorf("mxComputeOnce: more than one method specified");
	} else if (Rf_length(slotValue) == 1) {
		SEXP elem;
		Rf_protect(elem = STRING_ELT(slotValue, 0));
		if (algebras.size()) {
			const char *iMethod = CHAR(elem);
			if (infoMat) {
				infoMethod = stringToInfoMethod(iMethod);
				if (infoMethod == INFO_METHOD_MEAT && gradient && whatLen == 2) {
					//OK
				} else if (whatLen > 1) {
					howConflict = true;
				}
			} else {
				omxRaiseErrorf("mxComputeOnce: unknown method %s requested", iMethod);
			}
		} else {
			how = CHAR(elem);
			if (whatLen > 1) howConflict = true;
		}
	}
	if (howConflict) {
		omxRaiseErrorf("mxComputeOnce: when how is specified, you can only compute one thing at a time");
	}

	if (algebras.size() == 1 && algebras[0]->fitFunction) {
		omxFitFunction *ff = algebras[0]->fitFunction;
		if (gradient && !ff->gradientAvailable) {
			Rf_error("Gradient requested but not available");
		}
		if ((hessian || ihessian || hgprod) && !ff->hessianAvailable) {
			// add a separate flag for hgprod TODO
			Rf_error("Hessian requested but not available");
		}
		// add check for information TODO
	}
}

void omxComputeOnce::computeImpl(FitContext *fc)
{
	if (algebras.size()) {
		int want = 0;
		if (starting) {
			want |= FF_COMPUTE_STARTING;
		}
		if (mac) {
			want |= FF_COMPUTE_MAXABSCHANGE;
			fc->mac = 0;
		}
		if (fit) {
			want |= FF_COMPUTE_FIT;
			if (isBestFit) want |= FF_COMPUTE_BESTFIT;
			fc->fit = 0;
		}
		if (gradient) {
			want |= FF_COMPUTE_GRADIENT;
			fc->grad = Eigen::VectorXd::Zero(fc->numParam);
		}
		if (hessian) {
			want |= FF_COMPUTE_HESSIAN;
			fc->clearHessian();
		}
		if (infoMat) {
			want |= FF_COMPUTE_INFO;
			fc->infoMethod = infoMethod;
			fc->grad = Eigen::VectorXd::Zero(fc->numParam);
			fc->clearHessian();
			fc->preInfo();
		}
		if (ihessian) {
			want |= FF_COMPUTE_IHESSIAN;
			fc->clearHessian();
		}
		if (!want) return;

		for (size_t wx=0; wx < algebras.size(); ++wx) {
			omxMatrix *algebra = algebras[wx];
			if (algebra->fitFunction) {
				omxFitFunctionCompute(algebra->fitFunction, FF_COMPUTE_PREOPTIMIZE, fc);
				ComputeFit(algebra, want, fc);
				if (infoMat) {
					fc->postInfo();
				}
			} else {
				omxRecompute(algebra, want, fc);
			}
		}
	} else if (expectations.size()) {
		if (predict.size() > 1) Rf_error("Not implemented");
		for (size_t wx=0; wx < expectations.size(); ++wx) {
			omxExpectation *expectation = expectations[wx];
			omxExpectationCompute(expectation, predict[0], how);
		}
	}
}

void omxComputeOnce::reportResults(FitContext *fc, MxRList *slots, MxRList *out)
{
	if (algebras.size()==0 || algebras[0]->fitFunction == NULL) return;

	omxMatrix *algebra = algebras[0];
	omxPopulateFitFunction(algebra, out);
}

void ComputeStandardError::reportResults(FitContext *fc, MxRList *slots, MxRList *)
{
	fc->allocStderrs();  // at least report NAs

	const size_t numParams = fc->numParam;

	Eigen::VectorXd ihessDiag(fc->ihessDiag());

	const double scale = fabs(Global->llScale);

	// This function calculates the standard errors from the Hessian matrix
	// sqrt(scale * diag(solve(hessian)))

	for(size_t i = 0; i < numParams; i++) {
		double got = ihessDiag[i];
		if (got <= 0) continue;
		fc->stderrs[i] = sqrt(scale * got);
	}
}

/*
Date: Fri, 3 Jan 2014 14:02:34 -0600
From: Michael Hunter <mhunter@ou.edu>

Determining positive definiteness of matrix is typically done by
trying the Cholesky decomposition.  If it fails, the matrix is not
positive definite; if it passes, the matrix is.  The benefit of the
Cholesky is that it's much faster and easier to compute than a set of
eigenvalues.

The BLAS/LAPACK routine DTRCO quickly computes a good approximation to the
reciprocal condition number of a triangular matrix.  Hand it the Cholesky
(a triangular matrix) the rest is history.  I don't think we need the
exact condition number as long as it's just for finding very
ill-conditioned problems.  For the solution to a linear system of
equations, if you really care about the difference in precision between
1e-14 and 1e-11, then the exact condition number is needed.  Otherwise, the
approximation is faster and equally useful.
*/
void ComputeHessianQuality::reportResults(FitContext *fc, MxRList *slots, MxRList *)
{
	// See Luenberger & Ye (2008) Second Order Test (p. 190) and Condition Number (p. 239)

	// memcmp is required here because NaN != NaN always
	if (fc->infoDefinite != NA_LOGICAL ||
	    memcmp(&fc->infoCondNum, &NA_REAL, sizeof(double)) != 0) return; // already set elsewhere

	int numParams = int(fc->varGroup->vars.size());

	double *mat = fc->getDenseHessianish();
	if (!mat) {
		fc->infoDefinite = false;
		return;
	}
	omxBuffer<double> hessWork(numParams * numParams);
	memcpy(hessWork.data(), mat, sizeof(double) * numParams * numParams);

	char jobz = 'N';
	char range = 'A';
	char uplo = 'U';
	double abstol = 0;
	int m;
	omxBuffer<double> w(numParams);
	double optWork;
	int lwork = -1;
	omxBuffer<int> iwork(5 * numParams);
	int info;
	double realIgn = 0;
	int intIgn = 0;
	F77_CALL(dsyevx)(&jobz, &range, &uplo, &numParams, hessWork.data(),
			 &numParams, &realIgn, &realIgn, &intIgn, &intIgn, &abstol, &m, w.data(),
			 NULL, &numParams, &optWork, &lwork, iwork.data(), NULL, &info);

	lwork = optWork;
	omxBuffer<double> work(lwork);
	F77_CALL(dsyevx)(&jobz, &range, &uplo, &numParams, hessWork.data(),
			 &numParams, &realIgn, &realIgn, &intIgn, &intIgn, &abstol, &m, w.data(),
			 NULL, &numParams, work.data(), &lwork, iwork.data(), NULL, &info);
	if (info < 0) {
		Rf_error("dsyevx %d", info);
	} else if (info) {
		return;
	}

	bool definite = true;
	bool neg = w[0] < 0;
	for (int px=1; px < numParams; ++px) {
		if ((w[px] < 0) ^ neg) {
			definite = false;
			break;
		}
	}

	fc->infoDefinite = definite;

	if (definite) {
		double ev[2] = { fabs(w[0]), fabs(w[numParams-1]) };
		if (ev[0] < ev[1]) std::swap(ev[0], ev[1]);
		double got = ev[0] / ev[1];
		if (std::isfinite(got)) fc->infoCondNum = got;
	}
}

void ComputeReportDeriv::reportResults(FitContext *fc, MxRList *, MxRList *result)
{
	size_t numFree = fc->numParam;

	if (fc->wanted & FF_COMPUTE_GRADIENT) {
		if (!fc->grad.data()) {
			// oh well
		} else {
			SEXP Rgradient;
			Rf_protect(Rgradient = Rf_allocVector(REALSXP, numFree));
			memcpy(REAL(Rgradient), fc->grad.data(), sizeof(double) * numFree);
			result->add("gradient", Rgradient);
		}
	}
	if (fc->wanted & FF_COMPUTE_HESSIAN) {
		SEXP Rhessian;
		Rf_protect(Rhessian = Rf_allocMatrix(REALSXP, numFree, numFree));
		fc->copyDenseHess(REAL(Rhessian));
		result->add("hessian", Rhessian);
	}
	if (fc->wanted & FF_COMPUTE_IHESSIAN) {
		SEXP Rihessian;
		Rf_protect(Rihessian = Rf_allocMatrix(REALSXP, numFree, numFree));
		fc->copyDenseIHess(REAL(Rihessian));
		result->add("ihessian", Rihessian);
	}
}
