// Time tracking
#include "io/tictoc.hpp"

// Solvers
#include "solvers/Solver_spectral.hpp"
#include "solvers/Solver_exact.hpp"

// Own header
#include "tests/error_spectral.hpp"

using namespace std;

namespace tests {

    void error_spectral(vector<double> x, Problem *problem, Analyser *analyser) {

        // Maximal degree of Hermite polynomials
        int degree_min = 5;
        int degree_max = 30;

        vector<int> degrees(degree_max - degree_min + 1);
        for (unsigned int i = 0; i < degrees.size(); ++i)
            degrees[i] = i + degree_min;

        // Update of the statistics of the invariant measure
        analyser->update_stats(x);

        // Computation of the exact solution
        Solver_exact solver_exact(problem, analyser);
        SDE_coeffs c_exact = solver_exact.estimator(x, 0.);
        vector<double> exact_drift = c_exact.drif;
        vector< vector<double> > exact_diff = c_exact.diff;

        vector<double> estimator_time(degrees.size());
        vector<double> estimator_error(degrees.size());

        // Files to write to
        ofstream out_degree("degree");
        ofstream out_time("time");
        ofstream out_errs("error");

        // Configuration for the spectral solver
        config_spectral conf_spectral; {
            conf_spectral.n_nodes = 100;
            conf_spectral.degree = degrees[degrees.size()-1];
            conf_spectral.scaling = vector<double> (problem->nf, problem->sigma);
        }

        // Create new solver
        Solver_spectral solver_spectral(problem, analyser, &conf_spectral);

        // Find full solution
        tic(); vector<SDE_coeffs> full_c = solver_spectral.full_estimator(x, 0.,degrees);

        for (unsigned int i = 0; i < degrees.size(); ++i) {

            // Error
            vector<double> Ddrif = (full_c[i].drif - exact_drift);
            vector< vector<double> > Ddiff = (full_c[i].diff - exact_diff);
            estimator_error[i] = fabs(Ddrif)/fabs(exact_drift) + fabs(Ddiff)/fabs(exact_diff);

            // Write to file
            out_degree << degrees[i] << endl;
            out_time << estimator_time[i] << endl;
            out_errs << estimator_error[i] << endl;

            // Summary of iteration
            cout << "> Degree" << degrees[i] << ". Error: " << estimator_error[i] << endl;
        }

        cout << "Time for full solution: " << toc() << endl << endl;
    }
}

int main(int argc, char* argv[]) {

    // Initialization of the problem and helper analyser
    Problem problem;
    problem.init();
    Analyser analyser(&problem);

    // Test error_spectral
    tests::error_spectral(problem.x0, &problem, &analyser);
}
