#include "MultiLevelProblem.hpp"
#include "NumericVector.hpp"
#include "Fluid.hpp"
#include "Solid.hpp"
#include "Parameter.hpp"
#include "FemusInit.hpp"
#include "SparseMatrix.hpp"
#include "FElemTypeEnum.hpp"
#include "Files.hpp"
#include "MonolithicFSINonLinearImplicitSystem.hpp"
#include "TransientSystem.hpp"
#include "VTKWriter.hpp"
#include "MyVector.hpp"
#include "../include/FSITimeDependentAssemblySupgNonConservativeTwoPressures.hpp"
#include <cmath>
double scale = 1000.;

using namespace std;
using namespace femus;

double SetVariableTimeStep(const double time);

bool SetBoundaryConditionVeinValve(const std::vector < double >& x, const char name[],
                                   double& value, const int facename, const double time);

void GetSolutionFluxes(MultiLevelSolution& mlSol, std::vector <double>& fluxes);

void PrintConvergenceInfo(char *stdOutfile, const unsigned &numberOfUniformRefinedMeshes, const int &nprocs);
//------------------------------------------------------------------------------------------------------------------


int main(int argc, char** args)
{

  // ******* Init Petsc-MPI communicator *******
  FemusInit mpinit(argc, args, MPI_COMM_WORLD);

  //Files files;
  //files.CheckIODirectories(true);
  //files.RedirectCout(true);

  // ******* Extract the problem dimension and simulation identifier based on the inline input *******

  clock_t start_time = clock();

  valve = true;
  twoPressure = true;

  //std::string infile = "./../input/valve/2D/valve2.neu";
  //std::string infile = "./../input/valve/2D/valve2_corta2bis.neu";
  std::string infile = "./../input/valve/2D/valve2_corta2bis_moreElem.neu";
  //std::string infile = "./../input/valve/3D/valve3D_corta2bis.neu";
  //std::string infile = "./../input/valve/3D/valve3D_corta2bis_moreElem.neu";

  // ******* Set physics parameters *******
  double Lref, Uref, rhof, muf, rhos, ni, E, E1, ni1;

  Lref = 1.;
  Uref = 1.;

  rhof = 1060.;
  muf = 2.2 * 1.0e-3;
  rhos = 960;

  ni = 0.5;

  E = 260 * 1.0e6; //vein young modulus \\15, 30, 30, 40, 60, 260, 260
  //E = 4.3874951 * 1.0e12;
  E1 = 7.5 * 1.0e6; //leaflet young modulus \\0.5, 0.8, 1, 1.5, 1.5, 2.2, 1.5
  ni1 = 0.5; //0.4999
  
  Parameter par(Lref, Uref);

  // Generate Solid Object
  Solid solid;
  solid = Solid(par, E, ni, rhos, "Mooney-Rivlin");

  Solid solid1;
  solid1 = Solid(par, E1, ni1, rhos, "Mooney-Rivlin");

  cout << "Solid properties: " << endl;
  cout << solid << endl;

  // Generate Fluid Object
  Fluid fluid(par, muf, rhof, "Newtonian");
  cout << "Fluid properties: " << endl;
  cout << fluid << endl;

  // ******* Init multilevel mesh from mesh.neu file *******
  unsigned short numberOfUniformRefinedMeshes, numberOfAMRLevels;

  numberOfUniformRefinedMeshes = 4;

  numberOfAMRLevels = 0;

  MultiLevelMesh ml_msh(numberOfUniformRefinedMeshes + numberOfAMRLevels, numberOfUniformRefinedMeshes,
                        infile.c_str(), "fifth", Lref, NULL);

  unsigned dim = ml_msh.GetDimension();

  //ml_msh.EraseCoarseLevels(numberOfUniformRefinedMeshes - 2);

  ml_msh.PrintInfo();

  // ******* Init multilevel solution ******
  MultiLevelSolution ml_sol(&ml_msh);

  // ******* Add solution variables to multilevel solution and pair them *******
  ml_sol.AddSolution("DX", LAGRANGE, SECOND, 2);
  ml_sol.AddSolution("DY", LAGRANGE, SECOND, 2);
  if(dim == 3) ml_sol.AddSolution("DZ", LAGRANGE, SECOND, 2);

  ml_sol.AddSolution("U", LAGRANGE, SECOND, 2);
  ml_sol.AddSolution("V", LAGRANGE, SECOND, 2);
  if(dim == 3) ml_sol.AddSolution("W", LAGRANGE, SECOND, 2);

  // Pair each velocity variable with the corresponding displacement variable
  ml_sol.PairSolution("U", "DX");    // Add this line
  ml_sol.PairSolution("V", "DY");    // Add this line
  if(dim == 3) ml_sol.PairSolution("W", "DZ");    // Add this line

  ml_sol.AddSolution("PS", DISCONTINUOUS_POLYNOMIAL, FIRST, 2);
  ml_sol.AssociatePropertyToSolution("PS", "Pressure", false);    // Add this line

  // Since the Pressure is a Lagrange multiplier it is used as an implicit variable
  ml_sol.AddSolution("PF", DISCONTINUOUS_POLYNOMIAL, FIRST, 2);
  ml_sol.AssociatePropertyToSolution("PF", "Pressure", false);    // Add this line

  ml_sol.AddSolution("lmbd", DISCONTINUOUS_POLYNOMIAL, ZERO, 0, false);

  ml_sol.AddSolution("Um", LAGRANGE, SECOND, 0, false);
  ml_sol.AddSolution("Vm", LAGRANGE, SECOND, 0, false);
  if(dim == 3)  ml_sol.AddSolution("Wm", LAGRANGE, SECOND, 0, false);

//   ml_sol.AddSolution("AX", LAGRANGE, SECOND, 2);
//   ml_sol.AddSolution("AY", LAGRANGE, SECOND, 2);
//   if(dim == 3) ml_sol.AddSolution("AZ", LAGRANGE, SECOND, 2);

//   // ******* Initialize solution *******
  ml_sol.Initialize("All");


  ml_sol.AttachSetBoundaryConditionFunction(SetBoundaryConditionVeinValve);

  // ******* Set boundary conditions *******
  ml_sol.GenerateBdc("DX", "Steady");
  ml_sol.GenerateBdc("DY", "Steady");
  if(dim == 3) ml_sol.GenerateBdc("DZ", "Steady");

  ml_sol.GenerateBdc("U", "Steady");
  ml_sol.GenerateBdc("V", "Steady");
  if(dim == 3) ml_sol.GenerateBdc("W", "Steady");

  ml_sol.GenerateBdc("PF", "Steady");
  ml_sol.GenerateBdc("PS", "Steady");

  // ******* Define the FSI Multilevel Problem *******

  MultiLevelProblem ml_prob(&ml_sol);
  // Add fluid object
  ml_prob.parameters.set<Fluid> ("Fluid") = fluid;
  // Add Solid Object
  ml_prob.parameters.set<Solid> ("Solid") = solid;
  ml_prob.parameters.set<Solid> ("Solid1") = solid1;

  // ******* Add FSI system to the MultiLevel problem *******
  TransientMonolithicFSINonlinearImplicitSystem& system = ml_prob.add_system<TransientMonolithicFSINonlinearImplicitSystem> ("Fluid-Structure-Interaction");

  system.AddSolutionToSystemPDE("DX");
  system.AddSolutionToSystemPDE("DY");
  if(dim == 3) system.AddSolutionToSystemPDE("DZ");

  system.AddSolutionToSystemPDE("U");
  system.AddSolutionToSystemPDE("V");
  if(dim == 3) system.AddSolutionToSystemPDE("W");

  system.AddSolutionToSystemPDE("PS");

  if(twoPressure) system.AddSolutionToSystemPDE("PF");

  // ******* System Fluid-Structure-Interaction Assembly *******
  system.SetAssembleFunction(FSITimeDependentAssemblySupgNew2);

  // ******* set MG-Solver *******
  system.SetMgType(F_CYCLE);

  system.SetNonLinearConvergenceTolerance(1.e-7);
  //system.SetResidualUpdateConvergenceTolerance ( 1.e-15 );
  system.SetMaxNumberOfNonLinearIterations(20); //20
  //system.SetMaxNumberOfResidualUpdatesForNonlinearIteration ( 4 );

  system.SetMaxNumberOfLinearIterations(1);
  system.SetAbsoluteLinearConvergenceTolerance(1.e-50);

  system.SetNumberPreSmoothingStep(1);
  system.SetNumberPostSmoothingStep(1);

  // ******* Set Preconditioner *******

  system.SetLinearEquationSolverType(FEMuS_ASM);

  system.init();

  // ******* Set Smoother *******
  system.SetSolverFineGrids(RICHARDSON);
  system.SetRichardsonScaleFactor(0.4);
  //system.SetRichardsonScaleFactor(.5, .5);
  //system.SetSolverFineGrids(GMRES);

  system.SetPreconditionerFineGrids(MLU_PRECOND);
  if(dim == 3) system.SetPreconditionerFineGrids(MLU_PRECOND);

  if(dim==2){
    system.SetTolerances(1.e-10, 1.e-12, 1.e+50, 40, 40);
  }
  else{
    system.SetTolerances(1.e-10, 1.e-12, 1.e+50, 40, 40);
  }

  // ******* Add variables to be solved *******
  system.ClearVariablesToBeSolved();
  system.AddVariableToBeSolved("All");

  // ******* Set the last (1) variables in system (i.e. P) to be a schur variable *******
  //   // ******* Set block size for the ASM smoothers *******
  
  // ******* Set block size for the ASM smoothers *******
  
  system.SetElementBlockNumber(4);
  
  
  if(twoPressure)
    system.SetNumberOfSchurVariables(2);
  else 
    system.SetNumberOfSchurVariables(1);

  unsigned time_step_start = 1;

  //char restart_file_name[256] = "./save/valve2D_iteration28";
  char restart_file_name[256] = "";

  if(strcmp(restart_file_name, "") != 0) {
    ml_sol.LoadSolution(restart_file_name);
    time_step_start = 29;
    system.SetTime((time_step_start - 1) * 1. / 32);
  }

  // ******* Print solution *******
  ml_sol.SetWriter(VTK);


  std::vector<std::string> print_vars;
  print_vars.push_back("All");

  std::vector<std::string> mov_vars;
  mov_vars.push_back("DX");
  mov_vars.push_back("DY");
  if(dim == 3)mov_vars.push_back("DZ");
  ml_sol.GetWriter()->SetDebugOutput(true);
  ml_sol.GetWriter()->SetMovingMesh(mov_vars);
  ml_sol.GetWriter()->Write(DEFAULT_OUTPUTDIR, "biquadratic", print_vars, time_step_start - 1);


  // ******* Solve *******
  std::cout << std::endl;
  std::cout << " *********** Fluid-Structure-Interaction ************  " << std::endl;

  // time loop parameter
  system.AttachGetTimeIntervalFunction(SetVariableTimeStep);
  const unsigned int n_timesteps = 512;

  //std::vector < std::vector <double> > data(n_timesteps);

  int  iproc;
  MPI_Comm_rank(MPI_COMM_WORLD, &iproc);


  std::ofstream outf;
  if(iproc == 0) {
    //char *foutname;
    char foutname[100];
    sprintf(foutname, "fluxes_E1=%g_level=%d_incomp_dt128.txt",E1,numberOfUniformRefinedMeshes);
    //sprintf(foutname, "ksp_fluxes_E1=%g.txt",E1);
    outf.open(foutname);
    
    if(!outf) {
      std::cout << "Error in opening file DataPrint.txt";
      return 1;
    }
  }

//   std::vector < double > Qtot(3, 0.);
//   std::vector<double> fluxes(2, 0.);

  system.ResetComputationalTime();
  
  for(unsigned time_step = time_step_start; time_step <= n_timesteps; time_step++) {

    system.CopySolutionToOldSolution();

    for(unsigned level = 0; level < numberOfUniformRefinedMeshes; level++) {
      SetLambdaNew(ml_sol, level , SECOND, ELASTICITY);
    }

    if(time_step > 1)
      system.SetMgType(V_CYCLE);


    system.MGsolve();
    system.PrintComputationalTime();

    StoreMeshVelocity(ml_prob);

    //fluxes
//     double dt = system.GetIntervalTime();
// 
//     Qtot[0] += 0.5 * dt * fluxes[0];
//     Qtot[1] += 0.5 * dt * fluxes[1];
// 
//     GetSolutionFluxes(ml_sol, fluxes);
// 
//     Qtot[0] += 0.5 * dt * fluxes[0];
//     Qtot[1] += 0.5 * dt * fluxes[1];
//     Qtot[2] = Qtot[0] + Qtot[1];
// 
// 
//     std::cout << fluxes[0] << " " << fluxes[1] << " " << Qtot[0] << " " << Qtot[1] << " " << Qtot[2] << std::endl;
// 
// 
//     if(iproc == 0) {
//       outf << time_step << "," << system.GetTime() << "," << fluxes[0] << "," << fluxes[1] << "," << Qtot[0] << "," << Qtot[1] << "," << Qtot[2] << std::endl;
//     }

    //ml_sol.GetWriter()->SetMovingMesh(mov_vars);
    //ml_sol.GetWriter()->Write(DEFAULT_OUTPUTDIR, "biquadratic", print_vars, time_step);

    //if(time_step % 1 == 0) ml_sol.SaveSolution("valve2D", time_step);

  }

//   if(iproc == 0) {
//     outf.close();
//   }


  
  //******* Clear all systems *******
  ml_prob.clear();
  
  std::cout << " TOTAL TIME:\t" << \
            static_cast<double>(clock() - start_time) / CLOCKS_PER_SEC << std::endl;
    
//   int  nprocs;	    
//   MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
//   if(iproc == 0){
//     char stdOutputName[100];
//     sprintf(stdOutputName, "stdoutput_level%d_nprocs%d_stiffness10.txt",numberOfUniformRefinedMeshes, nprocs);
//     PrintConvergenceInfo(stdOutputName, numberOfUniformRefinedMeshes, nprocs);
//   }
//     
  return 0;
}

//---------------------------------------------------------------------------------------------------------------------

double SetVariableTimeStep(const double time)
{
  double dt = 1. / 64;
//   double shiftedTime = time - floor(time);
//   if (time > 1 && shiftedTime >= 0.125 && shiftedTime < 0.25) {
//     dt = 1. / 64;
//   }
//   std::cout << " Shifted Time = " << shiftedTime << " dt = " << dt << std::endl;

  return dt;
}


//---------------------------------------------------------------------------------------------------------------------

bool SetBoundaryConditionVeinValve(const std::vector < double >& x, const char name[], double& value, const int facename, const double time)
{
  bool test = 1; //dirichlet
  value = 0.;

  double PI = acos(-1.);
  double ramp = (time < 2) ? sin(PI / 2 * time / 2.) : 1.;

  if(!strcmp(name, "U")) {
    if(5 == facename || 7 == facename) {
      test = 0;
      value = 0;
    }
  }
  else if(!strcmp(name, "V")) {
    if(1 == facename || 2 == facename || 5 == facename || 6 == facename || 7 == facename) {
      test = 0;
      value = 0;
    }
  }
  else if(!strcmp(name, "W")) {
    if(5 == facename || 6 == facename) {
      test = 0;
      value = 0;
    }
  }

  else if(!strcmp(name, "PS")) {
    test = 0;
    value = 0.;
    if(1 == facename) {
      //value = -1;
      //value = ( /*2.5*/ + 2.5 * sin ( 2 * PI * time ) ) * ramp;
      //value = ( 5 + 3 * sin ( 2 * PI * time ) ) * ramp; //+ 4.5
      //value = ( 6 + 3 * sin ( 2 * PI * time ) ) * ramp; //+ 4.5
      //value = ( 12 + 9 * sin ( 2 * PI * time ) ) * ramp; //runna
      //value = ( 24 + 21 * sin ( 2 * PI * time ) ) * ramp; //runna
      value = (0 + 15 * sin(2 * PI * time)) * ramp;      //+ 3.5, 6, 7, 10, 10, 15, 15
    }
    else if(2 == facename) {
      //value = 1;
      //value = ( /*2.5*/ - 2.5 * sin ( 2 * PI * time ) ) * ramp;
      //value = ( 4 - 1 * sin ( 2 * PI * time ) ) * ramp; //- 4.5
      //value = ( 5 - 3 * sin ( 2 * PI * time ) ) * ramp; //non runna
      value = (0 - 15 * sin(2 * PI * time)) * ramp;      //- 3.5, 6, 7, 10, 10, 15, 15
    }
    else if(7 == facename) {
      Kslip = 0.;
    }
  }
  else if(!strcmp(name, "PF")) {
    test = 0;
    value = 0.;
  }
  else if(!strcmp(name, "DX")) {
    if(5 == facename || 7 == facename) {
      test = 0;
      value = 0;
    }
  }
  else if(!strcmp(name, "DY")) {
    if(5 == facename || 6 == facename || 7 == facename) {
      test = 0;
      value = 0;
    }
  }
  else if(!strcmp(name, "DZ")) {
    if(5 == facename || 6 == facename) {
      test = 0;
      value = 0;
    }
  }

  return test;

}

//---------------------------------------------------------------------------------------------------------------------

void GetSolutionFluxes(MultiLevelSolution& mlSol, std::vector <double>& fluxes)
{

  int  iproc, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &iproc);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

  MyVector<double> qTop(1, 0);
  qTop.stack();

  MyVector<double> qBottom(1, 0);
  qBottom.stack();

  unsigned level = mlSol._mlMesh->GetNumberOfLevels() - 1;

  Solution* solution  = mlSol.GetSolutionLevel(level);
  Mesh* msh = mlSol._mlMesh->GetLevel(level);
  elem* myel =  msh->el;

  const unsigned dim = msh->GetDimension();
  const unsigned max_size = static_cast< unsigned >(ceil(pow(3, dim)));

  vector< vector < double> >  sol(dim);
  vector< vector < double> > x(dim);

  const char varname[6][3] = {"U", "V", "W", "DX", "DY", "DZ"};
  vector <unsigned> indVar(2 * dim);
  unsigned solType;

  for(unsigned ivar = 0; ivar < dim; ivar++) {
    for(unsigned k = 0; k < 2; k++) {
      indVar[ivar + k * dim] = mlSol.GetIndex(&varname[ivar + k * 3][0]);
    }
  }
  solType = mlSol.GetSolutionType(&varname[0][0]);


  std::vector < double > phi;
  std::vector < double > gradphi;
  std::vector< double > xx(dim, 0.);
  double weight;

  for(int iel = msh->_elementOffset[iproc]; iel < msh->_elementOffset[iproc + 1]; iel++) {
    vector < double> normal(dim, 0);

    // loop on faces
    for(unsigned jface = 0; jface < msh->GetElementFaceNumber(iel); jface++) {


      int faceNumber = myel->GetBoundaryIndex(iel, jface);
      // look for boundary faces
      if(faceNumber == 1 || faceNumber == 2) {

        unsigned nve = msh->GetElementFaceDofNumber(iel, jface, solType);
        const unsigned felt = msh->GetElementFaceType(iel, jface);

        for(unsigned d = 0; d < dim; d++) {
          x[d].resize(nve);
          sol[d].resize(nve);
        }

        for(unsigned i = 0; i < nve; i++) {
          unsigned int ilocal = msh->GetLocalFaceVertexIndex(iel, jface, i);
          unsigned idof = msh->GetSolutionDof(ilocal, iel, 2);
          for(unsigned d = 0; d < dim; d++) {
            x[d][i] = (*msh->_topology->_Sol[d])(idof) + (*solution->_Sol[indVar[d + dim]])(idof);;
            sol[d][i] = (*solution->_Sol[indVar[d]])(idof);;
          }
        }

        double flux = 0.;
        for(unsigned igs = 0; igs < msh->_finiteElement[felt][solType]->GetGaussPointNumber(); igs++) {
          msh->_finiteElement[felt][solType]->JacobianSur(x, igs, weight, phi, gradphi, normal);
          double value;
          for(unsigned i = 0; i < nve; i++) {
            value = 0.;
            for(unsigned d = 0; d < dim; d++) {
              value += normal[d] * sol[d][i];
            }
            value *= phi[i];
          }
          flux += value * weight;
        }
        if(faceNumber == 1) qBottom[iproc] += flux;
        else qTop[iproc] += flux;
      }
    }
  }

  fluxes[0] = 0.;
  fluxes[1] = 0.;
  for(int j = 0; j < nprocs; j++) {
    qBottom.broadcast(j);
    qTop.broadcast(j);
    fluxes[0] += qBottom[j];
    fluxes[1] += qTop[j];
    qBottom.clearBroadcast();
    qTop.clearBroadcast();
  }
}

//---------------------------------------------------------------------------------------------------------------------

void PrintConvergenceInfo(char *stdOutfile, const unsigned &level, const int &nprocs){

  std::cout<<"END_COMPUTATION\n"<<std::flush;

  std::ifstream inf;
  inf.open(stdOutfile);
  if (!inf) {
    std::cout<<"Redirected standard output file not found\n";
    std::cout<<"add option -std_output std_out_filename > std_out_filename\n";
    return;
  }

  std::ofstream outf;
  char outFileName[100];
  sprintf(outFileName, "valve2D_convergence_level%d_nprocs%d_stiffness10.txt",level, nprocs);

  outf.open(outFileName, std::ofstream::app);
  outf << std::endl << std::endl;
  outf << "Number_of_refinements="<<level<<std::endl;
  outf << "Simulation_Time,Nonlinear_Iteration,resid_norm0,resid_normN,N,convergence";

  std::string str1;
  inf >> str1;
  double simulationTime=0.;
  while (str1.compare("END_COMPUTATION") != 0) {

    if (str1.compare("Simulation") == 0){
      inf >> str1;
      if (str1.compare("Time:") == 0){
        inf >> simulationTime;
      }
    }
    else if (str1.compare("Nonlinear") == 0) {
      inf >> str1;
      if (str1.compare("iteration") == 0) {
        inf >> str1;
        outf << std::endl << simulationTime<<","<<str1;
      }
    }
    else if (str1.compare("KSP") == 0){
      inf >> str1;
      if (str1.compare("preconditioned") == 0){
        inf >> str1;
        if (str1.compare("resid") == 0){
          inf >> str1;
          if (str1.compare("norm") == 0){
            double norm0 = 1.;
            double normN = 1.;
            unsigned counter = 0;
            inf >> norm0;
            outf <<","<< norm0;
            for (unsigned i = 0; i < 11; i++){
              inf >> str1;
            }
            while(str1.compare("norm") == 0){
              inf >> normN;
              counter++;
              for (unsigned i = 0; i < 11; i++){
                inf >> str1;
              }
            }
            outf <<","<< normN;
            if(counter != 0){
              outf << "," <<counter<< "," << pow(normN/norm0,1./counter);
            }
            else{
              outf << "Invalid solver, set -outer_ksp_solver \"gmres\"";
            }
          }
        }
      }
    }
    inf >> str1;
  }

  outf.close();
  inf.close();

}


