# Simulate univariate data

	set.seed(100)
	x <- rnorm (1000, 0, 1)
	univData <- as.matrix(x)
	dimnames(univData) <- list(NULL, "X")
	summary(univData)
	mean(univData)
	var(univData)

# Simulate bivariate data

    require(MASS)
    set.seed(200)
    rs=.5
    xy <- mvrnorm (1000, c(0,0), matrix(c(1,rs,rs,1),2,2))
    bivData <- xy
    dimnames(bivData) <- list(NULL, c('X','Y'))
    summary(bivData)
    colMeans(bivData)
    cov(bivData)
       #hist(univData)
    
# Create ordinal and binary data from continuous data
    univDataOrd <- data.frame(X=cut(univData[,1], breaks=5, ordered_result=T, labels=c(0,1,2,3,4)) )
       table(univDataOrd)
    univDataBin <- data.frame(X=ifelse(univData[,1] >.5,1,0))
       table(univDataBin)
       
    bivDataOrd <- data.frame(bivData)
    for (i in 1:2) { bivDataOrd[,i] <- cut(bivData[,i], breaks=5, ordered_result=T, labels=c(0,1,2,3,4)) }
       table(bivDataOrd[,1],bivDataOrd[,2])
    bivDataBin <- data.frame(bivData)
    for (i in 1:2) { bivDataBin[,i] <- ifelse(bivData[,i] >.5,1,0) }
       table(bivDataBin[,1],bivDataBin[,2])

# Create data objects for alternative data types

    require(OpenMx)
    obsRawData <- mxData( observed=univData, type="raw" )
       #obsRawData   
    obsBivData <- mxData( observed=bivData, type="raw" )
      
       univDataOrdF <- mxFactor( x=univDataOrd, levels=c(0:4) )
       univDataBinF <- mxFactor( x=univDataBin, levels=c(0,1) )
       bivDataOrdF <- mxFactor( x=bivDataOrd, levels=c(0:4) )
       bivDataBinF <- mxFactor( x=bivDataBin, levels=c(0,1) )
    
    obsRawDataOrd <- mxData( observed=univDataOrdF, type="raw" )
    obsRawDataBin <- mxData( observed=univDataBinF, type="raw" )
    obsBivDataOrd <- mxData( observed=bivDataOrdF, type="raw" )
    obsBivDataBin <- mxData( observed=bivDataBinF, type="raw" )
    
    univDataCov <- var(univData)
    obsCovData <- mxData( observed=univDataCov, type="cov", numObs=1000 )
    obsCovData <- mxData( observed=var(univData), type="cov", numObs=1000 )
       obsCovData
 
# Fit saturated model to covariance matrices using the path method

    selVars   <- c("X")
    manifestVars=selVars
    expVariance <- mxPath( from=c("X"), arrows=2, free=TRUE, values=1, lbound=.01, labels="vX" )
       expVariance
    type="RAM"
    univSatModel1 <- mxModel("univSat1", manifestVars=selVars, obsCovData, expVariance, type="RAM" )
       univSatModel1
       univSatModel1@matrices$S
    univSatFit1 <- mxRun(univSatModel1)
       univSatFit1@matrices$S@values
       univSatFit1[['S']]@values
    EC1 <- mxEval(S, univSatFit1)
       EC1   
    SL1 <- univSatFit1@output$other$Saturated
    LL1 <- mxEval(objective, univSatFit1)
    Chi1 <- LL1-SL1
       SL1
       LL1
       Chi1
    summary(univSatFit1)
    univSatSumm1 <- summary(univSatFit1)
       univSatSumm1
 	
# Fit saturated model to covariance matrices and means using the path method

    expMean <- mxPath(from="one", to="X", arrows=1, free=T, values=0, labels="mX")
       expMean
    obsCovMeanData <- mxData( observed=var(univData),  type="cov", numObs=1000, means=colMeans(univData) )
    univSatModel1M <- mxModel(univSatModel1, name="univSat1M", expMean, obsCovMeanData )
       univSatModel1M
    univSatFit1M <- mxRun(univSatModel1M)
    EM1m <- mxEval(M, univSatFit1M) 
    univSatSumm1M <- summary(univSatFit1M)
       univSatSumm1M
    
# Fit saturated model to raw data using the path method

    univSatModel2 <- mxModel(univSatModel1M, obsRawData )
    univSatFit2 <- mxRun(univSatModel2)
    univSatSumm2 <- summary(univSatFit2)
    EM2 <- mxEval(M, univSatFit2) 
    EC2 <- mxEval(S, univSatFit2)
    LL2 <- mxEval(objective, univSatFit2)
           EM2
       EC2
       LL2
       univSatSumm2
 	
# Fit saturated model to covariance matrices using the matrix method

    expCovMat <- mxMatrix( type="Symm", nrow=1, ncol=1, free=TRUE, values=1, name="expCov" )
       expCovMat
    MLobjective <- mxMLObjective( covariance="expCov", dimnames=selVars )
       MLobjective
    univSatModel3 <- mxModel("univSat3", obsCovData, expCovMat, MLobjective)
    univSatFit3 <- mxRun(univSatModel3)
    univSatSumm3 <- summary(univSatFit3)
       univSatSumm3
    
# Fit saturated model to covariance matrices and means using the matrix method    

    expMeanMat <- mxMatrix( type="Full", nrow=1, ncol=1, free=TRUE, values=0, name="expMean" )
       expMeanMat
    MLobjectiveM <- mxMLObjective( covariance="expCov", means="expMean", dimnames=selVars )
       MLobjectiveM
    univSatModel3M <- mxModel(univSatModel3, name="univSat3M", obsCovMeanData, expMeanMat, MLobjectiveM)
    univSatFit3M <- mxRun(univSatModel3M)
    univSatSumm3M <- summary(univSatFit3M)
 
# Fit saturated model to raw data using the matrix method 

    FIMLobjective <- mxFIMLObjective( covariance="expCov", means="expMean", dimnames=selVars )
       FIMLobjective
    univSatModel4 <- mxModel("univSat4", obsRawData, expCovMat, expMeanMat, FIMLobjective )
    univSatFit4 <- mxRun(univSatModel4)
    univSatSumm4 <- summary(univSatFit4)
       univSatSumm4

# Fit saturated model to raw binary data using the matrix method 

    expCovMatBin <- mxMatrix( type="Stand", nrow=1, ncol=1, free=TRUE, values=.5, name="expCov" )
    expMeanMatBin <- mxMatrix( type="Zero", nrow=1, ncol=1, name="expMean" )
    expThreMatBin <- mxMatrix( type="Full", nrow=1, ncol=1, free=TRUE, values=0, name="expThre" )
    	expThreMatBin
    FIMLobjectiveBin <- mxFIMLObjective( covariance="expCov", means="expMean", threshold="expThre", dimnames=selVars )
    univSatModel5 <- mxModel("univSat5", obsRawDataBin, expCovMatBin, expMeanMatBin, expThreMatBin, FIMLobjectiveBin )
    univSatFit5 <- mxRun(univSatModel5)
    univSatSumm5 <- summary(univSatFit5)
       univSatSumm5
    
# Fit saturated model to raw ordinal data using the matrix method 

    nv <- 1
    nth <- 4
    expCovMatOrd <- mxMatrix( type="Stand", nrow=nv, ncol=nv, free=TRUE, values=.5, name="expCov" )
    expMeanMatOrd <- mxMatrix( type="Zero", nrow=1, ncol=nv, name="expMean" )
    expThreMatOrd <- mxMatrix( type="Full", nrow=nth, ncol=nv, free=TRUE, values=c(-1.5,-.5,.5,1.5), name="expThre" )
       expThreMatOrd
    FIMLobjectiveOrd <- mxFIMLObjective( covariance="expCov", means="expMean", threshold="expThre", dimnames=selVars )
    univSatModel6 <- mxModel("univSat6", obsRawDataOrd, expCovMatOrd, expMeanMatOrd, expThreMatOrd, FIMLobjectiveOrd )
    univSatFit6 <- mxRun(univSatModel6)
    univSatSumm6 <- summary(univSatFit6)
 
# Fit saturated model to raw ordinal data using the matrix method with deviations/increments

    lth <- -1.5; ith <- 1
    thValues <- matrix(rep(c(lth,(rep(ith,nth-1)))),nrow=nth,ncol=nv)
    thLBound <- matrix(rep(c(-3,(rep(0.001,nth-1))),nv),nrow=nth,ncol=nv)
    Inc <- mxMatrix( type="Lower", nrow=nth, ncol=nth, free=FALSE, values=1, name="Inc" )
    Thre <- mxMatrix( type="Full", nrow=nth, ncol=1, free=TRUE, values=thValues, lbound=thLBound, name="Thre" )
    expThreMatOrd <- mxAlgebra( expression= Inc %*% Thre, name="expThre" )
    FIMLobjectiveOrd <- mxFIMLObjective( covariance="expCov", means="expMean", threshold="expThre", dimnames=selVars )
    univSatModel6I <- mxModel("univSat6", obsRawDataOrd, expCovMatOrd, expMeanMatOrd, Inc, Thre, expThreMatOrd, FIMLobjectiveOrd )
    univSatFit6I <- mxRun(univSatModel6I)
    univSatSumm6I <- summary(univSatFit6I)



# Fit bivariate saturated model to raw data using the path method 

    nv <- 2
    selVars <- c('X','Y')
    obsBivData <- mxData( observed=bivData, type="raw" )
    expVars <- mxPath( from=c("X", "Y"), arrows=2, free=TRUE, values=1, lbound=.01, labels=c("varX","varY") )
    expCovs <- mxPath( from="X", to="Y", arrows=2, free=TRUE, values=.2, lbound=.01, labels="covXY" )
    expMeans <- mxPath( from="one", to=c("X", "Y"), arrows=1, free=TRUE, values=.01, labels=c("meanX","meanY") )
    bivSatModel1 <- mxModel("bivSat1", manifestVars=selVars, obsBivData, expVars, expCovs, expMeans, type="RAM" )
    bivSatFit1 <- mxRun(bivSatModel1)
    bivSatSumm1 <- summary(bivSatFit1)
       bivSatSumm1

# Fit bivariate saturated model to raw data using the matrix method in the piecemeal style

    expCovM <- mxMatrix( type="Symm", nrow=nv, ncol=nv, free=TRUE, values=c(1,0.5,1), labels=c('V1','Cov','V2'), name="expCov" )
       expCovM
    expMeanM <- mxMatrix( type="Full", nrow=1, ncol=nv, free=TRUE, values=c(0,0), labels=c('M1','M2'), name="expMean" )
       expMeanM
    lowerTriM <- mxMatrix( type="Lower", nrow=nv, ncol=nv, free=TRUE, values=.5, name="Chol" )
    expCovMA <- mxAlgebra( expression=Chol %*% t(Chol), name="expCov" ) 
       expCovMA
    FIMLobjective <- mxFIMLObjective( covariance="expCov", means="expMean", dimnames=selVars )
    bivSatModel2 <- mxModel("bivSat2", obsBivData, lowerTriM, expCovMA, expMeanM, FIMLobjective )
    bivSatFit2 <- mxRun(bivSatModel2)
    bivSatSumm2 <- summary(bivSatFit2)
       bivSatSumm2
    
# Fit bivariate saturated model to raw data using the matrix method in the recursive style

    bivSatModel3 <- mxModel("bivSat3", mxData( observed=bivData, type="raw" ) )
    bivSatModel3 <- mxModel(bivSatModel3, mxMatrix( type="Lower", nrow=nv, ncol=nv, free=TRUE, values=.5, name="Chol" ) )
    bivSatModel3 <- mxModel(bivSatModel3, mxAlgebra( expression=Chol %*% t(Chol), name="expCov" ) )
    bivSatModel3 <- mxModel(bivSatModel3, mxMatrix( type="Full", nrow=1, ncol=nv, free=TRUE, values=c(0,0), labels=c('M1','M2'), name="expMean" ) )
    bivSatModel3 <- mxModel(bivSatModel3, mxFIMLObjective( covariance="expCov", means="expMean", dimnames=selVars ) )
    bivSatFit3 <- mxRun(bivSatModel3)
    bivSatSumm3 <- summary(bivSatFit3)
 
# Fit bivariate saturated model to raw data using the matrix method in the classic style

    bivSatModel4 <- mxModel("bivSat4", 
                                 mxData( observed=bivData, type="raw" ), 
                                 mxMatrix( type="Lower", nrow=nv, ncol=nv, free=TRUE, values=.5, name="Chol" ),
                                 mxAlgebra( expression=Chol %*% t(Chol), name="expCov" ),
                                 mxMatrix( type="Full", nrow=1, ncol=nv, free=TRUE, values=c(0,0), name="expMean" ),
                                 mxFIMLObjective( covariance="expCov", means="expMean", dimnames=selVars ) )
    bivSatFit4 <- mxRun(bivSatModel4)
    bivSatSumm4 <- summary(bivSatFit4)
 
# Fit bivariate saturated model to raw binary data using the matrix method 

    expCovMatBin <- mxMatrix( type="Stand", nrow=nv, ncol=nv, free=TRUE, values=.5, name="expCov" )
    expMeanMatBin <- mxMatrix( type="Zero", nrow=1, ncol=nv, name="expMean" )
    expThreMatBin <- mxMatrix( type="Full", nrow=1, ncol=nv, free=TRUE, values=0, name="expThre" )
    FIMLobjectiveBin <- mxFIMLObjective( covariance="expCov", means="expMean", threshold="expThre", dimnames=selVars )
    bivSatModel5 <- mxModel("bivSat5", obsBivDataBin, expCovMatBin, expMeanMatBin, expThreMatBin, FIMLobjectiveBin )
    bivSatFit5 <- mxRun(bivSatModel5)
    bivSatSumm5 <- summary(bivSatFit5)
  
# Fit bivariate saturated model to raw ordinal data using the matrix method 

    expCovMatOrd <- mxMatrix( type="Stand", nrow=nv, ncol=nv, free=TRUE, values=.5, name="expCov" )
    expMeanMatOrd <- mxMatrix( type="Zero", nrow=1, ncol=nv, name="expMean" )
    expThreMatOrd <- mxMatrix( type="Full", nrow=nth, ncol=nv, free=TRUE, values=c(-1.5,-.5,.5,1.5), name="expThre" )
    FIMLobjectiveOrd <- mxFIMLObjective( covariance="expCov", means="expMean", threshold="expThre", dimnames=selVars )
    bivSatModel6 <- mxModel("bivSat6", obsBivDataOrd, expCovMatOrd, expMeanMatOrd, expThreMatOrd, FIMLobjectiveOrd )
    bivSatFit6 <- mxRun(bivSatModel6)
    bivSatSumm6 <- summary(bivSatFit6) 
 