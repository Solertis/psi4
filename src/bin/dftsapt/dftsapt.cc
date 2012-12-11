#include "dftsapt.h"
#include <libmints/mints.h>
#include <libmints/sieve.h>
#include <libfock/jk.h>
#include <libqt/qt.h>
#include <libpsio/psio.hpp>
#include <libpsio/psio.h>
#include <psi4-dec.h>
#include <psifiles.h>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace psi;
using namespace boost;
using namespace std;

namespace psi {
namespace dftsapt {

DFTSAPT::DFTSAPT()
{
    common_init();
}
DFTSAPT::~DFTSAPT()
{
}
void DFTSAPT::common_init()
{
    print_ = 1;
    debug_ = 0;
    bench_ = 0;
}
boost::shared_ptr<DFTSAPT> DFTSAPT::build(boost::shared_ptr<Wavefunction> d,
                                          boost::shared_ptr<Wavefunction> mA, 
                                          boost::shared_ptr<Wavefunction> mB)
{
    DFTSAPT* sapt = new DFTSAPT();

    Options& options = Process::environment.options; 

    sapt->print_ = options.get_int("PRINT");
    sapt->debug_ = options.get_int("DEBUG");
    sapt->bench_ = options.get_int("BENCH");

    sapt->memory_ = (unsigned long int)(Process::environment.get_memory() * options.get_double("SAPT_MEM_FACTOR") * 0.125);

    sapt->cpks_maxiter_ = options.get_int("MAXITER");
    sapt->cpks_delta_ = options.get_double("D_CONVERGENCE");

    sapt->dimer_     = d->molecule();
    sapt->monomer_A_ = mA->molecule();
    sapt->monomer_B_ = mB->molecule();

    sapt->E_dimer_     = d->reference_energy();
    sapt->E_monomer_A_ = mA->reference_energy();
    sapt->E_monomer_B_ = mB->reference_energy();

    sapt->primary_   = d->basisset();
    sapt->primary_A_ = mA->basisset();
    sapt->primary_B_ = mB->basisset();

    if (sapt->primary_A_->nbf() != sapt->primary_B_->nbf() || sapt->primary_->nbf() != sapt->primary_A_->nbf()) {
        throw PSIEXCEPTION("Monomer-centered bases not allowed in DFT-SAPT");
    }

    sapt->Cocc_A_     = mA->Ca_subset("AO","OCC");

    sapt->Caocc_A_    = mA->Ca_subset("AO","ACTIVE_OCC");
    sapt->Cavir_A_    = mA->Ca_subset("AO","ACTIVE_VIR");

    sapt->eps_focc_A_ = mA->epsilon_a_subset("AO","FROZEN_OCC");
    sapt->eps_aocc_A_ = mA->epsilon_a_subset("AO","ACTIVE_OCC");
    sapt->eps_avir_A_ = mA->epsilon_a_subset("AO","ACTIVE_VIR");
    sapt->eps_fvir_A_ = mA->epsilon_a_subset("AO","FROZEN_VIR");

    sapt->Cocc_B_     = mB->Ca_subset("AO","OCC");

    sapt->Caocc_B_    = mB->Ca_subset("AO","ACTIVE_OCC");
    sapt->Cavir_B_    = mB->Ca_subset("AO","ACTIVE_VIR");

    sapt->eps_focc_B_ = mB->epsilon_a_subset("AO","FROZEN_OCC");
    sapt->eps_aocc_B_ = mB->epsilon_a_subset("AO","ACTIVE_OCC");
    sapt->eps_avir_B_ = mB->epsilon_a_subset("AO","ACTIVE_VIR");
    sapt->eps_fvir_B_ = mB->epsilon_a_subset("AO","FROZEN_VIR");

    boost::shared_ptr<BasisSetParser> parser(new Gaussian94BasisSetParser());
    sapt->mp2fit_ = BasisSet::construct(parser, sapt->dimer_, "DF_BASIS_SAPT");

    return boost::shared_ptr<DFTSAPT>(sapt);
}
double DFTSAPT::compute_energy()
{
    print_header();

    fock_terms();
    
    mp2_terms();

    print_trailer();
    
    Process::environment.globals["SAPT ENERGY"] = 0.0;

    return 0.0;
}
void DFTSAPT::print_header() const 
{
    fprintf(outfile, "\t --------------------------------------------------------\n");
    fprintf(outfile, "\t                        DF-DFT-SAPT                      \n");
    fprintf(outfile, "\t               Rob Parrish and Ed Hohenstein             \n");
    fprintf(outfile, "\t --------------------------------------------------------\n");
    fprintf(outfile, "\n");

    fprintf(outfile, "  ==> Sizes <==\n");
    fprintf(outfile, "\n");

    fprintf(outfile, "   => Resources <=\n\n");
    
    fprintf(outfile, "    Memory (MB):       %11ld\n", (memory_ *8L) / (1024L * 1024L));
    fprintf(outfile, "\n");

    fprintf(outfile, "   => Orbital Ranges <=\n\n");

    int nmo_A = eps_focc_A_->dim() + eps_aocc_A_->dim() + eps_avir_A_->dim() + eps_fvir_A_->dim();
    int nmo_B = eps_focc_B_->dim() + eps_aocc_B_->dim() + eps_avir_B_->dim() + eps_fvir_B_->dim();

    fprintf(outfile, "    ------------------\n");    
    fprintf(outfile, "    %-6s %5s %5s\n", "Range", "M_A", "M_B");
    fprintf(outfile, "    ------------------\n");    
    fprintf(outfile, "    %-6s %5d %5d\n", "nso", primary_A_->nbf(), primary_B_->nbf());
    fprintf(outfile, "    %-6s %5d %5d\n", "nmo", nmo_A, nmo_B);
    fprintf(outfile, "    %-6s %5d %5d\n", "nocc", eps_aocc_A_->dim() + eps_focc_A_->dim(), eps_aocc_B_->dim() + eps_focc_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "nvir", eps_avir_A_->dim() + eps_fvir_A_->dim(), eps_avir_B_->dim() + eps_fvir_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "nfocc", eps_focc_A_->dim(), eps_focc_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "naocc", eps_aocc_A_->dim(), eps_aocc_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "navir", eps_avir_A_->dim(), eps_avir_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "nfvir", eps_fvir_A_->dim(), eps_fvir_B_->dim());
    fprintf(outfile, "    ------------------\n");    
    fprintf(outfile, "\n");

    fprintf(outfile, "   => Primary Basis Set <=\n\n");
    primary_->print_by_level(outfile, print_);

    fflush(outfile);
}
void DFTSAPT::fock_terms()
{
    fprintf(outfile, "  SCF TERMS:\n\n");

    // ==> Setup <== //    

    boost::shared_ptr<JK> jk = JK::build_JK();
    // TODO: Account for 2-index overhead in memory
    jk->set_memory(memory_);
    jk->set_do_J(true);
    jk->set_do_K(true);
    jk->initialize();
    jk->print_header();

    // ==> Generalized Fock Source Terms [Elst/Exch] <== //

    // => Steric Interaction Density Terms (T) <= //

    boost::shared_ptr<Matrix> S = build_S(primary_);
    boost::shared_ptr<Matrix> Sij = build_Sij(S);
    boost::shared_ptr<Matrix> Sij_2 = build_Sij_2(Sij);
    boost::shared_ptr<Matrix> Sij_n = build_Sij_n(Sij);

    std::map<std::string, boost::shared_ptr<Matrix> > Cbar_2 = build_Cbar(Sij_2);
    boost::shared_ptr<Matrix> C_T_A_2  = Cbar_2["C_T_A"];
    boost::shared_ptr<Matrix> C_T_B_2  = Cbar_2["C_T_B"];
    boost::shared_ptr<Matrix> C_T_AB_2 = Cbar_2["C_T_AB"];
    boost::shared_ptr<Matrix> C_T_BA_2 = Cbar_2["C_T_BA"];

    std::map<std::string, boost::shared_ptr<Matrix> > Cbar_n = build_Cbar(Sij_n);
    boost::shared_ptr<Matrix> C_T_A_n  = Cbar_n["C_T_A"];
    boost::shared_ptr<Matrix> C_T_B_n  = Cbar_n["C_T_B"];
    boost::shared_ptr<Matrix> C_T_AB_n = Cbar_n["C_T_AB"];
    boost::shared_ptr<Matrix> C_T_BA_n = Cbar_n["C_T_BA"];

    if (debug_ > 2) {
        S->print();
        Sij->print();
        Sij_2->print();
        Sij_n->print();
    }

    Sij.reset();
    Sij_2.reset();
    Sij_n.reset();

    // => Load the JK Object <= //
    
    std::vector<SharedMatrix>& Cl = jk->C_left();
    std::vector<SharedMatrix>& Cr = jk->C_right();
    Cl.clear();
    Cr.clear();

    // J/K[P^A]
    Cl.push_back(Cocc_A_);
    Cr.push_back(Cocc_A_);
    // J/K[T^A, S^2]
    Cl.push_back(Cocc_A_);
    Cr.push_back(C_T_A_2);
    // J/K[T^A, S^\infty]
    Cl.push_back(Cocc_A_);
    Cr.push_back(C_T_A_n);
    // J/K[P^B]
    Cl.push_back(Cocc_B_);
    Cr.push_back(Cocc_B_);
    // J/K[T^BA, S^2]
    Cl.push_back(Cocc_B_);
    Cr.push_back(C_T_BA_2);
    // J/K[T^BA, S^\infty]
    Cl.push_back(Cocc_B_);
    Cr.push_back(C_T_BA_n);
    
    // => Compute the JK matrices <= //

    jk->compute();

    // => Unload the JK Object <= //

    const std::vector<SharedMatrix>& J = jk->J();
    const std::vector<SharedMatrix>& K = jk->K();

    boost::shared_ptr<Matrix> J_A      = J[0]; 
    boost::shared_ptr<Matrix> J_T_A_2  = J[1]; 
    boost::shared_ptr<Matrix> J_T_A_n  = J[2]; 
    boost::shared_ptr<Matrix> J_B      = J[3]; 
    boost::shared_ptr<Matrix> J_T_BA_2 = J[4]; 
    boost::shared_ptr<Matrix> J_T_BA_n = J[5]; 

    boost::shared_ptr<Matrix> K_A      = K[0]; 
    boost::shared_ptr<Matrix> K_T_A_2  = K[1]; 
    boost::shared_ptr<Matrix> K_T_A_n  = K[2]; 
    boost::shared_ptr<Matrix> K_B      = K[3]; 
    boost::shared_ptr<Matrix> K_T_BA_2 = K[4]; 
    boost::shared_ptr<Matrix> K_T_BA_n = K[5]; 

    // => Compute the P matrices <= //
    
    boost::shared_ptr<Matrix> P_A    = build_D(Cocc_A_, Cocc_A_);
    boost::shared_ptr<Matrix> P_B    = build_D(Cocc_B_, Cocc_B_);

    // => Compute the V matrices <= //

    boost::shared_ptr<Matrix> V_A = build_V(primary_A_);
    boost::shared_ptr<Matrix> V_B = build_V(primary_B_);

    // => Scale to make Hesselmann happy <= //

    P_A->scale(2.0);
    P_B->scale(2.0);
    
    J_T_A_2->scale(0.5);
    J_T_A_n->scale(0.5);
    J_T_BA_2->scale(0.5);
    J_T_BA_n->scale(0.5);

    K_T_A_2->scale(0.5);
    K_T_A_n->scale(0.5);
    K_T_BA_2->scale(0.5);
    K_T_BA_n->scale(0.5);

    // ==> Electrostatic Terms <== //

    double Elst10 = 0.0;

    // Classical physics (watch for cancellation!)
    double Enuc = 0.0;
    Enuc += dimer_->nuclear_repulsion_energy();
    Enuc -= monomer_A_->nuclear_repulsion_energy();
    Enuc -= monomer_B_->nuclear_repulsion_energy();

    std::vector<double> Elst10_terms;
    Elst10_terms.resize(4);
    
    Elst10_terms[0] += 1.0 * P_A->vector_dot(V_B);
    Elst10_terms[1] += 1.0 * P_B->vector_dot(V_A);
    Elst10_terms[2] += 2.0 * P_B->vector_dot(J_A);
    Elst10_terms[3] += 1.0 * Enuc;
    
    for (int k = 0; k < Elst10_terms.size(); k++) {
        Elst10 += Elst10_terms[k];
    }

    if (debug_) {
        for (int k = 0; k < Elst10_terms.size(); k++) {
            fprintf(outfile,"    Elst10 (%1d)          = %18.12lf H\n",k+1,Elst10_terms[k]);
        }
    }

    energies_["Elst10"] = Elst10;

    fprintf(outfile,"    Elst10,r            = %18.12lf H\n",Elst10);
    fflush(outfile);

    // ==> Exchange Terms (S^\infty) <== //
    
    double Exch10_n = 0.0;

    // => Compute the T matrices <= //

    boost::shared_ptr<Matrix> T_A_n  = build_D(Cocc_A_, C_T_A_n);
    boost::shared_ptr<Matrix> T_B_n  = build_D(Cocc_B_, C_T_B_n);
    boost::shared_ptr<Matrix> T_BA_n = build_D(Cocc_B_, C_T_BA_n);
    boost::shared_ptr<Matrix> T_AB_n = build_D(Cocc_A_, C_T_AB_n);

    std::vector<double> Exch10_n_terms;
    Exch10_n_terms.resize(6);

    Exch10_n_terms[0] += 1.0 * P_A->vector_dot(K_B);
    
    Exch10_n_terms[1] += 2.0 * T_A_n->vector_dot(V_B);
    Exch10_n_terms[1] += 4.0 * T_A_n->vector_dot(J_B);
    Exch10_n_terms[1] -= 2.0 * T_A_n->vector_dot(K_B);

    Exch10_n_terms[2] += 2.0 * T_B_n->vector_dot(V_A);
    Exch10_n_terms[2] += 4.0 * T_B_n->vector_dot(J_A);
    Exch10_n_terms[2] -= 2.0 * T_B_n->vector_dot(K_A);

    Exch10_n_terms[3] += 4.0 * T_AB_n->vector_dot(V_A);
    Exch10_n_terms[3] += 8.0 * T_AB_n->vector_dot(J_A);
    Exch10_n_terms[3] -= 4.0 * T_AB_n->vector_dot(K_A);
    Exch10_n_terms[3] += 4.0 * T_AB_n->vector_dot(V_B);
    Exch10_n_terms[3] += 8.0 * T_AB_n->vector_dot(J_B);
    Exch10_n_terms[3] -= 4.0 * T_AB_n->vector_dot(K_B);

    Exch10_n_terms[4] += 8.0 * T_AB_n->vector_dot(J_T_A_n);
    Exch10_n_terms[4] -= 4.0 * T_AB_n->vector_dot(K_T_A_n);
    Exch10_n_terms[4] += 8.0 * T_B_n->vector_dot(J_T_A_n);
    Exch10_n_terms[4] -= 4.0 * T_B_n->vector_dot(K_T_A_n);
    
    Exch10_n_terms[5] += 8.0 * T_BA_n->vector_dot(J_T_BA_n);
    Exch10_n_terms[5] -= 4.0 * T_BA_n->vector_dot(K_T_BA_n);
    Exch10_n_terms[5] += 8.0 * T_B_n->vector_dot(J_T_BA_n);
    Exch10_n_terms[5] -= 4.0 * T_B_n->vector_dot(K_T_BA_n);

    for (int k = 0; k < Exch10_n_terms.size(); k++) {
        Exch10_n_terms[k] *= -1.0;
        Exch10_n += Exch10_n_terms[k];
    }

    if (debug_) {
        for (int k = 0; k < Exch10_n_terms.size(); k++) {
            fprintf(outfile,"    Exch10 (%1d)          = %18.12lf H\n",k+1,Exch10_n_terms[k]);
        }
    }

    energies_["Exch10"] = Exch10_n;

    fprintf(outfile,"    Exch10              = %18.12lf H\n",Exch10_n);
    fflush(outfile);

    // Clear up some memory
    C_T_A_n.reset();
    C_T_B_n.reset();
    C_T_BA_n.reset();
    C_T_AB_n.reset();
    T_A_n.reset();
    T_B_n.reset();
    T_BA_n.reset();
    T_AB_n.reset();
    J_T_A_n.reset();
    J_T_BA_n.reset();
    K_T_A_n.reset();
    K_T_BA_n.reset();

    // ==> Exchange Terms (S^2) <== //
    
    double Exch10_2 = 0.0;

    // => Compute the T matrices <= //

    boost::shared_ptr<Matrix> T_A_2  = build_D(Cocc_A_, C_T_A_2);
    boost::shared_ptr<Matrix> T_B_2  = build_D(Cocc_B_, C_T_B_2);
    boost::shared_ptr<Matrix> T_BA_2 = build_D(Cocc_B_, C_T_BA_2);
    boost::shared_ptr<Matrix> T_AB_2 = build_D(Cocc_A_, C_T_AB_2);

    std::vector<double> Exch10_2_terms;
    Exch10_2_terms.resize(6);

    Exch10_2_terms[0] += 1.0 * P_A->vector_dot(K_B);
    
    Exch10_2_terms[1] += 2.0 * T_A_2->vector_dot(V_B);
    Exch10_2_terms[1] += 4.0 * T_A_2->vector_dot(J_B);
    Exch10_2_terms[1] -= 2.0 * T_A_2->vector_dot(K_B);

    Exch10_2_terms[2] += 2.0 * T_B_2->vector_dot(V_A);
    Exch10_2_terms[2] += 4.0 * T_B_2->vector_dot(J_A);
    Exch10_2_terms[2] -= 2.0 * T_B_2->vector_dot(K_A);

    Exch10_2_terms[3] += 4.0 * T_AB_2->vector_dot(V_A);
    Exch10_2_terms[3] += 8.0 * T_AB_2->vector_dot(J_A);
    Exch10_2_terms[3] -= 4.0 * T_AB_2->vector_dot(K_A);
    Exch10_2_terms[3] += 4.0 * T_AB_2->vector_dot(V_B);
    Exch10_2_terms[3] += 8.0 * T_AB_2->vector_dot(J_B);
    Exch10_2_terms[3] -= 4.0 * T_AB_2->vector_dot(K_B);

    Exch10_2_terms[4] += 8.0 * T_AB_2->vector_dot(J_T_A_2);
    Exch10_2_terms[4] -= 4.0 * T_AB_2->vector_dot(K_T_A_2);
    Exch10_2_terms[4] += 8.0 * T_B_2->vector_dot(J_T_A_2);
    Exch10_2_terms[4] -= 4.0 * T_B_2->vector_dot(K_T_A_2);
    
    Exch10_2_terms[5] += 8.0 * T_BA_2->vector_dot(J_T_BA_2);
    Exch10_2_terms[5] -= 4.0 * T_BA_2->vector_dot(K_T_BA_2);
    Exch10_2_terms[5] += 8.0 * T_B_2->vector_dot(J_T_BA_2);
    Exch10_2_terms[5] -= 4.0 * T_B_2->vector_dot(K_T_BA_2);

    for (int k = 0; k < Exch10_2_terms.size(); k++) {
        Exch10_2_terms[k] *= -1.0;
        Exch10_2 += Exch10_2_terms[k];
    }

    if (debug_) {
        for (int k = 0; k < Exch10_2_terms.size(); k++) {
            fprintf(outfile,"    Exch10 (S^2) (%1d)    = %18.12lf H\n",k+1,Exch10_2_terms[k]);
        }
    }

    energies_["Exch10 (S^2)"] = Exch10_2;

    fprintf(outfile,"    Exch10 (S^2)        = %18.12lf H\n",Exch10_2);
    fflush(outfile);

    // Clear up some memory
    C_T_A_2.reset();
    C_T_B_2.reset();
    C_T_BA_2.reset();
    C_T_AB_2.reset();
    T_A_2.reset();
    T_B_2.reset();
    T_BA_2.reset();
    T_AB_2.reset();
    J_T_A_2.reset();
    J_T_BA_2.reset();
    K_T_A_2.reset();
    K_T_BA_2.reset();

    fprintf(outfile, "\n");
    fflush(outfile);

    // ==> Uncorrelated Second-Order Response Terms [Induction] <== //

    // Electrostatic potential of monomer A (AO)
    boost::shared_ptr<Matrix> W_A(V_A->clone());
    W_A->copy(J_A);
    W_A->scale(2.0);
    W_A->add(V_A);

    // Electrostatic potential of monomer B (AO)
    boost::shared_ptr<Matrix> W_B(V_B->clone());
    W_B->copy(J_B);
    W_B->scale(2.0);
    W_B->add(V_B);

    // Electrostatic potential of monomer B (ov space of A)
    boost::shared_ptr<Matrix> w_B = build_w(W_B,Caocc_A_,Cavir_A_);
    // Electrostatic potential of monomer A (ov space of B)
    boost::shared_ptr<Matrix> w_A = build_w(W_A,Caocc_B_,Cavir_B_);

    // Compute CPHF
    std::pair<boost::shared_ptr<Matrix>, boost::shared_ptr<Matrix> > x_sol = compute_x(jk,w_B,w_A);
    boost::shared_ptr<Matrix> x_A = x_sol.first;
    boost::shared_ptr<Matrix> x_B = x_sol.second;

    // ==> Induction <== //

    double Ind20r_AB = - x_A->vector_dot(w_B);
    double Ind20r_BA = - x_B->vector_dot(w_A);
    double Ind20r = Ind20r_AB + Ind20r_BA;

    energies_["Ind20,r (A<-B)"] = Ind20r_AB;
    energies_["Ind20,r (B->A)"] = Ind20r_BA;
    energies_["Ind20,r"] = Ind20r_AB + Ind20r_BA;

    fprintf(outfile,"    Ind20 (A<-B)        = %18.12lf H\n",Ind20r_AB);
    fprintf(outfile,"    Ind20 (A->B)        = %18.12lf H\n",Ind20r_BA);
    fprintf(outfile,"    Ind20               = %18.12lf H\n",Ind20r);
    fflush(outfile);

    // ==> Exchange-Induction <== //

    // => AO-basis response <= //    

    // Response of monomber A (AO)
    boost::shared_ptr<Matrix> X_A = build_X(x_A,Caocc_A_,Cavir_A_);
    // Response of monomer B (AO)
    boost::shared_ptr<Matrix> X_B = build_X(x_B,Caocc_B_,Cavir_B_);

    // => O cross densities <= //

    //boost::shared_ptr<Matrix> O_AB = build_triple(P_A,S,P_B);
    //boost::shared_ptr<Matrix> O_BA = build_triple(P_B,S,P_A);

    // TODO Exhange induction
}
void DFTSAPT::mp2_terms()
{
    fprintf(outfile, "  PT2 TERMS:\n\n");

    // TODO 
}
void DFTSAPT::print_trailer() const 
{
    fprintf(outfile, "\tOh all the money that e'er I had, I spent it in good company.\n");
    fprintf(outfile, "\n");
}
boost::shared_ptr<Matrix> DFTSAPT::build_S(boost::shared_ptr<BasisSet> basis)
{
    boost::shared_ptr<IntegralFactory> factory(new IntegralFactory(basis));
    boost::shared_ptr<OneBodyAOInt> Sint(factory->ao_overlap());
    boost::shared_ptr<Matrix> S(new Matrix("S (AO)", basis->nbf(), basis->nbf()));
    Sint->compute(S);
    return S;
}
boost::shared_ptr<Matrix> DFTSAPT::build_V(boost::shared_ptr<BasisSet> basis)
{
    boost::shared_ptr<IntegralFactory> factory(new IntegralFactory(basis));
    boost::shared_ptr<OneBodyAOInt> Sint(factory->ao_potential());
    boost::shared_ptr<Matrix> S(new Matrix("V (AO)", basis->nbf(), basis->nbf()));
    Sint->compute(S);
    return S;
}
boost::shared_ptr<Matrix> DFTSAPT::build_Sij(boost::shared_ptr<Matrix> S)
{
    int nso = Cocc_A_->nrow();
    int nocc_A = Cocc_A_->ncol();
    int nocc_B = Cocc_B_->ncol();
    int nocc = nocc_A + nocc_B;

    boost::shared_ptr<Matrix> Sij(new Matrix("Sij (MO)", nocc, nocc));
    boost::shared_ptr<Matrix> T(new Matrix("T", nso, nocc_B));
    
    double** Sp = S->pointer();
    double** Tp = T->pointer();
    double** Sijp = Sij->pointer();
    double** CAp = Cocc_A_->pointer();
    double** CBp = Cocc_B_->pointer();

    C_DGEMM('N','N',nso,nocc_B,nso,1.0,Sp[0],nso,CBp[0],nocc_B,0.0,Tp[0],nocc_B);
    C_DGEMM('T','N',nocc_A,nocc_B,nso,1.0,CAp[0],nocc_A,Tp[0],nocc_B,0.0,&Sijp[0][nocc_A],nocc);

    Sij->copy_upper_to_lower();

    return Sij;
}
boost::shared_ptr<Matrix> DFTSAPT::build_Sij_2(boost::shared_ptr<Matrix> Sij)
{
    int nocc = Sij->nrow();

    boost::shared_ptr<Matrix> Sij2(new Matrix("Sij2 (MO)", nocc, nocc));

    double** Sijp = Sij->pointer();
    double** Sij2p = Sij2->pointer();

    Sij2->subtract(Sij);
    C_DGEMM('N','N',nocc,nocc,nocc,1.0,Sijp[0],nocc,Sijp[0],nocc,1.0,Sij2p[0],nocc);

    return Sij2;
}
boost::shared_ptr<Matrix> DFTSAPT::build_Sij_n(boost::shared_ptr<Matrix> Sij)
{
    int nocc = Sij->nrow();

    boost::shared_ptr<Matrix> Sij2(new Matrix("Sij^inf (MO)", nocc, nocc));

    double** Sijp = Sij->pointer();
    double** Sij2p = Sij2->pointer();

    Sij2->copy(Sij);
    for (int i = 0; i < nocc; i++) {
        Sij2p[i][i] = 1.0;
    }

    int info;
   
    info = C_DPOTRF('L',nocc,Sij2p[0],nocc); 
    if (info) {
        throw PSIEXCEPTION("Sij DPOTRF failed. How far up the steric wall are you?");
    }

    info = C_DPOTRI('L',nocc,Sij2p[0],nocc); 
    if (info) {
        throw PSIEXCEPTION("Sij DPOTRI failed. How far up the steric wall are you?");
    }

    Sij2->copy_upper_to_lower();

    for (int i = 0; i < nocc; i++) {
        Sij2p[i][i] -= 1.0;
    }
   
    return Sij2; 
}
std::map<std::string, boost::shared_ptr<Matrix> > DFTSAPT::build_Cbar(boost::shared_ptr<Matrix> S)
{
    std::map<std::string, boost::shared_ptr<Matrix> > Cbar;

    int nso = Cocc_A_->nrow();
    int nA = Cocc_A_->ncol();
    int nB = Cocc_B_->ncol();
    int no = nA + nB;

    double** Sp = S->pointer();
    double** CAp = Cocc_A_->pointer();
    double** CBp = Cocc_B_->pointer();
    double** Cp;

    Cbar["C_T_A"] = boost::shared_ptr<Matrix>(new Matrix("C_T_A", nso, nA));
    Cp = Cbar["C_T_A"]->pointer();
    C_DGEMM('N','N',nso,nA,nA,1.0,CAp[0],nA,&Sp[0][0],no,0.0,Cp[0],nA);

    Cbar["C_T_B"] = boost::shared_ptr<Matrix>(new Matrix("C_T_B", nso, nB));
    Cp = Cbar["C_T_B"]->pointer();
    C_DGEMM('N','N',nso,nB,nB,1.0,CBp[0],nB,&Sp[nA][nA],no,0.0,Cp[0],nB);

    Cbar["C_T_BA"] = boost::shared_ptr<Matrix>(new Matrix("C_T_BA", nso, nB));   
    Cp = Cbar["C_T_BA"]->pointer();
    C_DGEMM('N','N',nso,nB,nA,1.0,CAp[0],nA,&Sp[0][nA],no,0.0,Cp[0],nB);

    Cbar["C_T_AB"] = boost::shared_ptr<Matrix>(new Matrix("C_T_AB", nso, nA));   
    Cp = Cbar["C_T_AB"]->pointer();
    C_DGEMM('N','N',nso,nA,nB,1.0,CBp[0],nB,&Sp[nB][0],no,0.0,Cp[0],nA);

    return Cbar;
}
boost::shared_ptr<Matrix> DFTSAPT::build_D(boost::shared_ptr<Matrix> L, boost::shared_ptr<Matrix> R)
{
    int nso = L->nrow();
    int nocc = L->ncol();

    boost::shared_ptr<Matrix> D(new Matrix("D", nso, nso));
    
    double** Dp = D->pointer();
    double** Lp = L->pointer();
    double** Rp = R->pointer();

    C_DGEMM('N','T',nso,nso,nocc,1.0,Lp[0],nocc,Rp[0],nocc,0.0,Dp[0],nso);

    return D;
}
boost::shared_ptr<Matrix> DFTSAPT::build_w(boost::shared_ptr<Matrix> W, boost::shared_ptr<Matrix> L, boost::shared_ptr<Matrix> R)
{
    int nso = L->nrow();
    int no = L->ncol();
    int nv = R->ncol();

    boost::shared_ptr<Matrix> w(new Matrix("w", no, nv));
    boost::shared_ptr<Matrix> T(new Matrix("T", no, nso));
    
    double** wp = w->pointer();
    double** Wp = W->pointer();
    double** Lp = L->pointer();
    double** Rp = R->pointer();
    double** Tp = T->pointer();

    C_DGEMM('T','N',no,nso,nso,1.0,Lp[0],no,Wp[0],nso,0.0,Tp[0],nso);
    C_DGEMM('N','N',no,nv,nso,1.0,Tp[0],nso,Rp[0],nv,0.0,wp[0],nv);

    return w;
}
boost::shared_ptr<Matrix> DFTSAPT::build_X(boost::shared_ptr<Matrix> x, boost::shared_ptr<Matrix> L, boost::shared_ptr<Matrix> R)
{
    int nso = L->nrow();
    int no = L->ncol();
    int nv = R->ncol();

    boost::shared_ptr<Matrix> X(new Matrix("w", nso, nso));
    boost::shared_ptr<Matrix> T(new Matrix("T", no, nso));
    
    double** xp = x->pointer();
    double** Xp = X->pointer();
    double** Lp = L->pointer();
    double** Rp = R->pointer();
    double** Tp = T->pointer();
    
    C_DGEMM('N','T',no,nso,nv,1.0,xp[0],nv,Rp[0],nv,0.0,Tp[0],nso);
    C_DGEMM('N','N',nso,nso,no,1.0,Lp[0],no,Tp[0],nso,0.0,Xp[0],nso);

    return x;
}
std::pair<boost::shared_ptr<Matrix>, boost::shared_ptr<Matrix> > DFTSAPT::compute_x(boost::shared_ptr<JK> jk, boost::shared_ptr<Matrix> w_B, boost::shared_ptr<Matrix> w_A)
{
    boost::shared_ptr<CPKS_SAPT> cpks(new CPKS_SAPT);
   
    // Effective constructor 
    cpks->delta_ = cpks_delta_;
    cpks->maxiter_ = cpks_maxiter_;
    cpks->jk_ = jk;

    cpks->w_A_ = w_B; // Reversal of convention
    cpks->Caocc_A_ = Caocc_A_;
    cpks->Cavir_A_ = Cavir_A_;
    cpks->eps_aocc_A_ = eps_aocc_A_;
    cpks->eps_avir_A_ = eps_avir_A_;

    cpks->w_B_ = w_A; // Reversal of convention
    cpks->Caocc_B_ = Caocc_B_;
    cpks->Cavir_B_ = Cavir_B_;
    cpks->eps_aocc_B_ = eps_aocc_B_;
    cpks->eps_avir_B_ = eps_avir_B_;

    // Gogo CPKS
    cpks->compute_cpks();

    // Unpack
    std::pair<boost::shared_ptr<Matrix>, boost::shared_ptr<Matrix> > x_sol = make_pair(cpks->x_A_,cpks->x_B_);

    // Scale up for electron pairs
    x_sol.first->scale(2.0);
    x_sol.second->scale(2.0);

    return x_sol;
}

CPKS_SAPT::CPKS_SAPT()
{
}
CPKS_SAPT::~CPKS_SAPT()
{
}
void CPKS_SAPT::compute_cpks()
{
    // Allocate
    x_A_ = boost::shared_ptr<Matrix>(w_A_->clone());
    x_B_ = boost::shared_ptr<Matrix>(w_B_->clone());
    x_A_->zero();
    x_B_->zero();

    boost::shared_ptr<Matrix> r_A(w_A_->clone());
    boost::shared_ptr<Matrix> z_A(w_A_->clone());
    boost::shared_ptr<Matrix> p_A(w_A_->clone());
    boost::shared_ptr<Matrix> r_B(w_B_->clone());
    boost::shared_ptr<Matrix> z_B(w_B_->clone());
    boost::shared_ptr<Matrix> p_B(w_B_->clone());
        
    // Initialize (x_0 = 0)
    r_A->copy(w_A_);
    r_B->copy(w_B_);
    preconditioner(r_A,z_A,eps_aocc_A_,eps_avir_A_);
    preconditioner(r_B,z_B,eps_aocc_B_,eps_avir_B_);
    p_A->copy(z_A);
    p_B->copy(z_B);

    double zr_old_A = z_A->vector_dot(r_A);
    double zr_old_B = z_B->vector_dot(r_B);

    double r2A = 1.0;
    double r2B = 1.0;

    double b2A = sqrt(w_A_->vector_dot(w_A_));
    double b2B = sqrt(w_B_->vector_dot(w_B_));

    fprintf(outfile, "  ==> CPKS Iterations <==\n\n");

    fprintf(outfile, "    Maxiter     = %11d\n", maxiter_);
    fprintf(outfile, "    Convergence = %11.3E\n", delta_);
    fprintf(outfile, "\n");

    fprintf(outfile, "    ------------------------------\n");
    fprintf(outfile, "    %4s %11s  %11s \n", "Iter", "Monomer A", "Monomer B");
    fprintf(outfile, "    ------------------------------\n");
    fflush(outfile);

    int iter;
    for (iter = 0; iter < maxiter_; iter++) {
       
        std::map<std::string, boost::shared_ptr<Matrix> > b;
        if (r2A > delta_) {
            b["A"] = p_A;
        } 
        if (r2B > delta_) {
            b["B"] = p_B;
        } 
    
        std::map<std::string, boost::shared_ptr<Matrix> > s =
            product(b);
    
        if (r2A > delta_) {
            boost::shared_ptr<Matrix> s_A = s["A"];
            double alpha = r_A->vector_dot(z_A) / p_A->vector_dot(s_A);
            int no = x_A_->nrow();
            int nv = x_A_->ncol();
            double** xp = x_A_->pointer();
            double** rp = r_A->pointer();
            double** pp = p_A->pointer();
            double** sp = s_A->pointer();
            C_DAXPY(no*nv, alpha,pp[0],1,xp[0],1);
            C_DAXPY(no*nv,-alpha,sp[0],1,rp[0],1); 
            r2A = sqrt(C_DDOT(no*nv,rp[0],1,rp[0],1)) / b2A; 
        } 
    
        if (r2B > delta_) {
            boost::shared_ptr<Matrix> s_B = s["B"];
            double alpha = r_B->vector_dot(z_B) / p_B->vector_dot(s_B);
            int no = x_B_->nrow();
            int nv = x_B_->ncol();
            double** xp = x_B_->pointer();
            double** rp = r_B->pointer();
            double** pp = p_B->pointer();
            double** sp = s_B->pointer();
            C_DAXPY(no*nv, alpha,pp[0],1,xp[0],1);
            C_DAXPY(no*nv,-alpha,sp[0],1,rp[0],1); 
            r2B = sqrt(C_DDOT(no*nv,rp[0],1,rp[0],1)) / b2B; 
        } 

        fprintf(outfile, "    %4d %11.3E%1s %11.3E%1s\n", iter+1,
            r2A, (r2A < delta_ ? "*" : " "), 
            r2B, (r2B < delta_ ? "*" : " ")
            );
        fflush(outfile);

        if (r2A <= delta_ && r2B <= delta_) {
            break;
        }
        
        if (r2A > delta_) {
            preconditioner(r_A,z_A,eps_aocc_A_,eps_avir_A_);
            double zr_new = z_A->vector_dot(r_A);
            double beta = zr_new / zr_old_A;
            zr_old_A = zr_new;
            int no = x_A_->nrow();
            int nv = x_A_->ncol();
            double** pp = p_A->pointer();
            double** zp = z_A->pointer();
            C_DSCAL(no*nv,beta,pp[0],1);
            C_DAXPY(no*nv,1.0,zp[0],1,pp[0],1);
        } 
        
        if (r2B > delta_) {
            preconditioner(r_B,z_B,eps_aocc_B_,eps_avir_B_);
            double zr_new = z_B->vector_dot(r_B);
            double beta = zr_new / zr_old_B;
            zr_old_B = zr_new;
            int no = x_B_->nrow();
            int nv = x_B_->ncol();
            double** pp = p_B->pointer();
            double** zp = z_B->pointer();
            C_DSCAL(no*nv,beta,pp[0],1);
            C_DAXPY(no*nv,1.0,zp[0],1,pp[0],1);
        } 
    }
    
    fprintf(outfile, "    ------------------------------\n");
    fprintf(outfile, "\n");
    fflush(outfile);

    if (iter == maxiter_) 
        throw PSIEXCEPTION("CPKS did not converge.");
}
void CPKS_SAPT::preconditioner(boost::shared_ptr<Matrix> r,
                               boost::shared_ptr<Matrix> z,
                               boost::shared_ptr<Vector> o,
                               boost::shared_ptr<Vector> v)
{
    int no = o->dim();
    int nv = v->dim();
    
    double** rp = r->pointer();
    double** zp = z->pointer();

    double* op = o->pointer();
    double* vp = v->pointer();

    for (int i = 0; i < no; i++) {
        for (int a = 0; a < nv; a++) {
            zp[i][a] = rp[i][a] / (vp[a] - op[i]);
        }
    }
}
std::map<std::string, boost::shared_ptr<Matrix> > CPKS_SAPT::product(std::map<std::string, boost::shared_ptr<Matrix> > b)
{
    std::map<std::string, boost::shared_ptr<Matrix> > s;

    bool do_A = b.count("A");
    bool do_B = b.count("B");

    std::vector<SharedMatrix>& Cl = jk_->C_left();
    std::vector<SharedMatrix>& Cr = jk_->C_right();
    Cl.clear();
    Cr.clear();

    if (do_A) {
        Cl.push_back(Caocc_A_);    
        int no = b["A"]->nrow();
        int nv = b["A"]->ncol();
        int nso = Cavir_A_->nrow();
        double** Cp = Cavir_A_->pointer();
        double** bp = b["A"]->pointer();
        boost::shared_ptr<Matrix> T(new Matrix("T",nso,no));
        double** Tp = T->pointer();
        C_DGEMM('N','T',nso,no,nv,1.0,Cp[0],nv,bp[0],nv,0.0,Tp[0],no);
        Cr.push_back(T);
    }

    if (do_B) {
        Cl.push_back(Caocc_B_);    
        int no = b["B"]->nrow();
        int nv = b["B"]->ncol();
        int nso = Cavir_B_->nrow();
        double** Cp = Cavir_B_->pointer();
        double** bp = b["B"]->pointer();
        boost::shared_ptr<Matrix> T(new Matrix("T",nso,no));
        double** Tp = T->pointer();
        C_DGEMM('N','T',nso,no,nv,1.0,Cp[0],nv,bp[0],nv,0.0,Tp[0],no);
        Cr.push_back(T);
    }

    jk_->compute();
    
    const std::vector<SharedMatrix>& J = jk_->J();
    const std::vector<SharedMatrix>& K = jk_->K();

    int indA = 0;
    int indB = (do_A ? 1 : 0);

    if (do_A) {
        boost::shared_ptr<Matrix> Jv = J[indA];
        boost::shared_ptr<Matrix> Kv = K[indA];
        Jv->scale(4.0);
        Jv->subtract(Kv);
        Jv->subtract(Kv->transpose());
        
        int no = b["A"]->nrow();
        int nv = b["A"]->ncol();
        int nso = Cavir_A_->nrow();
        boost::shared_ptr<Matrix> T(new Matrix("T", no, nso));
        s["A"] = boost::shared_ptr<Matrix>(new Matrix("S", no, nv));
        double** Cop = Caocc_A_->pointer();
        double** Cvp = Cavir_A_->pointer();
        double** Jp = Jv->pointer();
        double** Tp = T->pointer();
        double** Sp = s["A"]->pointer();
        C_DGEMM('T','N',no,nso,nso,1.0,Cop[0],no,Jp[0],nso,0.0,Tp[0],nso);
        C_DGEMM('N','N',no,nv,nso,1.0,Tp[0],nso,Cvp[0],nv,0.0,Sp[0],nv); 

        double** bp = b["A"]->pointer();
        double* op = eps_aocc_A_->pointer();
        double* vp = eps_avir_A_->pointer();
        for (int i = 0; i < no; i++) {
            for (int a = 0; a < nv; a++) {
                Sp[i][a] += bp[i][a] * (vp[a] - op[i]);
            }
        }
    } 

    if (do_B) {
        boost::shared_ptr<Matrix> Jv = J[indB];
        boost::shared_ptr<Matrix> Kv = K[indB];
        Jv->scale(4.0);
        Jv->subtract(Kv);
        Jv->subtract(Kv->transpose());
        
        int no = b["B"]->nrow();
        int nv = b["B"]->ncol();
        int nso = Cavir_B_->nrow();
        boost::shared_ptr<Matrix> T(new Matrix("T", no, nso));
        s["B"] = boost::shared_ptr<Matrix>(new Matrix("S", no, nv));
        double** Cop = Caocc_B_->pointer();
        double** Cvp = Cavir_B_->pointer();
        double** Jp = Jv->pointer();
        double** Tp = T->pointer();
        double** Sp = s["B"]->pointer();
        C_DGEMM('T','N',no,nso,nso,1.0,Cop[0],no,Jp[0],nso,0.0,Tp[0],nso);
        C_DGEMM('N','N',no,nv,nso,1.0,Tp[0],nso,Cvp[0],nv,0.0,Sp[0],nv); 

        double** bp = b["B"]->pointer();
        double* op = eps_aocc_B_->pointer();
        double* vp = eps_avir_B_->pointer();
        for (int i = 0; i < no; i++) {
            for (int a = 0; a < nv; a++) {
                Sp[i][a] += bp[i][a] * (vp[a] - op[i]);
            }
        }
    } 

    return s;
}

}}
