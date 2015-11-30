#include "tests/time_integration.hpp"

using namespace std;

namespace tests {

    void integrate(Problem *problem, Solver *solver, int seed, string s) {

        // Precision of the cout command
        cout.precision(6);
        cout << scientific;

        // Macro time-step
        double Dt = .01;
        int sizet = (int) (problem->t_end/Dt) + 1;

        // Vector of times of the macro-simulation
        vector<double> t(sizet,0.);
        for (unsigned int i = 0; i < sizet; i++) {
            t[i] = i*Dt;
        }

        // Random numbers generator
        default_random_engine generator; generator.seed(seed);
        normal_distribution<double> distribution(0.0,1.0);

        // Vector of random variables used to simulate the brownian
        // motion for the evolution of the slow variable.
        vector< vector<double> > dWs(sizet,vector<double>(problem->ns, 0.));
        for (unsigned int i = 0; i < sizet; i++) {
            for (int j = 0; j < problem->ns ; j++) {
                dWs[i][j] = distribution(generator);
            }
        }

        // Approximate and exact solutions
        vector< vector<double> > x(sizet,vector<double>(problem->ns,0.));

        // Initial condition
        x[0] = problem->x0;

        // Streams
        ofstream out_time("time_" + s);
        ofstream out_sol("sol_" + s);

        for (unsigned int i = 0; i < sizet - 1; i++) {

            struct SDE_coeffs c = solver->estimator(x[i], t[i]);
            x[i+1] = x[i];

            for (int i1 = 0; i1 < problem->ns; i1++) {
                for (int i2 = 0; i2 < problem->ns; i2++) {
                    x[i+1][i1] += c.diff[i1][i2]*sqrt(Dt)*dWs[i][i2];
                }
                x[i+1][i1] += Dt*c.drif[i1];
            }

            out_time << t[i] << endl;
            for (int j = 0; j < problem->ns; ++j) {
                out_sol << x[i][j];
                if (j != problem->ns -1) out_sol << " ";
                else out_sol << endl;
            }
        }
    }
}

int main(int argc, char* argv[]) {

    // Initialization of the problem and helper analyser
    Problem problem;
    problem.init();
    Analyser analyser(&problem);

    // Configuration for the HMM solver
    config_hmm conf_hmm = Solver_hmm::sensible_conf(4,1);

    // Configuration for the spectral solver
    config_spectral conf_spectral; {
        conf_spectral.n_nodes = 0;
        conf_spectral.degree = 15;
        conf_spectral.scaling = vector<double> (problem.nf, 0.5);
        conf_spectral.scaling = vector<double> (problem.nf, 1.);
    }

    // Initialization of the default solvers
    Solver_exact solver_exact(&problem, &analyser);
    Solver_spectral solver_spectral = Solver_spectral(&problem, &analyser, &conf_spectral);
    Solver_hmm solver_hmm = Solver_hmm(&problem, &conf_hmm);

    // Test of the solvers
    tests::integrate(&problem, &solver_exact, 0, "exact");
    tests::integrate(&problem, &solver_spectral, 0, "spectral");
    tests::integrate(&problem, &solver_hmm, 0, "hmm");
}
