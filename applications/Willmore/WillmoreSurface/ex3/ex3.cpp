/* This example details the full implementation of the p-Willmore flow
   algorithm, which involves three nonlinear systems.

   System0 AssembleInit computes the initial curvatures given mesh positions.
   System AssemblePWillmore solves the flow equations.
   System2 AssembleConformalMinimization "reparametrizes" the surface to
   correct the mesh. */

#include "FemusInit.hpp"
#include "MultiLevelSolution.hpp"
#include "MultiLevelProblem.hpp"
#include "NumericVector.hpp"
#include "VTKWriter.hpp"
#include "GMVWriter.hpp"
#include "NonLinearImplicitSystem.hpp"
#include "TransientSystem.hpp"
#include "adept.h"
#include <cstdlib>
#include "petsc.h"
#include "petscmat.h"
#include "PetscMatrix.hpp"

/* Vector option for P (to handle polynomials).
 ap is the coefficient in front of the power of H. */
const unsigned P[3] = {0, 3, 4};
const double ap[3] = {1, 0., 0.};

using namespace femus;

bool O2conformal = true;
bool firstTime = true;
double surface0 = 0.;
double volume0 = 0.;
bool volumeConstraint = false;
bool areaConstraint = false;

unsigned conformalTriangleType = 2;
const double eps = 1.0e-5;

const double normalSign = -1.;

#include "../include/supportFunctions.hpp"
#include "../include/assembleConformalMinimization.hpp"
#include "../include/assembleInit.hpp"

// Penalty parameter for conformal minimization (eps).
// Trick for system0 (delta). ????
// Trick for system2 (timederiv).
const double delta = 0.00000;
const double timederiv = 0.;

// Declaration of systems.
void AssembleMCF (MultiLevelProblem&);


double dt0 = 6e-4;

// Function to control the time stepping.
double GetTimeStep (const double t) {
  // if(time==0) return 5.0e-7;
  //return 0.0001;
  //double dt0 = .00002;
  // double dt0 = 0.67;
  //double s = 1.;
  //double n = 0.3;
  //return dt0 * pow (1. + t / pow (dt0, s), n);
  return dt0;
}

// IBVs.  No boundary, and IVs set to sphere (just need something).
bool SetBoundaryCondition (const std::vector < double >& x,
                           const char SolName[], double& value, const int facename,
                           const double time) {
  bool dirichlet = false;
  value = 0.;
  return dirichlet;
}
double InitalValueY1 (const std::vector < double >& x) {
  return -2. * x[0];
}
double InitalValueY2 (const std::vector < double >& x) {
  return -2. * x[1];
}
double InitalValueY3 (const std::vector < double >& x) {
  return -2. * x[2];
}


// Main program starts here.
int main (int argc, char** args) {

  // init Petsc-MPI communicator
  FemusInit mpinit (argc, args, MPI_COMM_WORLD);

  // define multilevel mesh
  unsigned maxNumberOfMeshes;
  MultiLevelMesh mlMsh;

  // Read coarse level mesh and generate finer level meshes.
  double scalingFactor = 1.; // 1 over the characteristic length

  //mlMsh.ReadCoarseMesh("../input/torus.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/sphere.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/ellipsoidRef3.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/ellipsoidV1.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/genusOne.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/knot.neu", "seventh", scalingFactor);
  mlMsh.ReadCoarseMesh ("../input/c.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/horseShoe3.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/tiltedTorus.neu", "seventh", scalingFactor);
  scalingFactor = 1.;
  //mlMsh.ReadCoarseMesh ("../input/dog.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/virus3.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/ellipsoidSphere.neu", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh("../input/CliffordTorus.neu", "seventh", scalingFactor);

  //mlMsh.ReadCoarseMesh ("../input/spot.med", "seventh", scalingFactor);
  //mlMsh.ReadCoarseMesh ("../input/moai.med", "seventh", scalingFactor);


  // Set number of mesh levels.
  unsigned numberOfUniformLevels = 3;
  unsigned numberOfSelectiveLevels = 0;
  mlMsh.RefineMesh (numberOfUniformLevels , numberOfUniformLevels + numberOfSelectiveLevels, NULL);

  // Erase all the coarse mesh levels.
  mlMsh.EraseCoarseLevels (numberOfUniformLevels - 1);

  // print mesh info
  mlMsh.PrintInfo();

  // Define the multilevel solution and attach the mlMsh object to it.
  MultiLevelSolution mlSol (&mlMsh);

  // Add variables X,Y,W to mlSol.
  mlSol.AddSolution ("Dx1", LAGRANGE, FIRST, 2);
  mlSol.AddSolution ("Dx2", LAGRANGE, FIRST, 2);
  mlSol.AddSolution ("Dx3", LAGRANGE, FIRST, 2);
  mlSol.AddSolution ("Y1", LAGRANGE, FIRST, 2);
  mlSol.AddSolution ("Y2", LAGRANGE, FIRST, 2);
  mlSol.AddSolution ("Y3", LAGRANGE, FIRST, 2);

  // Add variable "Lambda" based on constraint choice.
  if (volumeConstraint || areaConstraint) {
    mlSol.AddSolution ("Lambda", DISCONTINUOUS_POLYNOMIAL, ZERO, 0);
  }

  mlSol.AddSolution ("ENVN", LAGRANGE, FIRST, 0, false);

  // Add variables "nDx" and "Lambda1" for the conformal system.
  mlSol.AddSolution ("nDx1", LAGRANGE, FIRST, 0);
  mlSol.AddSolution ("nDx2", LAGRANGE, FIRST, 0);
  mlSol.AddSolution ("nDx3", LAGRANGE, FIRST, 0);
  mlSol.AddSolution ("Lambda1", DISCONTINUOUS_POLYNOMIAL, ZERO, 0);

  // Initialize the variables and attach boundary conditions.
  mlSol.Initialize ("All");

  mlSol.AttachSetBoundaryConditionFunction (SetBoundaryCondition);
  mlSol.GenerateBdc ("All");

  GetElementNearVertexNumber (mlSol);

  MultiLevelProblem mlProb (&mlSol);

  LinearImplicitSystem& systemY = mlProb.add_system < LinearImplicitSystem > ("InitY");

  // Add solutions Y to systemY.
  systemY.AddSolutionToSystemPDE ("Y1");
  systemY.AddSolutionToSystemPDE ("Y2");
  systemY.AddSolutionToSystemPDE ("Y3");

  // Add the assembling function to system0 and initialize.
  systemY.SetAssembleFunction (AssembleSystemY);
  systemY.init();

  // Add system P-Willmore in mlProb as a time-dependent system.
  TransientNonlinearImplicitSystem& system = mlProb.add_system < TransientNonlinearImplicitSystem > ("MCF");

  // Add solutions X, Y, W to P-Willmore system.
  system.AddSolutionToSystemPDE ("Dx1");
  system.AddSolutionToSystemPDE ("Dx2");
  system.AddSolutionToSystemPDE ("Dx3");
  system.AddSolutionToSystemPDE ("Y1");
  system.AddSolutionToSystemPDE ("Y2");
  system.AddSolutionToSystemPDE ("Y3");

  // Add solution Lambda to system based on constraint choice.
  if (volumeConstraint || areaConstraint) {
    system.AddSolutionToSystemPDE ("Lambda");
    system.SetNumberOfGlobalVariables (volumeConstraint + areaConstraint);
  }

  // Parameters for convergence and # of iterations for Willmore.
  system.SetMaxNumberOfNonLinearIterations (2);
  system.SetNonLinearConvergenceTolerance (1.e-10);

  // Attach the assembling function to P-Willmore system.
  system.SetAssembleFunction (AssembleMCF);

  // Attach time step function to P-Willmore sysyem.
  system.AttachGetTimeIntervalFunction (GetTimeStep);

  // Initialize the P-Willmore system.
  system.init();
  system.SetMgType (V_CYCLE);

  // Add system2 Conformal Minimization in mlProb.
  NonLinearImplicitSystem& system2 = mlProb.add_system < NonLinearImplicitSystem > ("nProj");

  // Add solutions newDX, Lambda1 to system2.
  system2.AddSolutionToSystemPDE ("nDx1");
  system2.AddSolutionToSystemPDE ("nDx2");
  system2.AddSolutionToSystemPDE ("nDx3");
  system2.AddSolutionToSystemPDE ("Lambda1");

  // Parameters for convergence and # of iterations.
  system2.SetMaxNumberOfNonLinearIterations (2);
  system2.SetNonLinearConvergenceTolerance (1.e-10);

  // Attach the assembling function to system2 and initialize.
  system2.SetAssembleFunction (AssembleConformalMinimization);
  system2.init();

  mlSol.SetWriter (VTK);
  std::vector<std::string> mov_vars;
  mov_vars.push_back ("Dx1");
  mov_vars.push_back ("Dx2");
  mov_vars.push_back ("Dx3");
  mlSol.GetWriter()->SetMovingMesh (mov_vars);

  // and this?
  std::vector < std::string > variablesToBePrinted;
  variablesToBePrinted.push_back ("All");

  // and this?
  mlSol.GetWriter()->SetDebugOutput (false);
  mlSol.GetWriter()->Write ("./output1", "linear", variablesToBePrinted, 0);

  // First, solve system2 to "conformalize" the initial mesh.
  CopyDisplacement (mlSol, true);
  //system2.MGsolve();

  // Then, solve system0 to compute initial curvatures.
  CopyDisplacement (mlSol, false);
  system.CopySolutionToOldSolution();
  systemY.MGsolve();

  mlSol.GetWriter()->Write (DEFAULT_OUTPUTDIR, "linear", variablesToBePrinted, 0);

  // Parameters for the main algorithm loop.
  unsigned numberOfTimeSteps = 1000u;
  unsigned printInterval = 1u;
  
  std::fstream fs;
  int iproc;
  MPI_Comm_rank(MPI_COMM_WORLD, &iproc);
  if(iproc == 0) {
    fs.open ("Energy.txt", std::fstream::out);
  }
  
  
  // Main algorithm loop.
  for (unsigned time_step = 0; time_step < numberOfTimeSteps; time_step++) {
    system.CopySolutionToOldSolution();
    system.MGsolve();
    
    double energy = GetPWillmoreEnergy(mlSol);
    double dt = system.GetIntervalTime();
    double time = system.GetTime();
    if(iproc == 0) fs << dt <<" " << time << " " << energy << std::endl;

    dt0 *= 1.02;

    if (time_step % 1 == 0) {
      mlSol.GetWriter()->Write ("./output1", "linear", variablesToBePrinted, (time_step + 1) / printInterval);

      CopyDisplacement (mlSol, true);

      // if (time_step % 20 ==1) {
        system2.MGsolve();
      // }

      CopyDisplacement (mlSol, false);
      system.CopySolutionToOldSolution();
      systemY.MGsolve();
    }

    if ( (time_step + 1) % printInterval == 0)
      mlSol.GetWriter()->Write (DEFAULT_OUTPUTDIR, "linear", variablesToBePrinted, (time_step + 1) / printInterval);
  }
  
  if(iproc == 0) fs.close();
  return 0;
}


// Building the P-Willmore assembly function.
void AssembleMCF (MultiLevelProblem& ml_prob) {
  //  ml_prob is the global object from/to where get/set all the data
  //  level is the level of the PDE system to be assembled

  // Call the adept stack object.
  adept::Stack& s = FemusInit::_adeptStack;

  // Extract pointers to the several objects that we are going to use.
  TransientNonlinearImplicitSystem* mlPdeSys =
    &ml_prob.get_system<TransientNonlinearImplicitSystem> ("MCF");

  // Define level and time variable.
  double dt = mlPdeSys->GetIntervalTime();
  const unsigned level = mlPdeSys->GetLevelToAssemble();

  // Point to the mesh and element objects.
  Mesh *msh = ml_prob._ml_msh->GetLevel (level);
  elem *el = msh->el;

  // Point to mlSol, solution (level), and equation (level) objects.
  MultiLevelSolution *mlSol = ml_prob._ml_sol;
  Solution *sol = ml_prob._ml_sol->GetSolutionLevel (level);
  LinearEquationSolver *pdeSys = mlPdeSys->_LinSolver[level];

  // Point to the global stiffness mtx and residual vectors in pdeSys (level).
  SparseMatrix *KK = pdeSys->_KK;
  NumericVector *RES = pdeSys->_RES;

  // Convenience variables to encode the dimension.
  const unsigned dim = 2;
  const unsigned DIM = 3;

  // Get the process_id (for parallel computation).
  unsigned iproc = msh->processor_id();

  // Extract the solution vector; get solDx positions in the ml_sol object.
  unsigned solDxIndex[DIM];
  solDxIndex[0] = mlSol->GetIndex ("Dx1");
  solDxIndex[1] = mlSol->GetIndex ("Dx2");
  solDxIndex[2] = mlSol->GetIndex ("Dx3");

  // Extract the finite element type for solx.
  unsigned solxType;
  solxType = mlSol->GetSolutionType (solDxIndex[0]);

  // Get positions of solDx in the pdeSys object.
  unsigned solDxPdeIndex[DIM];
  solDxPdeIndex[0] = mlPdeSys->GetSolPdeIndex ("Dx1");
  solDxPdeIndex[1] = mlPdeSys->GetSolPdeIndex ("Dx2");
  solDxPdeIndex[2] = mlPdeSys->GetSolPdeIndex ("Dx3");

  // Define solx and solxOld.
  std::vector < adept::adouble > solx[DIM];
  std::vector < double > solxOld[DIM];

  // Get the finite element type for "x", it is always 2 (LAGRANGE QUADRATIC).
  unsigned xType = 2;

  // Get positions of Y in the ml_sol object.
  unsigned solYIndex[DIM];
  solYIndex[0] = mlSol->GetIndex ("Y1");
  solYIndex[1] = mlSol->GetIndex ("Y2");
  solYIndex[2] = mlSol->GetIndex ("Y3");

  // Extract the finite element type for Y.
  unsigned solYType;
  solYType = mlSol->GetSolutionType (solYIndex[0]);

  // Get positions of Y in the pdeSys object.
  unsigned solYPdeIndex[DIM];
  solYPdeIndex[0] = mlPdeSys->GetSolPdeIndex ("Y1");
  solYPdeIndex[1] = mlPdeSys->GetSolPdeIndex ("Y2");
  solYPdeIndex[2] = mlPdeSys->GetSolPdeIndex ("Y3");

  // Define solY and solYOld.
  std::vector < adept::adouble > solY[DIM];
  std::vector < double > solYOld[DIM];

  // Local-to-global pdeSys dofs.
  std::vector < unsigned > SYSDOF;

  // Define local residual vectors.
  vector < double > Res;
  std::vector< adept::adouble > aResx[3];
  std::vector< adept::adouble > aResY[3];

  // Local (column-ordered) Jacobian matrix
  vector < double > Jac;

  // Optional command for debugging.
  /* MatSetOption ( ( static_cast<PetscMatrix*> ( KK ) )->mat(),
      MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_FALSE ); */

  // Zero all entries of global matrix and residual vectors
  KK->zero();
  RES->zero();

  // Setting up solLambda1 and solLambda2.
  unsigned solLambaPdeIndex;

  adept::adouble solLambda1 = 0.;
  adept::adouble aResLambda1;
  unsigned lambda1PdeDof;

  adept::adouble solLambda2 = 0.;
  adept::adouble aResLambda2;
  unsigned lambda2PdeDof;

  // Don't know what's happening here. ?
  if (volumeConstraint || areaConstraint) {
    unsigned solLambdaIndex;
    solLambdaIndex = mlSol->GetIndex ("Lambda");
    solLambaPdeIndex = mlPdeSys->GetSolPdeIndex ("Lambda");

    if (volumeConstraint) {
      double lambda1;
      if (iproc == 0) {
        lambda1 = (*sol->_Sol[solLambdaIndex]) (0); // global to local solution
        lambda1PdeDof = pdeSys->GetSystemDof (solLambdaIndex,
                                              solLambaPdeIndex, 0, 0);
      }
      MPI_Bcast (&lambda1, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast (&lambda1PdeDof, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);
      solLambda1 = lambda1;
    }

    if (areaConstraint) {
      double lambda2;
      if (iproc == 0) {
        lambda2 = (*sol->_Sol[solLambdaIndex]) (volumeConstraint); // global to local solution
        lambda2PdeDof = pdeSys->GetSystemDof (solLambdaIndex,
                                              solLambaPdeIndex, 0, volumeConstraint);
      }
      MPI_Bcast (&lambda2, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Bcast (&lambda2PdeDof, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);
      solLambda2 = lambda2;
    }

    // No idea what is happening here -- something with parallelization?
    std::vector < double > value (2);
    std::vector < int > row (1);
    std::vector < int > columns (2);
    value[0] = 1;
    value[1] = -1;
    columns[1] = (volumeConstraint) ? lambda1PdeDof : lambda2PdeDof;

    // For equations other than Lagrange multiplier:
    for (int iel = msh->_elementOffset[iproc];
         iel < msh->_elementOffset[iproc + 1]; iel++) {
      if (iel > volumeConstraint * areaConstraint) {
        row[0] = pdeSys->GetSystemDof (solLambdaIndex,
                                       solLambaPdeIndex, 0, iel);
        columns[0] = row[0];
        KK->add_matrix_blocked (value, row, columns);
      }
    }
  }

  // Initialize area, volume, P-Willmore energy.
  double surface = 0.;
  double volume = 0.;
  double energy = 0.;

  // ELEMENT LOOP: each process loops only on the elements that it owns.
  for (int iel = msh->_elementOffset[iproc];
       iel < msh->_elementOffset[iproc + 1]; iel++) {

    // Number of solution element dofs.
    short unsigned ielGeom = msh->GetElementType (iel);
    unsigned nxDofs  = msh->GetElementDofNumber (iel, solxType);
    unsigned nYDofs  = msh->GetElementDofNumber (iel, solYType);

    // Resize solution vectors.
    for (unsigned K = 0; K < DIM; K++) {
      solx[K].resize (nxDofs);
      solxOld[K].resize (nxDofs);
      solY[K].resize (nYDofs);
      solYOld[K].resize (nYDofs);
    }

    // Convenience variable for keeping track of problem size.
    unsigned sizeAll = DIM * (nxDofs + nYDofs)
                       + volumeConstraint + areaConstraint;

    // Resize local arrays.
    SYSDOF.resize (sizeAll);
    Res.resize (sizeAll);
    for (unsigned K = 0; K < DIM; K++) {
      aResx[K].assign (nxDofs, 0.);
      aResY[K].assign (nYDofs, 0.);
    }
    aResLambda1 = 0.;
    aResLambda2 = 0.;

    // Loop which handles local storage of global mapping and solution X.
    for (unsigned i = 0; i < nxDofs; i++) {

      // Global-to-local mapping between solution node and solution dof.
      unsigned iDDof = msh->GetSolutionDof (i, iel, solxType);
      unsigned iXDof  = msh->GetSolutionDof (i, iel, xType);

      for (unsigned K = 0; K < DIM; K++) {
        solxOld[K][i] = (*msh->_topology->_Sol[K]) (iXDof)
                        + (*sol->_SolOld[solDxIndex[K]]) (iDDof);
        solx[K][i] = (*msh->_topology->_Sol[K]) (iXDof)
                     + (*sol->_Sol[solDxIndex[K]]) (iDDof);

        // Global-to-global mapping between solution node and pdeSys dof.
        SYSDOF[K * nxDofs + i] = pdeSys->GetSystemDof (solDxIndex[K],
                                                       solDxPdeIndex[K], i, iel);
      }
    }

    // Loop which handles local storage of global mapping and solution Y.
    for (unsigned i = 0; i < nYDofs; i++) {

      // Global-to-local mapping between solution node and solution dof.
      unsigned iYDof = msh->GetSolutionDof (i, iel, solYType);
      for (unsigned K = 0; K < DIM; K++) {

        // Global-to-local solutions.
        solYOld[K][i] = (*sol->_SolOld[solYIndex[K]]) (iYDof);
        solY[K][i] = (*sol->_Sol[solYIndex[K]]) (iYDof);

        // Global-to-global mapping between solution node and pdeSys dof.
        SYSDOF[DIM * nxDofs + K * nYDofs + i] =
          pdeSys->GetSystemDof (solYIndex[K], solYPdeIndex[K], i, iel);
      }
    }

    // Conditions for local storage of global Lagrange multipliers.
    if (volumeConstraint) {
      SYSDOF[sizeAll - 1u - areaConstraint ] = lambda1PdeDof;
    }

    if (areaConstraint) {
      SYSDOF[sizeAll - 1u ] = lambda2PdeDof;
    }

    // Start a new recording of all the operations involving adept variables.
    s.new_recording();

    // begin GAUSS POINT LOOP
    for (unsigned ig = 0; ig < msh->
         _finiteElement[ielGeom][solxType]->GetGaussPointNumber(); ig++) {

      const double *phix;  // local test function
      const double *phix_uv[dim]; // first order derivatives in (u,v)

      const double *phiY;  // local test function
      const double *phiY_uv[dim]; // first order derivatives in (u,v)

      double weight; // gauss point weight

      //Extract Gauss point weight, test functions, and their partial derivatives.
      // "0" is derivative in u, "1" is derivative in v.
      weight = msh->_finiteElement[ielGeom][solxType]->GetGaussWeight (ig);

      phix = msh->_finiteElement[ielGeom][solxType]->GetPhi (ig);
      phix_uv[0] = msh->_finiteElement[ielGeom][solxType]->GetDPhiDXi (ig);
      phix_uv[1] = msh->_finiteElement[ielGeom][solxType]->GetDPhiDEta (ig);

      phiY = msh->_finiteElement[ielGeom][solYType]->GetPhi (ig);
      phiY_uv[0] = msh->_finiteElement[ielGeom][solYType]->GetDPhiDXi (ig);
      phiY_uv[1] = msh->_finiteElement[ielGeom][solYType]->GetDPhiDEta (ig);

      // phiW = msh->_finiteElement[ielGeom][solWType]->GetPhi (ig);
      // phiW_uv[0] = msh->_finiteElement[ielGeom][solWType]->GetDPhiDXi (ig);
      // phiW_uv[1] = msh->_finiteElement[ielGeom][solWType]->GetDPhiDEta (ig);

      // Initialize quantities xNew, xOld, Y, W at the Gauss points.
      adept::adouble solxNewg[3] = {0., 0., 0.};
      double solxOldg[3] = {0., 0., 0.};
      adept::adouble solYg[3] = {0., 0., 0.};

      // Initialize derivatives of x and W (new, middle, old) at the Gauss points.
      adept::adouble solxNew_uv[3][2] = {{0., 0.}, {0., 0.}, {0., 0.}};
      adept::adouble solx_uv[3][2] = {{0., 0.}, {0., 0.}, {0., 0.}};
      double solxOld_uv[3][2] = {{0., 0.}, {0., 0.}, {0., 0.}};

      //adept::adouble solYNew_uv[3][2] = {{0., 0.}, {0., 0.}, {0., 0.}};
      //adept::adouble solY_uv[3][2] = {{0., 0.}, {0., 0.}, {0., 0.}};
      //double solYOld_uv[3][2] = {{0., 0.}, {0., 0.}, {0., 0.}};

      // Compute the above quantities at the Gauss points.
      for (unsigned K = 0; K < DIM; K++) {

        for (unsigned i = 0; i < nxDofs; i++) {
          solxNewg[K] += phix[i] * solx[K][i];
          solxOldg[K] += phix[i] * solxOld[K][i];
        }

        for (unsigned i = 0; i < nYDofs; i++) {
          //solYNewg[K] += phiY[i] * solY[K][i];
          solYg[K] += phiY[i] * 0.5 * (solYOld[K][i] + solY[K][i]);
        }

        for (int j = 0; j < dim; j++) {
          for (unsigned i = 0; i < nxDofs; i++) {
            solxNew_uv[K][j]    += phix_uv[j][i] * solx[K][i];
            solx_uv[K][j]    += phix_uv[j][i] * 0.5 * (solx[K][i] + solxOld[K][i]);
            solxOld_uv[K][j] += phix_uv[j][i] * solxOld[K][i];
          }
        }

      //   for (int j = 0; j < dim; j++) {
      //     for (unsigned i = 0; i < nYDofs; i++) {
      //       solYNew_uv[K][j] += phiY_uv[j][i] * solY[K][i];
      //       solY_uv[K][j] += phiY_uv[j][i] * 0.5 * (solY[K][i] + solYOld[K][i]);
      //       solYOld_uv[K][j] += phiY_uv[j][i] * solYOld[K][i];
      //     }
      //   }
      }

      // Computing the metric, metric determinant, and area element.
      adept::adouble g[dim][dim] = {{0., 0.}, {0., 0.}};
      for (unsigned i = 0; i < dim; i++) {
        for (unsigned j = 0; j < dim; j++) {
          for (unsigned K = 0; K < DIM; K++) {
            g[i][j] += solx_uv[K][i] * solx_uv[K][j];
          }
        }
      }
      adept::adouble detg = g[0][0] * g[1][1] - g[0][1] * g[1][0];
      adept::adouble Area = weight * sqrt (detg);

      // Computing the unit normal vector N.
      adept::adouble normal[DIM];
      normal[0] = normalSign * (solx_uv[1][0] * solx_uv[2][1]
                                - solx_uv[2][0] * solx_uv[1][1]) / sqrt (detg);
      normal[1] = normalSign * (solx_uv[2][0] * solx_uv[0][1]
                                - solx_uv[0][0] * solx_uv[2][1]) / sqrt (detg);
      normal[2] = normalSign * (solx_uv[0][0] * solx_uv[1][1]
                                - solx_uv[1][0] * solx_uv[0][1]) / sqrt (detg);

      // Computing Y.N and |Y|^2, which are essentially 2H and 4H^2.
      adept::adouble YdotN = 0.;
      adept::adouble YdotY = 0.;
      for (unsigned K = 0; K < DIM; K++) {
        YdotN += solYg[K] * normal[K];
        YdotY += solYg[K] * solYg[K];
      }
      double signYdotN = (YdotN.value() >= 0.) ? 1. : -1.;

      // Some necessary quantities when working with polynomials.
      //adept::adouble sumP1 = 0.;
      //adept::adouble sumP2 = 0.;
      adept::adouble sumP3 = 0.;
      for (unsigned p = 0; p < 3; p++) {
        double signP = (P[p] % 2u == 0) ? 1. : signYdotN;
        //sumP1 += signP * ap[p] * P[p] * pow (YdotY, (P[p] - 2.) / 2.);
        //sumP2 += signP * ap[p] * (1. - P[p]) * pow (YdotY , P[p] / 2.);
        //sumP2 += signP * (ap[p] - ap[p] * P[p]) * pow (YdotY , P[p]/2.);
        sumP3 += signP * ap[p] * pow (YdotY, P[p] / 2.);
      }

      // Computing the metric inverse
      adept::adouble gi[dim][dim];
      gi[0][0] =  g[1][1] / detg;
      gi[0][1] = -g[0][1] / detg;
      gi[1][0] = -g[1][0] / detg;
      gi[1][1] =  g[0][0] / detg;

      // Computing the "reduced Jacobian" g^{ij}X_j .
      adept::adouble Jir[dim][DIM] = {{0., 0., 0.}, {0., 0., 0.}};
      for (unsigned i = 0; i < dim; i++) {
        for (unsigned J = 0; J < DIM; J++) {
          for (unsigned k = 0; k < dim; k++) {
            Jir[i][J] += gi[i][k] * solx_uv[J][k];
          }
        }
      }

      // Initializing tangential gradients of X and W (new, middle, old).
      adept::adouble solxNew_Xtan[DIM][DIM] = {{0., 0., 0.}, {0., 0., 0.}, {0., 0., 0.}};
      adept::adouble solx_Xtan[DIM][DIM] = {{0., 0., 0.}, {0., 0., 0.}, {0., 0., 0.}};
      adept::adouble solxOld_Xtan[DIM][DIM] = {{0., 0., 0.}, {0., 0., 0.}, {0., 0., 0.}};

      // adept::adouble solYNew_Xtan[DIM][DIM] = {{0., 0., 0.}, {0., 0., 0.}, {0., 0., 0.}};
      // adept::adouble solY_Xtan[DIM][DIM] = {{0., 0., 0.}, {0., 0., 0.}, {0., 0., 0.}};
      // adept::adouble solYOld_Xtan[DIM][DIM] = {{0., 0., 0.}, {0., 0., 0.}, {0., 0., 0.}};

      // Computing tangential gradients defined above.
      for (unsigned I = 0; I < DIM; I++) {
        for (unsigned J = 0; J < DIM; J++) {
          for (unsigned k = 0; k < dim; k++) {
            solxNew_Xtan[I][J] += solxNew_uv[I][k] * Jir[k][J];
            solx_Xtan[I][J] += solx_uv[I][k] * Jir[k][J];
            solxOld_Xtan[I][J] += solxOld_uv[I][k] * Jir[k][J];

            // solYNew_Xtan[I][J] += solYNew_uv[I][k] * Jir[k][J];
            // solY_Xtan[I][J] += solY_uv[I][k] * Jir[k][J];
            // solYOld_Xtan[I][J] += solYOld_uv[I][k] * Jir[k][J];
          }
        }
      }

      // Define and compute gradients of test functions for X and W.
      std::vector < adept::adouble > phix_Xtan[DIM];
      std::vector < adept::adouble > phiY_Xtan[DIM];

      for (unsigned J = 0; J < DIM; J++) {
        phix_Xtan[J].assign (nxDofs, 0.);
        phiY_Xtan[J].assign (nYDofs, 0.);

        for (unsigned inode  = 0; inode < nxDofs; inode++) {
          for (unsigned k = 0; k < dim; k++) {
            phix_Xtan[J][inode] += phix_uv[k][inode] * Jir[k][J];
          }
        }

        for (unsigned inode  = 0; inode < nYDofs; inode++) {
          for (unsigned k = 0; k < dim; k++) {
            phiY_Xtan[J][inode] += phiY_uv[k][inode] * Jir[k][J];
          }
        }
      }

      // Implement the curvature equation Y = \Delta X .
      for (unsigned K = 0; K < DIM; K++) {
        for (unsigned i = 0; i < nxDofs; i++) {
          adept::adouble term1 = 0.;

          for (unsigned J = 0; J < DIM; J++) {
            term1 +=  solxNew_Xtan[K][J] * phix_Xtan[J][i];
          }
          aResx[K][i] += (solYg[K] * phix[i] + term1) * Area;
        }

        // Implement the MCF equation.
        for (unsigned i = 0; i < nYDofs; i++) {
          adept::adouble term1 = 0.;

          for (unsigned J = 0; J < DIM; J++) {
            term1 +=  solxNew_Xtan[K][J] * phiY_Xtan[J][i];
          }
          aResY[K][i] += ( ( ( (solxNewg[K] - solxOldg[K])  / dt) + term1
                          + solLambda1 * normal[K] ) * phiY[i]
                          + solLambda2 * term1) * Area;
        }

        // Lagrange multiplier (volume) equation Dx.N = 0.
        if (volumeConstraint) {
          aResLambda1 += ( (solxNewg[K] - solxOldg[K]) * normal[K]) * Area;
        }

        // Lagrange multiplier (area) equation.
        if (areaConstraint) {
          //aResLambda2 += ( -YdotN * (solxNewg[K] - solxOldg[K]) * normal[K]) * Area;

          adept::adouble term1t = 0.;
          for (unsigned J = 0; J < DIM; J++) {
            term1t +=  solx_Xtan[K][J] * (solxNew_Xtan[K][J] - solxOld_Xtan[K][J]) ;
          }
          aResLambda2 += term1t * Area;
        }
      }

      // Compute new surface area, volume, and P-Willmore energy.
      for (unsigned K = 0; K < DIM; K++) {
        surface += Area.value();
        volume += normalSign * (solxNewg[K].value() * normal[K].value()) * Area.value();
      }
      energy += sumP3.value() * Area.value();

    } // end GAUSS POINT LOOP.

    //------------------------------------------------------------------------
    // Add the local Matrix/Vector into the global Matrix/Vector
    //copy the value of the adept::adoube aRes in double Res and store

    for (int K = 0; K < DIM; K++) {

      for (int i = 0; i < nxDofs; i++) {
        Res[ K * nxDofs + i] = -aResx[K][i].value();
      }

      for (int i = 0; i < nYDofs; i++) {
        Res[DIM * nxDofs + K * nYDofs + i] = -aResY[K][i].value();
      }
    }

    if (volumeConstraint) {
      Res[sizeAll - 1u - areaConstraint] = - aResLambda1.value();
    }

    if (areaConstraint) {
      Res[sizeAll - 1u] = - aResLambda2.value();
    }

    // ???
    RES->add_vector_blocked (Res, SYSDOF);

    // Resize Jacobian.
    Jac.resize (sizeAll * sizeAll);

    // Define the dependent variables.
    for (int K = 0; K < DIM; K++) {
      s.dependent (&aResx[K][0], nxDofs);
    }

    for (int K = 0; K < DIM; K++) {
      s.dependent (&aResY[K][0], nYDofs);
    }

    if (volumeConstraint) {
      s.dependent (&aResLambda1, 1);
    }

    if (areaConstraint) {
      s.dependent (&aResLambda2, 1);
    }

    // Define the independent variables.
    for (int K = 0; K < DIM; K++) {
      s.independent (&solx[K][0], nxDofs);
    }

    for (int K = 0; K < DIM; K++) {
      s.independent (&solY[K][0], nYDofs);
    }

    if (volumeConstraint) {
      s.independent (&solLambda1, 1);
    }

    if (areaConstraint) {
      s.independent (&solLambda2, 1);
    }

    // Get the Jacobian matrix (ordered by row).
    s.jacobian (&Jac[0], true);

    KK->add_matrix_blocked (Jac, SYSDOF, SYSDOF);

    s.clear_independents();
    s.clear_dependents();

  } // End ELEMENT LOOP for each process.

  RES->close();
  KK->close();

  // Get data from each process running in parallel.
  double surfaceAll;
  MPI_Reduce (&surface, &surfaceAll, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  std::cout << "SURFACE = " << surfaceAll << std::endl;

  double volumeAll;
  MPI_Reduce (&volume, &volumeAll, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  std::cout << "VOLUME = " << volumeAll << std::endl;

  double energyAll;
  MPI_Reduce (&energy, &energyAll, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  std::cout << "ENERGY = " << energyAll << std::endl;

// For debugging:
//   VecView ( (static_cast<PetscVector*> (RES))->vec(),  PETSC_VIEWER_STDOUT_SELF);
//   MatView ( (static_cast<PetscMatrix*> (KK))->mat(), PETSC_VIEWER_STDOUT_SELF);

//     PetscViewer    viewer;
//     PetscViewerDrawOpen (PETSC_COMM_WORLD, NULL, NULL, 0, 0, 900, 900, &viewer);
//     PetscObjectSetName ( (PetscObject) viewer, "PWilmore matrix");
//     PetscViewerPushFormat (viewer, PETSC_VIEWER_DRAW_LG);
//     MatView ( (static_cast<PetscMatrix*> (KK))->mat(), viewer);
//     double a;
//     std::cin >> a;

  firstTime = false;

} // end AssemblePWillmore.
