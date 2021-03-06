/* TODO: Problem with exact sigma (urbain, Wed 20 May 2015 21:00:20 BST) */

#include "toolbox/linear_algebra.hpp"
#include "solvers/Solver_spectral.hpp"
#include "global/templates.hpp"
#include "io/io.hpp"
#include <iomanip>

using namespace std;

SDE_coeffs Solver_spectral::estimator(vec x, double t) {
    vector<int> degrees(1, conf->degree);
    vector<SDE_coeffs> full_result =  full_estimator(x,t,degrees);
    return full_result[0];
}

vector<SDE_coeffs> Solver_spectral::full_estimator(vec x, double t, vector<int> degrees) {

    // Update statistics
    analyser->update_stats(x);

    // Parameters of the problem
    int nf = problem->nf;
    int ns = problem->ns;

    // Update statistics of Gaussian
    this->update_stats();

    // Vector of functions to discretize
    int n_functions = ns + ns*ns + 1;
    vector<double (*) (vec, vec)> functions(n_functions);
    for (int i = 0; i < ns; i++)
        functions[i] = problem->a[i];
    for (int i = 0; i < ns; i++) {
        for (int j = 0; j < ns; j++) {
            functions[ns + i*ns + j] = problem->dxa[i][j];
        }
    }
    functions[ns + ns*ns] = problem->stardiv_h;

    // Discretization of functions in space
    mat functions_discretized_space(n_functions);
    for (int i = 0; i < n_functions; ++i)
        functions_discretized_space[i] = discretize(x, functions[i]);

    // Discretization of functions in hermite components
    mat functions_discretized_herm(n_functions);
    for (int i = 0; i < n_functions; ++i)
        functions_discretized_herm[i] = project_herm(nf, conf->degree, functions_discretized_space[i], 1);

    // Matrix of the linear system
    mat matrix = compute_matrix(x);

    // Solution of the Poisson equation
    vector<SDE_coeffs> result(degrees.size());

    for (unsigned int i = 0; i < degrees.size(); ++i) {

        int d = degrees[i];

        // Dimension of the space of polynomials of degree lower or equal to i.
        int n = bin(d + nf, nf);

        // Solutions obtained by using polynomials of degree up to i.
        mat sub_solutions(n_functions);
        mat sub_functions(n_functions);

        // Corresponding submatrix
        arma::mat sub_matrix = to_arma(matrix).submat(0,0, n-1,n-1);

        for (int j = 0; j < n_functions; ++j) {
            sub_functions[j] = to_std_vec(to_arma_vec(functions_discretized_herm[j]).subvec(0,n-1));
            sub_solutions[j] = to_std_vec(arma::solve(sub_matrix, to_arma_vec(sub_functions[j])));
        }

        result[i] = compute_averages(sub_functions, sub_solutions);
    }
    return result;
}

mat Solver_spectral::compute_matrix(vec x) {

    // Parameters of the problem
    int nf = problem->nf;
    int nb = bin(conf->degree + nf, nf);

    // Discretized difference of linear terms
    mat quad_points = gauss->nodes;
    vec quad_weights = gauss->weights;

    int ni = gauss->weights.size();
    vec diff_discretized(ni, 1.);

    for (int j = 0; j < ni; ++j) {

        vec z = quad_points[j];
        vec y = map_to_real(z);

        diff_discretized[j] = gaussian_linear_term(z) - problem->linearTerm(x,y);
        diff_discretized[j] *= quad_weights[j];
    }

    // Projection against monomials
    vec tmp_vec = project_mon(nf, 2*conf->degree, diff_discretized, 0);

    // Generating Galerkin matrix based on these
    mat prod_mat(nb, vec(nb, 0.));

    for (int i = 0; i < nb; ++i) {
        vector<int> m1 = ind2mult[i];
        for (int j = 0; j < nb; ++j) {
            vector<int> m2 = ind2mult[j];
            int index = mult2ind[m1 + m2];
            prod_mat[i][j] = tmp_vec[index];
        }
    }

    mat tmp_mat(nb, vec(nb, 0.));
    mat matrix(nb, vec(nb, 0.));
    for (int i = 0; i < nb; ++i) {
        for (int j = 0; j < nb; ++j) {
            for (int k = 0; k < nb; ++k) {
                tmp_mat[i][j] += hermiteCoeffs_nd[j][k]*prod_mat[i][k];
            }
        }
    }

    for (int i = 0; i < nb; ++i) {
        vector<int> m1 = ind2mult[i];

        for (int j = 0; j < nf; ++j) {
            matrix[i][i] += m1[j] / this->eig_val_cov[j] * (problem->s * problem->s) / 2;
        }
        for (int j = 0; j <= i; ++j) {
            for (int k = 0; k < nb; ++k) {
                matrix[i][j] += hermiteCoeffs_nd[i][k]*tmp_mat[k][j];
            }
            matrix[j][i] = matrix[i][j];
        }
    }

    return matrix;
}

/*! Function to calculate the effective coefficients
 *
 * This function calculates the homogenized coefficients from the solution of
 * the cell problem, and the discretization of the coefficients in hermite functions.
 */
SDE_coeffs Solver_spectral::compute_averages(const mat& functions, const mat& solutions) {

    int ns = problem->ns;

    mat coeffs(ns);
    mat sol(ns);
    cube coeffs_dx(ns, mat(ns));
    cube sol_dx(ns, mat(ns));

    for (int i = 0; i < ns; ++i) {
        coeffs[i] = functions[i];
        sol[i] = solutions[i];
        for (int j = 0; j < ns; j++) {
            coeffs_dx[i][j] = functions[ns + i*ns + j];
            sol_dx[i][j] = solutions[ns + i*ns +j];
        }
    }
    vec coeffs_h = functions[ns + ns*ns];

    // Drift coefficient
    vec F1(ns, 0.);
    vec F2(ns, 0.);

    // Diffusion coefficient
    mat A0(ns, vec(ns,0.));

    // Calculation of the coefficients of the effective equation
    for (unsigned int i = 0; i < solutions[0].size(); ++i) {

        // First part of the drift coefficient
        for (int j = 0; j < ns; ++j) {
            for (int k = 0; k < ns; ++k)
                F1[j] += sol_dx[j][k][i]*coeffs[k][i];
        }

        // Second part of the drift coefficient
        for (int j = 0; j < ns; ++j) {
            F2[j] += sol[j][i]*coeffs_h[i];
        }

        // Diffusion coefficient
        for (int j = 0; j < ns; ++j) {
            for (int k = 0; k < ns; ++k) {
                A0[j][k] += 2*sol[j][i]*coeffs[k][i];
            }
        }
    }

    arma::vec eigval; arma::mat eigvec;
    eig_sym(eigval, eigvec, to_arma(symmetric(A0)));
    vec eigen_values = to_std_vec(eigval);

    if (arma::min(eigval) < -1E-14) {
        cout << "Warning: Matrix not positive definite" << endl;
        A0 = mat(ns, vec(ns, 0.));
    }

    SDE_coeffs sde_coeffs;
    sde_coeffs.diff =  square_root(symmetric(A0));
    sde_coeffs.drif = F1 + F2;

    return sde_coeffs;
}

/*! Function that calculates the 0-order term in the Schrodinger equation
 *
 * Assuming that L is the generator of an OU process with
 *
 * TODO: add description (urbain, Thu 25 Jun 2015 01:27:09 CEST)
 */
double Solver_spectral::gaussian_linear_term(vec z) {

    // Laplacian term.
    double laplacian = 0.;

    // Square of the gradient.
    double grad2 = 0.;

    // Square of the coefficient of the noise.
    double S = problem->s*problem->s;

    // Computation of the term
    for (int k = 0; k < this->nf; ++k) {
        laplacian += 1/this->eig_val_cov[k];
        grad2 += z[k]*z[k] /this->eig_val_cov[k];
    }

    // Standard formula for the potential
    return (0.25 * S * laplacian - 0.125 * S * grad2);
}

/*! Change of variable y = Cz + m
 *
 * This function implements the change of variables y = Cz + m.. If z is
 * distributed as G(0,I), y will be distributed as G(CC^T,m).
 */
vec Solver_spectral::map_to_real(vec z) {

    // Initialization
    vec result(z.size(), 0.);

    for (unsigned int i = 0; i < z.size(); ++i) {

        // Left-multiply z by covariance matrix
        for (unsigned int j = 0; j < z.size(); ++j) {
            result[i] += this->sqrt_cov[i][j] * z[j];
        }

        // Add bias
        result[i] += this->bias[i];
    }

    return result;
}

/*! Update the statistics of the Gaussian related to Hermite functions
 *
 * This function updates the statistics of the Gaussian from which the Hermite
 * functions are calculated. In simple words, the Hermite functions are
 * concentrated where the density of the Gaussian is non-zero.
 */
void Solver_spectral::update_stats() {

    // Scaling for covariant matrix
    vec var_scaling = conf->scaling;

    // Update of bias and covariance
    this->bias = analyser->bias;
    this->eig_vec_cov = analyser->eig_vec_cov;
    this->eig_val_cov = analyser->eig_val_cov;

    // Apply user-defined extra-scaling
    for (int i = 0; i < nf; ++i) {
        this->eig_val_cov[i] *= var_scaling[i]*var_scaling[i];
    }

    // Square root of covariance matrix
    this->sqrt_cov = this->eig_vec_cov;
    for (int i = 0; i < nf; i++) {
        for (int j = 0; j < nf; j++) {
            this->sqrt_cov[i][j] *= sqrt(this->eig_val_cov[j]);
        }
    }

    // Determinant of the covariance matrix
    this->det_cov = 1.;
    for (int i = 0; i < nf; ++i) {
        this->det_cov *= this->eig_val_cov[i];
    }
}

vec Solver_spectral::discretize(vec x, double(*f)(vec, vec )) {

    // Number of integration points
    int ni = gauss->weights.size();

    // Weights and points of the integrator.
    mat quad_points = gauss->nodes;
    vec quad_weights = gauss->weights;

    // Discretization of a at gridpoints
    vec f_discretized(ni, 1.);

    // Computation of the function to integrate at the gridpoints.
    for (int j = 0; j < ni; ++j) {

        // Integration point and rescaled version for integrattion
        vec z = quad_points[j];
        vec y = map_to_real(z);

        // Scaling to pass to Schrodinger equation;
        f_discretized[j] *= sqrt(analyser->rho(x,y));

        // Scaling needed for the integration;
        f_discretized[j] /= sqrt(gaussian(z));

        // Discretization of a
        f_discretized[j] *= f(x,y);

        // Weight of the integration
        f_discretized[j] *= quad_weights[j];
    }

    return f_discretized;
}

vec Solver_spectral::project_mon(int nf, int degree, vec f_discretized, int rescale) {

    // Number of polynomials
    int nb = bin(degree + nf, nf);

    // Number of integration points
    int ni = gauss->weights.size();

    // Nodes of the gaussian quadrature
    mat quad_points = gauss->nodes;

    // Vector of coefficients of the projection
    vec coefficients(nb, 0.);

    // Vector of multi-indices
    vector< vector<int> > m(nb);

    // Vector of mapped indices of lower degrees
    vector<int> mi(nb);

    // Nonzero element in m - mapped_m
    vector<int> delta(nb);

    for (int i = 0; i < nb; ++i) {

        // Multi-index associated with j
        m[i] = ind2mult[i];

        // Mapping that associates for each multi-index one of lower degree.
        for (delta[i] = 0; i != 0 && m[i][delta[i]] == 0; ++delta[i]) {}

        // Initialization of mapped multi-index
        vector<int> mm = m[i];

        // Decrease degree by 1
        mm[delta[i]] --;

        // Mapped linear index
        mi[i] = mult2ind[mm];
    }

    // Loop over the points of the quadrature
    for (int i = 0; i < ni; ++i) {

        // Quadrature point
        vec quad_point = quad_points[i];

        // Evaluate monomials in point
        vec mon_val(nb, 1.);

        // Evaluation by incrementation of the degree
        for (int j = 1; j < nb; ++j) {
            mon_val[j] = mon_val[mi[j]] * quad_point[delta[j]];
        }

        // Loop over all the monomials
        for (int j = 0; j < nb; ++j) {

            // Update coefficient
            coefficients[j] += f_discretized[i] * mon_val[j];
        }
    }

    // Scaling due to change of variable
    for (int i = 0; i < nb && rescale; ++i) {
        coefficients[i] *= sqrt(sqrt(det_cov));
    }

    return coefficients;
}

vec Solver_spectral::project_herm(int nf, int degree, vec f_discretized, int rescale) {
    return hermiteCoeffs_nd * project_mon(nf, degree, f_discretized, rescale);
}

/*! Constructor of the spectral solver
 *
 * The contructor takes 3 arguments:
 * - The degree of the polynomial approximation to use,
 * - The number of nodes to use in the Gauss-Hermite quadrature,
 * - The number of variables in the fast processes.
 */
Solver_spectral::Solver_spectral(Problem *p, Analyser *a, config_spectral* config) {

    conf = config;

    problem = p;
    analyser = a;

    // Number of fast processes
    nf = problem->nf;
    ns = problem->ns;

    // Integrator
    gauss = new Gaussian_integrator(conf->n_nodes, nf);

    // Weights and points of the integrator.
    mat quad_points = gauss->nodes;
    vec quad_weights = gauss->weights;

    // Initialize multi-indices
    ind2mult = lower_multi_indices(nf, 2*conf->degree);
    for (unsigned int i = 0; i < ind2mult.size(); ++i) {
        mult2ind.emplace(ind2mult[i], i);
    }

    // Dimension of the approximation space
    int nb = bin(conf->degree + problem->nf, problem->nf);

    // Matrices that will contain the coefficients of the 1- and multi- dimensional
    // Hermite polynomials
    mat mat1d (conf->degree + 1, vec(conf->degree + 1,0.));
    mat matnd(nb, vec(nb,0.));

    // Fill the matrix for the unidimensional coefficients.
    hermite_coefficients(conf->degree, mat1d);

    // Calculate the multi-dimensional coefficients by tensor products.
    for (int i = 0; i < nb; ++i) {

        // Multi-index corresponding to linear index i
        vector<int> m1 = ind2mult[i];

        for (int j = 0; j < nb; ++j) {

            // Multi-index corresponding to linear index j
            vector<int> m2 = ind2mult[j];

            // Calculation of the coefficients of x^m2 in h^m1
            matnd[i][j] = 1.;

            for (int k = 0; k < problem->nf; ++k) {
                matnd[i][j] *= mat1d[m1[k]][m2[k]];
            }
        }
    }

    // Initialize variables of the solver
    this->hermiteCoeffs_1d = mat1d;
    this->hermiteCoeffs_nd = matnd;
}

/*! Function to compute hermite coefficients
 *
 * This fills the matrix passed in argument with the hermite coefficients of
 * degree 0 to the degree passed to the method. The coefficients on the first
 * line represent the coefficients of the.
 */
void Solver_spectral::hermite_coefficients (int degree, mat& matrix) {

    if (degree >= 0) {
        matrix[0][0] = 1.0;
    }

    if (degree >= 1) {
        matrix[1][1] = 1.0;
    }

    if (degree >= 2) {
        matrix[2][0] = sqrt(2.0)*(-1.0/2.0);
        matrix[2][2] = sqrt(2.0)*(1.0/2.0);
    }

    if (degree >= 3) {
        matrix[3][1] = sqrt(6.0)*(-1.0/2.0);
        matrix[3][3] = sqrt(6.0)*(1.0/6.0);
    }

    if (degree >= 4) {
        matrix[4][0] = sqrt(6.0)*(1.0/4.0);
        matrix[4][2] = sqrt(6.0)*(-1.0/2.0);
        matrix[4][4] = sqrt(6.0)*(1.0/1.2E1);
    }

    if (degree >= 5) {
        matrix[5][1] = sqrt(3.0E1)*(1.0/4.0);
        matrix[5][3] = sqrt(3.0E1)*(-1.0/6.0);
        matrix[5][5] = sqrt(3.0E1)*(1.0/6.0E1);
    }

    if (degree >= 6) {
        matrix[6][0] = sqrt(5.0)*(-1.0/4.0);
        matrix[6][2] = sqrt(5.0)*(3.0/4.0);
        matrix[6][4] = sqrt(5.0)*(-1.0/4.0);
        matrix[6][6] = sqrt(5.0)*(1.0/6.0E1);
    }

    if (degree >= 7) {
        matrix[7][1] = sqrt(3.5E1)*(-1.0/4.0);
        matrix[7][3] = sqrt(3.5E1)*(1.0/4.0);
        matrix[7][5] = sqrt(3.5E1)*(-1.0/2.0E1);
        matrix[7][7] = sqrt(3.5E1)*(1.0/4.2E2);
    }

    if (degree >= 8) {
        matrix[8][0] = sqrt(7.0E1)*(1.0/1.6E1);
        matrix[8][2] = sqrt(7.0E1)*(-1.0/4.0);
        matrix[8][4] = sqrt(7.0E1)*(1.0/8.0);
        matrix[8][6] = sqrt(7.0E1)*(-1.0/6.0E1);
        matrix[8][8] = sqrt(7.0E1)*5.952380952380952E-4;
    }

    if (degree >= 9) {
        matrix[9][1] = sqrt(7.0E1)*(3.0/1.6E1);
        matrix[9][3] = sqrt(7.0E1)*(-1.0/4.0);
        matrix[9][5] = sqrt(7.0E1)*(3.0/4.0E1);
        matrix[9][7] = sqrt(7.0E1)*(-1.0/1.4E2);
        matrix[9][9] = sqrt(7.0E1)*1.984126984126984E-4;
    }

    if (degree >= 10) {
        matrix[10][0] = sqrt(7.0)*(-3.0/1.6E1);
        matrix[10][2] = sqrt(7.0)*(1.5E1/1.6E1);
        matrix[10][4] = sqrt(7.0)*(-5.0/8.0);
        matrix[10][6] = sqrt(7.0)*(1.0/8.0);
        matrix[10][8] = sqrt(7.0)*(-1.0/1.12E2);
        matrix[10][10] = sqrt(7.0)*1.984126984126984E-4;
    }

    if (degree >= 11) {
        matrix[11][1] = sqrt(7.7E1)*(-3.0/1.6E1);
        matrix[11][3] = sqrt(7.7E1)*(5.0/1.6E1);
        matrix[11][5] = sqrt(7.7E1)*(-1.0/8.0);
        matrix[11][7] = sqrt(7.7E1)*(1.0/5.6E1);
        matrix[11][9] = sqrt(7.7E1)*(-9.920634920634921E-4);
        matrix[11][11] = sqrt(7.7E1)*1.803751803751804E-5;
    }

    if (degree >= 12) {
        matrix[12][0] = sqrt(2.31E2)*(1.0/3.2E1);
        matrix[12][2] = sqrt(2.31E2)*(-3.0/1.6E1);
        matrix[12][4] = sqrt(2.31E2)*(5.0/3.2E1);
        matrix[12][6] = sqrt(2.31E2)*(-1.0/2.4E1);
        matrix[12][8] = sqrt(2.31E2)*(1.0/2.24E2);
        matrix[12][10] = sqrt(2.31E2)*(-1.984126984126984E-4);
        matrix[12][12] = sqrt(2.31E2)*3.006253006253006E-6;
    }

    if (degree >= 13) {
        matrix[13][1] = 5.479963503528103E1*(1.0/3.2E1);
        matrix[13][3] = 5.479963503528103E1*(-1.0/1.6E1);
        matrix[13][5] = 5.479963503528103E1*(1.0/3.2E1);
        matrix[13][7] = 5.479963503528103E1*(-1.0/1.68E2);
        matrix[13][9] = 5.479963503528103E1*4.96031746031746E-4;
        matrix[13][11] = 5.479963503528103E1*(-1.803751803751804E-5);
        matrix[13][13] = 5.479963503528103E1*2.312502312502313E-7;
    }

    if (degree >= 14) {
        matrix[14][0] = sqrt(8.58E2)*(-1.0/6.4E1);
        matrix[14][2] = sqrt(8.58E2)*(7.0/6.4E1);
        matrix[14][4] = sqrt(8.58E2)*(-7.0/6.4E1);
        matrix[14][6] = sqrt(8.58E2)*(7.0/1.92E2);
        matrix[14][8] = sqrt(8.58E2)*(-1.0/1.92E2);
        matrix[14][10] = sqrt(8.58E2)*3.472222222222222E-4;
        matrix[14][12] = sqrt(8.58E2)*(-1.052188552188552E-5);
        matrix[14][14] = sqrt(8.58E2)*1.156251156251156E-7;
    }

    if (degree >= 15) {
        matrix[15][1] = 3.781534080237807E1*(-3.0/6.4E1);
        matrix[15][3] = 3.781534080237807E1*(7.0/6.4E1);
        matrix[15][5] = 3.781534080237807E1*(-2.1E1/3.2E2);
        matrix[15][7] = 3.781534080237807E1*(1.0/6.4E1);
        matrix[15][9] = 3.781534080237807E1*(-1.0/5.76E2);
        matrix[15][11] = 3.781534080237807E1*9.46969696969697E-5;
        matrix[15][13] = 3.781534080237807E1*(-2.428127428127428E-6);
        matrix[15][15] = 3.781534080237807E1*2.312502312502313E-8;
    }

    if (degree >= 16) {
        matrix[16][0] = 3.781534080237807E1*(3.0/2.56E2);
        matrix[16][2] = 3.781534080237807E1*(-3.0/3.2E1);
        matrix[16][4] = 3.781534080237807E1*(7.0/6.4E1);
        matrix[16][6] = 3.781534080237807E1*(-7.0/1.6E2);
        matrix[16][8] = 3.781534080237807E1*(1.0/1.28E2);
        matrix[16][10] = 3.781534080237807E1*(-6.944444444444444E-4);
        matrix[16][12] = 3.781534080237807E1*3.156565656565657E-5;
        matrix[16][14] = 3.781534080237807E1*(-6.937506937506938E-7);
        matrix[16][16] = 3.781534080237807E1*5.781255781255781E-9;
    }

    if (degree >= 17) {
        matrix[17][1] = 1.559166443969341E2*(3.0/2.56E2);
        matrix[17][3] = 1.559166443969341E2*(-1.0/3.2E1);
        matrix[17][5] = 1.559166443969341E2*(7.0/3.2E2);
        matrix[17][7] = 1.559166443969341E2*(-1.0/1.6E2);
        matrix[17][9] = 1.559166443969341E2*8.680555555555556E-4;
        matrix[17][11] = 1.559166443969341E2*(-6.313131313131313E-5);
        matrix[17][13] = 1.559166443969341E2*2.428127428127428E-6;
        matrix[17][15] = 1.559166443969341E2*(-4.625004625004625E-8);
        matrix[17][17] = 1.559166443969341E2*3.400738694856342E-10;
    }

    if (degree >= 18) {
        matrix[18][0] = 1.102497165529236E2*(-1.0/2.56E2);
        matrix[18][2] = 1.102497165529236E2*(9.0/2.56E2);
        matrix[18][4] = 1.102497165529236E2*(-3.0/6.4E1);
        matrix[18][6] = 1.102497165529236E2*(7.0/3.2E2);
        matrix[18][8] = 1.102497165529236E2*(-3.0/6.4E2);
        matrix[18][10] = 1.102497165529236E2*5.208333333333333E-4;
        matrix[18][12] = 1.102497165529236E2*(-3.156565656565657E-5);
        matrix[18][14] = 1.102497165529236E2*1.040626040626041E-6;
        matrix[18][16] = 1.102497165529236E2*(-1.734376734376734E-8);
        matrix[18][18] = 1.102497165529236E2*1.133579564952114E-10;
    }

    if (degree >= 19) {
        matrix[19][1] = 4.805673730081975E2*(-1.0/2.56E2);
        matrix[19][3] = 4.805673730081975E2*(3.0/2.56E2);
        matrix[19][5] = 4.805673730081975E2*(-3.0/3.2E2);
        matrix[19][7] = 4.805673730081975E2*(1.0/3.2E2);
        matrix[19][9] = 4.805673730081975E2*(-5.208333333333333E-4);
        matrix[19][11] = 4.805673730081975E2*4.734848484848485E-5;
        matrix[19][13] = 4.805673730081975E2*(-2.428127428127428E-6);
        matrix[19][15] = 4.805673730081975E2*6.937506937506938E-8;
        matrix[19][17] = 4.805673730081975E2*(-1.020221608456903E-9);
        matrix[19][19] = 4.805673730081975E2*5.966208236590074E-12;
    }

    if (degree >= 20) {
        matrix[20][0] = 2.149162627629654E2*(1.0/5.12E2);
        matrix[20][2] = 2.149162627629654E2*(-5.0/2.56E2);
        matrix[20][4] = 2.149162627629654E2*(1.5E1/5.12E2);
        matrix[20][6] = 2.149162627629654E2*(-1.0/6.4E1);
        matrix[20][8] = 2.149162627629654E2*(1.0/2.56E2);
        matrix[20][10] = 2.149162627629654E2*(-5.208333333333333E-4);
        matrix[20][12] = 2.149162627629654E2*3.945707070707071E-5;
        matrix[20][14] = 2.149162627629654E2*(-1.734376734376734E-6);
        matrix[20][16] = 2.149162627629654E2*4.335941835941836E-8;
        matrix[20][18] = 2.149162627629654E2*(-5.66789782476057E-10);
        matrix[20][20] = 2.149162627629654E2*2.983104118295037E-12;
    }

    if (degree >= 21) {
        matrix[21][1] = 9.848700421883082E2*(1.0/5.12E2);
        matrix[21][3] = 9.848700421883082E2*(-5.0/7.68E2);
        matrix[21][5] = 9.848700421883082E2*(3.0/5.12E2);
        matrix[21][7] = 9.848700421883082E2*(-1.0/4.48E2);
        matrix[21][9] = 9.848700421883082E2*4.340277777777778E-4;
        matrix[21][11] = 9.848700421883082E2*(-4.734848484848485E-5);
        matrix[21][13] = 9.848700421883082E2*3.035159285159285E-6;
        matrix[21][15] = 9.848700421883082E2*(-1.156251156251156E-7);
        matrix[21][17] = 9.848700421883082E2*2.550554021142256E-9;
        matrix[21][19] = 9.848700421883082E2*(-2.983104118295037E-11);
        matrix[21][21] = 9.848700421883082E2*1.420525770616684E-13;
    }

    if (degree >= 22) {
        matrix[22][0] = 4.199499970234552E2*(-9.765625E-4);
        matrix[22][2] = 4.199499970234552E2*1.07421875E-2;
        matrix[22][4] = 4.199499970234552E2*(-1.790364583333333E-2);
        matrix[22][6] = 4.199499970234552E2*1.07421875E-2;
        matrix[22][8] = 4.199499970234552E2*(-3.069196428571429E-3);
        matrix[22][10] = 4.199499970234552E2*4.774305555555556E-4;
        matrix[22][12] = 4.199499970234552E2*(-4.340277777777778E-5);
        matrix[22][14] = 4.199499970234552E2*2.38476800976801E-6;
        matrix[22][16] = 4.199499970234552E2*(-7.949226699226699E-8);
        matrix[22][18] = 4.199499970234552E2*1.558671901809157E-9;
        matrix[22][20] = 4.199499970234552E2*(-1.64070726506227E-11);
        matrix[22][22] = 4.199499970234552E2*7.102628853083421E-14;
    }

    if (degree >= 23) {
        matrix[23][1] = 2.014009433940169E3*(-9.765625E-4);
        matrix[23][3] = 2.014009433940169E3*3.580729166666667E-3;
        matrix[23][5] = 2.014009433940169E3*(-3.580729166666667E-3);
        matrix[23][7] = 2.014009433940169E3*1.534598214285714E-3;
        matrix[23][9] = 2.014009433940169E3*(-3.410218253968254E-4);
        matrix[23][11] = 2.014009433940169E3*4.340277777777778E-5;
        matrix[23][13] = 2.014009433940169E3*(-3.338675213675214E-6);
        matrix[23][15] = 2.014009433940169E3*1.58984533984534E-7;
        matrix[23][17] = 2.014009433940169E3*(-4.67601570542747E-9);
        matrix[23][19] = 2.014009433940169E3*8.203536325311351E-11;
        matrix[23][21] = 2.014009433940169E3*(-7.812891738391763E-13);
        matrix[23][23] = 2.014009433940169E3*3.088099501340618E-15;
    }

    if (degree >= 24) {
        matrix[24][0] = 8.222159083841665E2*4.8828125E-4;
        matrix[24][2] = 8.222159083841665E2*(-3.0/5.12E2);
        matrix[24][4] = 8.222159083841665E2*1.07421875E-2;
        matrix[24][6] = 8.222159083841665E2*(-7.161458333333333E-3);
        matrix[24][8] = 8.222159083841665E2*2.301897321428571E-3;
        matrix[24][10] = 8.222159083841665E2*(-4.092261904761905E-4);
        matrix[24][12] = 8.222159083841665E2*4.340277777777778E-5;
        matrix[24][14] = 8.222159083841665E2*(-2.861721611721612E-6);
        matrix[24][16] = 8.222159083841665E2*1.192384004884005E-7;
        matrix[24][18] = 8.222159083841665E2*(-3.117343803618313E-9);
        matrix[24][20] = 8.222159083841665E2*4.922121795186811E-11;
        matrix[24][22] = 8.222159083841665E2*(-4.261577311850053E-13);
        matrix[24][24] = 8.222159083841665E2*1.544049750670309E-15;
    }

    if (degree >= 25) {
        matrix[25][1] = 8.222159083841665E2*2.44140625E-3;
        matrix[25][3] = 8.222159083841665E2*(-5.0/5.12E2);
        matrix[25][5] = 8.222159083841665E2*1.07421875E-2;
        matrix[25][7] = 8.222159083841665E2*(-5.115327380952381E-3);
        matrix[25][9] = 8.222159083841665E2*1.278831845238095E-3;
        matrix[25][11] = 8.222159083841665E2*(-1.860119047619048E-4);
        matrix[25][13] = 8.222159083841665E2*1.669337606837607E-5;
        matrix[25][15] = 8.222159083841665E2*(-9.539072039072039E-7);
        matrix[25][17] = 8.222159083841665E2*3.507011779070603E-8;
        matrix[25][19] = 8.222159083841665E2*(-8.203536325311351E-10);
        matrix[25][21] = 8.222159083841665E2*1.171933760758764E-11;
        matrix[25][23] = 8.222159083841665E2*(-9.264298504021853E-14);
        matrix[25][25] = 8.222159083841665E2*3.088099501340618E-16;
    }

    if (degree >= 26) {
        matrix[26][0] = 3.224996124028679E2*(-1.220703125E-3);
        matrix[26][2] = 3.224996124028679E2*1.5869140625E-2;
        matrix[26][4] = 3.224996124028679E2*(-3.173828125E-2);
        matrix[26][6] = 3.224996124028679E2*2.327473958333333E-2;
        matrix[26][8] = 3.224996124028679E2*(-8.312406994047619E-3);
        matrix[26][10] = 3.224996124028679E2*1.662481398809524E-3;
        matrix[26][12] = 3.224996124028679E2*(-2.015128968253968E-4);
        matrix[26][14] = 3.224996124028679E2*1.550099206349206E-5;
        matrix[26][16] = 3.224996124028679E2*(-7.750496031746032E-7);
        matrix[26][18] = 3.224996124028679E2*2.53284184043988E-8;
        matrix[26][20] = 3.224996124028679E2*(-5.332298611452378E-10);
        matrix[26][22] = 3.224996124028679E2*6.925063131756335E-12;
        matrix[26][24] = 3.224996124028679E2*(-5.018161689678504E-14);
        matrix[26][26] = 3.224996124028679E2*1.544049750670309E-16;
    }

    if (degree >= 27) {
        matrix[27][1] = 5.585857141030372E2*(-3.662109375E-3);
        matrix[27][3] = 5.585857141030372E2*1.5869140625E-2;
        matrix[27][5] = 5.585857141030372E2*(-1.904296875E-2);
        matrix[27][7] = 5.585857141030372E2*9.974888392857143E-3;
        matrix[27][9] = 5.585857141030372E2*(-2.770802331349206E-3);
        matrix[27][11] = 5.585857141030372E2*4.534040178571429E-4;
        matrix[27][13] = 5.585857141030372E2*(-4.650297619047619E-5);
        matrix[27][15] = 5.585857141030372E2*3.100198412698413E-6;
        matrix[27][17] = 5.585857141030372E2*(-1.367734593837535E-7);
        matrix[27][19] = 5.585857141030372E2*3.999223958589284E-9;
        matrix[27][21] = 5.585857141030372E2*(-7.617569444931969E-11);
        matrix[27][23] = 5.585857141030372E2*9.032691041421307E-13;
        matrix[27][25] = 5.585857141030372E2*(-6.021794027614205E-15);
        matrix[27][27] = 5.585857141030372E2*1.715610834078121E-17;
    }

    if (degree >= 28) {
        matrix[28][0] = 2.111255550614373E2*1.8310546875E-3;
        matrix[28][2] = 2.111255550614373E2*(-2.5634765625E-2);
        matrix[28][4] = 2.111255550614373E2*5.55419921875E-2;
        matrix[28][6] = 2.111255550614373E2*(-4.443359375E-2);
        matrix[28][8] = 2.111255550614373E2*1.74560546875E-2;
        matrix[28][10] = 2.111255550614373E2*(-3.879123263888889E-3);
        matrix[28][12] = 2.111255550614373E2*5.289713541666667E-4;
        matrix[28][14] = 2.111255550614373E2*(-4.650297619047619E-5);
        matrix[28][16] = 2.111255550614373E2*2.712673611111111E-6;
        matrix[28][18] = 2.111255550614373E2*(-1.063793572984749E-7);
        matrix[28][20] = 2.111255550614373E2*2.799456771012499E-9;
        matrix[28][22] = 2.111255550614373E2*(-4.847544192229435E-11);
        matrix[28][24] = 2.111255550614373E2*5.269069774162429E-13;
        matrix[28][26] = 2.111255550614373E2*(-3.242504476407649E-15);
        matrix[28][28] = 2.111255550614373E2*8.578054170390605E-18;
    }

    if (degree >= 29) {
        matrix[29][1] = 1.13694590900359E3*1.8310546875E-3;
        matrix[29][3] = 1.13694590900359E3*(-8.544921875E-3);
        matrix[29][5] = 1.13694590900359E3*1.11083984375E-2;
        matrix[29][7] = 1.13694590900359E3*(-6.34765625E-3);
        matrix[29][9] = 1.13694590900359E3*1.939561631944444E-3;
        matrix[29][11] = 1.13694590900359E3*(-3.526475694444444E-4);
        matrix[29][13] = 1.13694590900359E3*4.069010416666667E-5;
        matrix[29][15] = 1.13694590900359E3*(-3.100198412698413E-6);
        matrix[29][17] = 1.13694590900359E3*1.595690359477124E-7;
        matrix[29][19] = 1.13694590900359E3*(-5.598913542024997E-9);
        matrix[29][21] = 1.13694590900359E3*1.333074652863095E-10;
        matrix[29][23] = 1.13694590900359E3*(-2.107627909664972E-12);
        matrix[29][25] = 1.13694590900359E3*2.107627909664972E-14;
        matrix[29][27] = 1.13694590900359E3*(-1.200927583854685E-16);
        matrix[29][29] = 1.13694590900359E3*2.957949713927795E-19;
    }

    if (degree >= 30) {
        matrix[30][0] = 1.037884868374137E3*(-3.662109375E-4);
        matrix[30][2] = 1.037884868374137E3*5.4931640625E-3;
        matrix[30][4] = 1.037884868374137E3*(-1.28173828125E-2);
        matrix[30][6] = 1.037884868374137E3*1.11083984375E-2;
        matrix[30][8] = 1.037884868374137E3*(-4.7607421875E-3);
        matrix[30][10] = 1.037884868374137E3*1.163736979166667E-3;
        matrix[30][12] = 1.037884868374137E3*(-1.763237847222222E-4);
        matrix[30][14] = 1.037884868374137E3*1.743861607142857E-5;
        matrix[30][16] = 1.037884868374137E3*(-1.162574404761905E-6);
        matrix[30][18] = 1.037884868374137E3*5.318967864923747E-8;
        matrix[30][20] = 1.037884868374137E3*(-1.679674062607499E-9);
        matrix[30][22] = 1.037884868374137E3*3.635658144172076E-11;
        matrix[30][24] = 1.037884868374137E3*(-5.269069774162429E-13);
        matrix[30][26] = 1.037884868374137E3*4.863756714611473E-15;
        matrix[30][28] = 1.037884868374137E3*(-2.573416251117181E-17);
        matrix[30][30] = 1.037884868374137E3*5.91589942785559E-20;
    }


    if (degree >= 31) {
        matrix[31][1] = 5.778698382854049E3*(-3.662109375E-4);
        matrix[31][3] = 5.778698382854049E3*1.8310546875E-3;
        matrix[31][5] = 5.778698382854049E3*(-2.5634765625E-3);
        matrix[31][7] = 5.778698382854049E3*1.5869140625E-3;
        matrix[31][9] = 5.778698382854049E3*(-5.289713541666667E-4);
        matrix[31][11] = 5.778698382854049E3*1.057942708333333E-4;
        matrix[31][13] = 5.778698382854049E3*(-1.356336805555556E-5);
        matrix[31][15] = 5.778698382854049E3*1.162574404761905E-6;
        matrix[31][17] = 5.778698382854049E3*(-6.838672969187675E-8);
        matrix[31][19] = 5.778698382854049E3*2.799456771012499E-9;
        matrix[31][21] = 5.778698382854049E3*(-7.998447917178567E-11);
        matrix[31][23] = 5.778698382854049E3*1.580720932248729E-12;
        matrix[31][25] = 5.778698382854049E3*(-2.107627909664972E-14);
        matrix[31][27] = 5.778698382854049E3*1.801391375782027E-16;
        matrix[31][29] = 5.778698382854049E3*(-8.873849141783384E-19);
        matrix[31][31] = 5.778698382854049E3*1.908354654146964E-21;
    }

    if (degree >= 32) {
        matrix[32][0] = 8.172313625895668E3*4.57763671875E-5;
        matrix[32][2] = 8.172313625895668E3*(-7.32421875E-4);
        matrix[32][4] = 8.172313625895668E3*1.8310546875E-3;
        matrix[32][6] = 8.172313625895668E3*(-1.708984375E-3);
        matrix[32][8] = 8.172313625895668E3*7.9345703125E-4;
        matrix[32][10] = 8.172313625895668E3*(-2.115885416666667E-4);
        matrix[32][12] = 8.172313625895668E3*3.526475694444444E-5;
        matrix[32][14] = 8.172313625895668E3*(-3.875248015873016E-6);
        matrix[32][16] = 8.172313625895668E3*2.906436011904762E-7;
        matrix[32][18] = 8.172313625895668E3*(-1.519705104263928E-8);
        matrix[32][20] = 8.172313625895668E3*5.598913542024997E-10;
        matrix[32][22] = 8.172313625895668E3*(-1.45426325766883E-11);
        matrix[32][24] = 8.172313625895668E3*2.634534887081215E-13;
        matrix[32][26] = 8.172313625895668E3*(-3.242504476407649E-15);
        matrix[32][28] = 8.172313625895668E3*2.573416251117181E-17;
        matrix[32][30] = 8.172313625895668E3*(-1.183179885571118E-19);
        matrix[32][32] = 8.172313625895668E3*2.385443317683705E-22;
    }

    if (degree >= 33) {
        matrix[33][1] = 4.694636759111401E4*4.57763671875E-5;
        matrix[33][3] = 4.694636759111401E4*(-2.44140625E-4);
        matrix[33][5] = 4.694636759111401E4*3.662109375E-4;
        matrix[33][7] = 4.694636759111401E4*(-2.44140625E-4);
        matrix[33][9] = 4.694636759111401E4*8.816189236111111E-5;
        matrix[33][11] = 4.694636759111401E4*(-1.923532196969697E-5);
        matrix[33][13] = 4.694636759111401E4*2.712673611111111E-6;
        matrix[33][15] = 4.694636759111401E4*(-2.583498677248677E-7);
        matrix[33][17] = 4.694636759111401E4*1.709668242296919E-8;
        matrix[33][19] = 4.694636759111401E4*(-7.998447917178567E-10);
        matrix[33][21] = 4.694636759111401E4*2.666149305726189E-11;
        matrix[33][23] = 4.694636759111401E4*(-6.322883728994915E-13);
        matrix[33][25] = 4.694636759111401E4*1.053813954832486E-14;
        matrix[33][27] = 4.694636759111401E4*(-1.200927583854685E-16);
        matrix[33][29] = 4.694636759111401E4*8.873849141783384E-19;
        matrix[33][31] = 4.694636759111401E4*(-3.816709308293929E-21);
        matrix[33][33] = 4.694636759111401E4*7.228616114193047E-24;
    }

    if (degree >= 34) {
        matrix[34][0] = 8.051235619456184E3*(-4.57763671875E-5);
        matrix[34][2] = 8.051235619456184E3*7.781982421875E-4;
        matrix[34][4] = 8.051235619456184E3*(-2.0751953125E-3);
        matrix[34][6] = 8.051235619456184E3*2.0751953125E-3;
        matrix[34][8] = 8.051235619456184E3*(-1.03759765625E-3);
        matrix[34][10] = 8.051235619456184E3*2.997504340277778E-4;
        matrix[34][12] = 8.051235619456184E3*(-5.450007891414141E-5);
        matrix[34][14] = 8.051235619456184E3*6.587921626984127E-6;
        matrix[34][16] = 8.051235619456184E3*(-5.489934689153439E-7);
        matrix[34][18] = 8.051235619456184E3*3.229373346560847E-8;
        matrix[34][20] = 8.051235619456184E3*(-1.359736145920356E-9);
        matrix[34][22] = 8.051235619456184E3*4.12041256339502E-11;
        matrix[34][24] = 8.051235619456184E3*(-8.957418616076129E-13);
        matrix[34][26] = 8.051235619456184E3*1.378064402473251E-14;
        matrix[34][28] = 8.051235619456184E3*(-1.458269208966403E-16);
        matrix[34][30] = 8.051235619456184E3*1.00570290273545E-18;
        matrix[34][32] = 8.051235619456184E3*(-4.055253640062299E-21);
        matrix[34][34] = 8.051235619456184E3*7.228616114193047E-24;
    }

    if (degree >= 35) {
        matrix[35][1] = 9.526350455447249E3*(-2.288818359375E-4);
        matrix[35][3] = 9.526350455447249E3*1.2969970703125E-3;
        matrix[35][5] = 9.526350455447249E3*(-2.0751953125E-3);
        matrix[35][7] = 9.526350455447249E3*1.482282366071429E-3;
        matrix[35][9] = 9.526350455447249E3*(-5.764431423611111E-4);
        matrix[35][11] = 9.526350455447249E3*1.362501972853535E-4;
        matrix[35][13] = 9.526350455447249E3*(-2.096156881313131E-5);
        matrix[35][15] = 9.526350455447249E3*2.195973875661376E-6;
        matrix[35][17] = 9.526350455447249E3*(-1.614686673280423E-7);
        matrix[35][19] = 9.526350455447249E3*8.498350912002228E-9;
        matrix[35][21] = 9.526350455447249E3*(-3.237467014096087E-10);
        matrix[35][23] = 9.526350455447249E3*8.957418616076129E-12;
        matrix[35][25] = 9.526350455447249E3*(-1.791483723215226E-13);
        matrix[35][27] = 9.526350455447249E3*2.551971115691205E-15;
        matrix[35][29] = 9.526350455447249E3*(-2.514257256838626E-17);
        matrix[35][31] = 9.526350455447249E3*1.62210145602492E-19;
        matrix[35][33] = 9.526350455447249E3*(-6.14432369706409E-22);
        matrix[35][35] = 9.526350455447249E3*1.032659444884721E-24;
    }

    if (degree >= 36) {
        matrix[36][0] = 9.526350455447249E3*3.814697265625E-5;
        matrix[36][2] = 9.526350455447249E3*(-6.866455078125E-4);
        matrix[36][4] = 9.526350455447249E3*1.94549560546875E-3;
        matrix[36][6] = 9.526350455447249E3*(-2.0751953125E-3);
        matrix[36][8] = 9.526350455447249E3*1.111711774553571E-3;
        matrix[36][10] = 9.526350455447249E3*(-3.458658854166667E-4);
        matrix[36][12] = 9.526350455447249E3*6.812509864267677E-5;
        matrix[36][14] = 9.526350455447249E3*(-8.983529491341991E-6);
        matrix[36][16] = 9.526350455447249E3*8.234902033730159E-7;
        matrix[36][18] = 9.526350455447249E3*(-5.382288910934744E-8);
        matrix[36][20] = 9.526350455447249E3*2.549505273600668E-9;
        matrix[36][22] = 9.526350455447249E3*(-8.829455492989328E-11);
        matrix[36][24] = 9.526350455447249E3*2.239354654019032E-12;
        matrix[36][26] = 9.526350455447249E3*(-4.134193207419752E-14);
        matrix[36][28] = 9.526350455447249E3*5.468509533624011E-16;
        matrix[36][30] = 9.526350455447249E3*(-5.028514513677251E-18);
        matrix[36][32] = 9.526350455447249E3*3.041440230046724E-20;
        matrix[36][34] = 9.526350455447249E3*(-1.084292417128957E-22);
        matrix[36][36] = 9.526350455447249E3*1.721099074807868E-25;
    }

    if (degree >= 37) {
        matrix[37][1] = 5.79465276008839E4*3.814697265625E-5;
        matrix[37][3] = 5.79465276008839E4*(-2.288818359375E-4);
        matrix[37][5] = 5.79465276008839E4*3.8909912109375E-4;
        matrix[37][7] = 5.79465276008839E4*(-2.964564732142857E-4);
        matrix[37][9] = 5.79465276008839E4*1.235235305059524E-4;
        matrix[37][11] = 5.79465276008839E4*(-3.144235321969697E-5);
        matrix[37][13] = 5.79465276008839E4*5.240392203282828E-6;
        matrix[37][15] = 5.79465276008839E4*(-5.989019660894661E-7);
        matrix[37][17] = 5.79465276008839E4*4.84406001984127E-8;
        matrix[37][19] = 5.79465276008839E4*(-2.832783637334076E-9);
        matrix[37][21] = 5.79465276008839E4*1.214050130286033E-10;
        matrix[37][23] = 5.79465276008839E4*(-3.838893692604055E-12);
        matrix[37][25] = 5.79465276008839E4*8.957418616076129E-14;
        matrix[37][27] = 5.79465276008839E4*(-1.531182669414723E-15);
        matrix[37][29] = 5.79465276008839E4*1.885692942628969E-17;
        matrix[37][31] = 5.79465276008839E4*(-1.62210145602492E-19);
        matrix[37][33] = 5.79465276008839E4*9.216485545596135E-22;
        matrix[37][35] = 5.79465276008839E4*(-3.097978334654163E-24);
        matrix[37][37] = 5.79465276008839E4*4.651619121102347E-27;
    }

    if (degree >= 38) {
        matrix[38][0] = 1.880033611401669E4*(-1.9073486328125E-5);
        matrix[38][2] = 1.880033611401669E4*3.62396240234375E-4;
        matrix[38][4] = 1.880033611401669E4*(-1.087188720703125E-3);
        matrix[38][6] = 1.880033611401669E4*1.232147216796875E-3;
        matrix[38][8] = 1.880033611401669E4*(-7.040841238839286E-4);
        matrix[38][10] = 1.880033611401669E4*2.346947079613095E-4;
        matrix[38][12] = 1.880033611401669E4*(-4.978372593118687E-5);
        matrix[38][14] = 1.880033611401669E4*7.11196084731241E-6;
        matrix[38][16] = 1.880033611401669E4*(-7.11196084731241E-7);
        matrix[38][18] = 1.880033611401669E4*5.113174465388007E-8;
        matrix[38][20] = 1.880033611401669E4*(-2.691144455467372E-9);
        matrix[38][22] = 1.880033611401669E4*1.048497839792483E-10;
        matrix[38][24] = 1.880033611401669E4*(-3.039124173311544E-12);
        matrix[38][26] = 1.880033611401669E4*6.545805911747941E-14;
        matrix[38][28] = 1.880033611401669E4*(-1.039016811388562E-15);
        matrix[38][30] = 1.880033611401669E4*1.194272196998347E-17;
        matrix[38][32] = 1.880033611401669E4*(-9.631227395147961E-20);
        matrix[38][34] = 1.880033611401669E4*5.150388981362546E-22;
        matrix[38][36] = 1.880033611401669E4*(-1.635044121067475E-24);
        matrix[38][38] = 1.880033611401669E4*2.325809560551173E-27;
    }

    if (degree >= 39) {
        matrix[39][1] = 3.913602046708377E4*(-5.7220458984375E-5);
        matrix[39][3] = 3.913602046708377E4*3.62396240234375E-4;
        matrix[39][5] = 3.913602046708377E4*(-6.52313232421875E-4);
        matrix[39][7] = 3.913602046708377E4*5.280630929129464E-4;
        matrix[39][9] = 3.913602046708377E4*(-2.346947079613095E-4);
        matrix[39][11] = 3.913602046708377E4*6.400764762581169E-5;
        matrix[39][13] = 3.913602046708377E4*(-1.14885521379662E-5);
        matrix[39][15] = 3.913602046708377E4*1.422392169462482E-6;
        matrix[39][17] = 3.913602046708377E4*(-1.255051914231602E-7);
        matrix[39][19] = 3.913602046708377E4*8.073433366402116E-9;
        matrix[39][21] = 3.913602046708377E4*(-3.844492079239103E-10);
        matrix[39][23] = 3.913602046708377E4*1.367605877990195E-11;
        matrix[39][25] = 3.913602046708377E4*(-3.646949007973853E-13);
        matrix[39][27] = 3.913602046708377E4*7.273117679719934E-15;
        matrix[39][29] = 3.913602046708377E4*(-1.074844977298512E-16);
        matrix[39][31] = 3.913602046708377E4*1.155747287417755E-18;
        matrix[39][33] = 3.913602046708377E4*(-8.755661268316328E-21);
        matrix[39][35] = 3.913602046708377E4*4.414619126882182E-23;
        matrix[39][37] = 3.913602046708377E4*(-1.325711449514169E-25);
        matrix[39][39] = 3.913602046708377E4*1.789084277347056E-28;
    }

    if (degree >= 40) {
        matrix[40][0] = 6.187948161547574E4*5.7220458984375E-6;
        matrix[40][2] = 6.187948161547574E4*(-1.1444091796875E-4);
        matrix[40][4] = 6.187948161547574E4*3.62396240234375E-4;
        matrix[40][6] = 6.187948161547574E4*(-4.3487548828125E-4);
        matrix[40][8] = 6.187948161547574E4*2.640315464564732E-4;
        matrix[40][10] = 6.187948161547574E4*(-9.387788318452381E-5);
        matrix[40][12] = 6.187948161547574E4*2.133588254193723E-5;
        matrix[40][14] = 6.187948161547574E4*(-3.282443467990343E-6);
        matrix[40][16] = 6.187948161547574E4*3.555980423656205E-7;
        matrix[40][18] = 6.187948161547574E4*(-2.789004253848004E-8);
        matrix[40][20] = 6.187948161547574E4*1.614686673280423E-9;
        matrix[40][22] = 6.187948161547574E4*(-6.989985598616551E-11);
        matrix[40][24] = 6.187948161547574E4*2.279343129983658E-12;
        matrix[40][26] = 6.187948161547574E4*(-5.610690781498235E-14);
        matrix[40][28] = 6.187948161547574E4*1.039016811388562E-15;
        matrix[40][30] = 6.187948161547574E4*(-1.433126636398017E-17);
        matrix[40][32] = 6.187948161547574E4*1.444684109272194E-19;
        matrix[40][34] = 6.187948161547574E4*(-1.030077796272509E-21);
        matrix[40][36] = 6.187948161547574E4*4.905132363202425E-24;
        matrix[40][38] = 6.187948161547574E4*(-1.395485736330704E-26);
        matrix[40][40] = 6.187948161547574E4*1.789084277347056E-29;
    }

    if (degree > 40) {
        cout << "Degree too high" << endl; exit(0);
    }
}
