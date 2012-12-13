#
#   Copyright 2007-2012 The OpenMx Project
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
# 
#        http://www.apache.org/licenses/LICENSE-2.0
# 
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.


setClass(Class = "MxFitFunctionAlgebra",
	representation = representation(
		algebra = "MxCharOrNumber",
		numObs = "numeric",
		numStats = "numeric"),
	contains = "MxBaseFitFunction")

setMethod("initialize", "MxFitFunctionAlgebra",
	function(.Object, algebra, numObs, numStats, name = 'fitfunction') {
		.Object@name <- name
		.Object@algebra <- algebra
		.Object@numObs <- numObs
		.Object@numStats <- numStats
		return(.Object)
	}
)

setMethod("genericFitDependencies", signature("MxFitFunctionAlgebra"),
	function(.Object, flatModel, dependencies) {
	dependencies <- callNextMethod()
	dependencies <- imxAddDependency(.Object@algebra, .Object@name, dependencies)
	return(dependencies)
})

setMethod("genericFitFunConvert", signature("MxFitFunctionAlgebra"), 
	function(.Object, flatModel, model, labelsData, defVars, dependencies) {
		name <- .Object@name
		algebra <- .Object@algebra
		if (is.na(algebra)) {
			modelname <- imxReverseIdentifier(model, .Object@name)[[1]]
			msg <- paste("The algebra name cannot be NA",
			"for the algebra fit function of model", omxQuotes(modelname))
			stop(msg, call. = FALSE)
		}
		algebraIndex <- imxLocateIndex(flatModel, algebra, name)
		modelname <- imxReverseIdentifier(model, .Object@name)[[1]]
		expectName <- paste(modelname, "expectation", sep=".")
		if (expectName %in% names(flatModel@expectations)) {
			expectIndex <- imxLocateIndex(flatModel, expectName, name)
		} else {
			expectIndex <- as.integer(NA)
		}
		.Object@algebra <- algebraIndex
		.Object@expectation <- expectIndex
		return(.Object)
})

setMethod("genericFitFunNamespace", signature("MxFitFunctionAlgebra"), 
	function(.Object, modelname, namespace) {
		.Object@name <- imxIdentifier(modelname, .Object@name)
		.Object@algebra <- imxConvertIdentifier(.Object@algebra, modelname, namespace)
		return(.Object)
})

setMethod("genericFitRename", signature("MxFitFunctionAlgebra"),
	function(.Object, oldname, newname) {
		.Object@algebra <- renameReference(.Object@algebra, oldname, newname)
		return(.Object)
})

mxFitFunctionAlgebra <- function(algebra, numObs = NA, numStats = NA) {
	if (missing(algebra) || typeof(algebra) != "character") {
		stop("Algebra argument is not a string (the name of the algebra)")
	}
	if (single.na(numObs)) {
		numObs <- as.numeric(NA)
	}
	if (single.na(numStats)) {
		numStats <- as.numeric(NA)
	}
	return(new("MxFitFunctionAlgebra", algebra, numObs, numStats))
}

displayMxFitFunctionAlgebra <- function(fitfunction) {
	cat("MxFitFunctionAlgebra", omxQuotes(fitfunction@name), '\n')
	cat("@algebra: ", omxQuotes(fitfunction@algebra), '\n')	
	cat("@numObs: ", fitfunction@numObs, '\n')
	cat("@numStats: ", fitfunction@numStats, '\n')
	if (length(fitfunction@result) == 0) {
		cat("@result: (not yet computed) ")
	} else {
		cat("@result:\n")
	}
	print(fitfunction@result)
	invisible(fitfunction)
}

setMethod("print", "MxFitFunctionAlgebra", function(x,...) { 
	displayMxFitFunctionAlgebra(x) 
})

setMethod("show", "MxFitFunctionAlgebra", function(object) { 
	displayMxFitFunctionAlgebra(object) 
})