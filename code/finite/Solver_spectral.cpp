/* TODO: Problem with exact sigma (urbain, Wed 20 May 2015 21:00:20 BST) */

#include "structures.hpp"
#include "toolbox.hpp"
#include "Problem.hpp"
#include "Gaussian_integrator.hpp"
#include "Solver_spectral.hpp"
#include "templates.hpp"
#include "io.hpp"
#include <iomanip>

using namespace std;

double Solver_spectral::basis(vector<int> mult, vector<double> x, vector<double> sigmas) {
    assert(poly_basis == "MONOMIAL" || poly_basis == "HERMITE");
    double result = 0.;

    if (poly_basis == "MONOMIAL") {
        result = 1.;
        for (unsigned int i = 0; i < mult.size(); ++i) {
            result *= ipow(x[i]/sigmas[i], mult[i]);
        }
    }
    else if (poly_basis == "HERMITE") {
        result = 1.;
        for (unsigned int i = 0; i < mult.size(); ++i) {
            double result_i = 0.;
            double xi = x[i]/sigmas[i];
            for (unsigned int j = 0; j < hermiteCoeffs_1d.size(); ++j) {
                result_i += hermiteCoeffs_1d[mult[i]][j] * ipow(xi, j);
            }
            result *= result_i;
        }
    }
    return result;
}

void Solver_spectral::estimator(Problem &problem, vector<double> x,  vector<SDE_coeffs>& c, double t) {

    int nf = problem.nf;
    int ns = problem.d;
    int nb = bin(degree + nf, nf);

    c = vector<SDE_coeffs> (nb);
    Gaussian_integrator gauss = Gaussian_integrator(nNodes,nf);

    /* vector<double> sigmas_hf(1, 1./sqrt(2.) ); */
    vector<double> sigmas_hf = {0.6, 0.6};

    // Expansion of right-hand side of the Poisson equation
    vector< vector<double> > coefficients(ns, vector<double>(nb, 0.));
    vector< vector <vector<double> > > coefficients_dx(ns, vector< vector<double> >(ns, vector<double>(nb, 0.)));
    vector<double> coefficients_h(nb, 0.);
    cout << "* Calculating the right-hand side of the linear system." << endl;
    for (int i = 0; i < nb; ++i) {
        progress_bar(((double) i )/((double) nb));
        vector<int> multIndex = ind2mult(i, degree, nf);

        vector<double> v0(ns,0.);
        vector< vector<double> > m0(ns, vector<double> (ns,0.));

        auto lambda = [&] (vector<double> y) -> vector<double> {
            return problem.a(x,y) * basis(multIndex, y, sigmas_hf) * sqrt( problem.rho(x,y) / gaussian(y,sigmas_hf) ); };
        auto lambda_dx = [&] (vector<double> y) -> vector< vector<double> > {
            return problem.dax(x,y) * basis(multIndex, y, sigmas_hf) * sqrt( problem.rho(x,y) / gaussian(y,sigmas_hf) ); };
        auto lambda_h = [&] (vector<double> y) -> double {
            return problem.stardiv_h(x,y) * basis(multIndex, y, sigmas_hf) * sqrt( problem.rho(x,y) / gaussian(y,sigmas_hf) ); };

        vector<double> result = gauss.quadnd(lambda, sigmas_hf, v0);
        vector< vector<double> > result_dx = gauss.quadnd(lambda_dx, sigmas_hf, m0);
        double result_h = gauss.quadnd(lambda_h, sigmas_hf);

        for (int j = 0; j < ns; ++j) {
            coefficients[j][i] = result[j];
            for (int k = 0; k < ns; ++k) {
                coefficients_dx[j][k][i] = result_dx[j][k];
            }
        }
        coefficients_h[i] = result_h;
    }
    end_progress_bar();

    for (int i = 0; i < ns; ++i) {
        for (int j = 0; j < ns; ++j) {
            coefficients_dx[i][j] = basis2herm(coefficients_dx[i][j],nf,degree);
        }
        coefficients[i] = basis2herm(coefficients[i],nf,degree);
    }
    coefficients_h = basis2herm(coefficients_h,nf,degree);

    // Solution of the Poisson equation
    vector< vector<double> > solution(ns, vector<double>(nb,0.));
    vector< vector < vector<double> > > solution_dx(ns, vector< vector<double> >(ns, vector<double>(nb, 0.)));

    int n_tmp  = bin(2*degree + nf, nf);
    int n_done = 0;
    vector<double> tmp_vec(n_tmp, 0.);
    vector<int> position_visited(n_tmp, 0);
    cout << "* Calculating necessary the products < L mi , mj >." << endl;
    for (int i = 0; i < nb; ++i) {
        vector<int> m1 = ind2mult(i, degree, nf);
        for (int j = 0; j < nb; ++j) {
            vector<int> m2 = ind2mult(j, degree, nf);
            int index = mult2ind(m1 + m2, 2*degree);
            if (position_visited[index] == 0) {
                n_done ++;
                progress_bar(((double) n_done)/((double) n_tmp));
                position_visited[index] = 1;
                auto lambda = [&] (vector<double> y) -> double {
                    double tmp = 0.;
                    for (int k = 0; k < nf; ++k) {
                        tmp += 1/(2*pow(sigmas_hf[k], 2)) - pow(y[k], 2)/(4*pow(sigmas_hf[k], 4));
                    }
                    return (tmp - problem.linearTerm(x,y)) * basis(m1 + m2, y, sigmas_hf); };
                    tmp_vec[index] += gauss.quadnd(lambda, sigmas_hf);
            }
        }
    }
    end_progress_bar();

    vector< vector<double> > prod_mat(nb, vector<double>(nb, 0.));
    for (int i = 0; i < nb; ++i) {
        vector<int> m1 = ind2mult(i, degree, nf);
        for (int j = 0; j < nb; ++j) {
            vector<int> m2 = ind2mult(j, degree, nf);
            int index = mult2ind(m1 + m2, 2*degree);
            prod_mat[i][j] += tmp_vec[index];
        }
    }

    cout << "* Creating the matrix of the linear system." << endl;
    vector< vector<double> > mat(nb, vector<double>(nb, 0.));
    for (int i = 0; i < nb; ++i) {
        vector<int> m1 = ind2mult(i, degree, nf);
        for (int j = 0; j < nf; ++j) {
            mat[i][i] += m1[j]/(sigmas_hf[j]*sigmas_hf[j]);
        }
        for (int j = 0; j <= i; ++j) {
            progress_bar(( (double) (i*(i+1)) )/( (double) (nb*(nb+1)) ));
            for (int k = 0; k < nb; ++k) {
                for (int l = 0; l < nb; ++l) {
                    mat[i][j] += herm_to_basis[i][k]*herm_to_basis[j][l]*prod_mat[k][l];
                }
            }
            mat[j][i] = mat[i][j];
        }
    }
    end_progress_bar();

    cout << "* Solving linear system." << endl;
    for (int i = 0; i < ns; ++i) {
        solution[i] = solve(mat, coefficients[i]);
        for (int j = 0; j < ns; ++j) {
            solution_dx[i][j] = solve(mat, coefficients_dx[i][j]);
        }
    }
    cout << "done!" << endl;

    // Calculation of the coefficients of the simplified equation
    vector<double> F1(ns, 0.);
    vector<double> F2(ns, 0.);
    vector< vector <double> > A0(ns, vector<double>(ns,0.));
    for (int j = 0; j < nb; ++j) {
        for (int k = 0; k < ns; ++k) {
            for (int l = 0; l < ns; ++l)
                F1[k] += solution_dx[k][l][j]*coefficients[l][j];
        }

        for (int k = 0; k < ns; ++k) {
            F2[k] += solution[k][j]*coefficients_h[j];
        }

        for (int k = 0; k < ns; ++k) {
            for (int l = 0; l < ns; ++l) {
                A0[k][l] += 2*solution[k][j]*coefficients[l][j];
            }
        }

        c[j].diff =  cholesky(symmetric(A0));
        c[j].drif = F1 + F2;
    }
}

Solver_spectral::Solver_spectral(int degree, int nNodes, int n_vars, string string_basis) {
    this->degree = degree;
    this->nNodes = nNodes;
    this->poly_basis = string_basis;

    vector< vector<double> > mat1d(degree + 1, vector<double>(degree + 1,0.));

    if (degree >= 0) {
        mat1d[0][0] = 1.0;
    }

    if (degree >= 1) {
        mat1d[1][1] = 1.0;
    }

    if (degree >= 2) {
        mat1d[2][0] = sqrt(2.0)*(-1.0/2.0);
        mat1d[2][2] = sqrt(2.0)*(1.0/2.0);
    }

    if (degree >= 3) {
        mat1d[3][1] = sqrt(6.0)*(-1.0/2.0);
        mat1d[3][3] = sqrt(6.0)*(1.0/6.0);
    }

    if (degree >= 4) {
        mat1d[4][0] = sqrt(6.0)*(1.0/4.0);
        mat1d[4][2] = sqrt(6.0)*(-1.0/2.0);
        mat1d[4][4] = sqrt(6.0)*(1.0/1.2E1);
    }

    if (degree >= 5) {
        mat1d[5][1] = sqrt(3.0E1)*(1.0/4.0);
        mat1d[5][3] = sqrt(3.0E1)*(-1.0/6.0);
        mat1d[5][5] = sqrt(3.0E1)*(1.0/6.0E1);
    }

    if (degree >= 6) {
        mat1d[6][0] = sqrt(5.0)*(-1.0/4.0);
        mat1d[6][2] = sqrt(5.0)*(3.0/4.0);
        mat1d[6][4] = sqrt(5.0)*(-1.0/4.0);
        mat1d[6][6] = sqrt(5.0)*(1.0/6.0E1);
    }

    if (degree >= 7) {
        mat1d[7][1] = sqrt(3.5E1)*(-1.0/4.0);
        mat1d[7][3] = sqrt(3.5E1)*(1.0/4.0);
        mat1d[7][5] = sqrt(3.5E1)*(-1.0/2.0E1);
        mat1d[7][7] = sqrt(3.5E1)*(1.0/4.2E2);
    }

    if (degree >= 8) {
        mat1d[8][0] = sqrt(7.0E1)*(1.0/1.6E1);
        mat1d[8][2] = sqrt(7.0E1)*(-1.0/4.0);
        mat1d[8][4] = sqrt(7.0E1)*(1.0/8.0);
        mat1d[8][6] = sqrt(7.0E1)*(-1.0/6.0E1);
        mat1d[8][8] = sqrt(7.0E1)*5.952380952380952E-4;
    }

    if (degree >= 9) {
        mat1d[9][1] = sqrt(7.0E1)*(3.0/1.6E1);
        mat1d[9][3] = sqrt(7.0E1)*(-1.0/4.0);
        mat1d[9][5] = sqrt(7.0E1)*(3.0/4.0E1);
        mat1d[9][7] = sqrt(7.0E1)*(-1.0/1.4E2);
        mat1d[9][9] = sqrt(7.0E1)*1.984126984126984E-4;
    }

    if (degree >= 10) {
        mat1d[10][0] = sqrt(7.0)*(-3.0/1.6E1);
        mat1d[10][2] = sqrt(7.0)*(1.5E1/1.6E1);
        mat1d[10][4] = sqrt(7.0)*(-5.0/8.0);
        mat1d[10][6] = sqrt(7.0)*(1.0/8.0);
        mat1d[10][8] = sqrt(7.0)*(-1.0/1.12E2);
        mat1d[10][10] = sqrt(7.0)*1.984126984126984E-4;
    }

    if (degree >= 11) {
        mat1d[11][1] = sqrt(7.7E1)*(-3.0/1.6E1);
        mat1d[11][3] = sqrt(7.7E1)*(5.0/1.6E1);
        mat1d[11][5] = sqrt(7.7E1)*(-1.0/8.0);
        mat1d[11][7] = sqrt(7.7E1)*(1.0/5.6E1);
        mat1d[11][9] = sqrt(7.7E1)*(-9.920634920634921E-4);
        mat1d[11][11] = sqrt(7.7E1)*1.803751803751804E-5;
    }

    if (degree >= 12) {
        mat1d[12][0] = sqrt(2.31E2)*(1.0/3.2E1);
        mat1d[12][2] = sqrt(2.31E2)*(-3.0/1.6E1);
        mat1d[12][4] = sqrt(2.31E2)*(5.0/3.2E1);
        mat1d[12][6] = sqrt(2.31E2)*(-1.0/2.4E1);
        mat1d[12][8] = sqrt(2.31E2)*(1.0/2.24E2);
        mat1d[12][10] = sqrt(2.31E2)*(-1.984126984126984E-4);
        mat1d[12][12] = sqrt(2.31E2)*3.006253006253006E-6;
    }

    if (degree >= 13) {
        mat1d[13][1] = 5.479963503528103E1*(1.0/3.2E1);
        mat1d[13][3] = 5.479963503528103E1*(-1.0/1.6E1);
        mat1d[13][5] = 5.479963503528103E1*(1.0/3.2E1);
        mat1d[13][7] = 5.479963503528103E1*(-1.0/1.68E2);
        mat1d[13][9] = 5.479963503528103E1*4.96031746031746E-4;
        mat1d[13][11] = 5.479963503528103E1*(-1.803751803751804E-5);
        mat1d[13][13] = 5.479963503528103E1*2.312502312502313E-7;
    }

    if (degree >= 14) {
        mat1d[14][0] = sqrt(8.58E2)*(-1.0/6.4E1);
        mat1d[14][2] = sqrt(8.58E2)*(7.0/6.4E1);
        mat1d[14][4] = sqrt(8.58E2)*(-7.0/6.4E1);
        mat1d[14][6] = sqrt(8.58E2)*(7.0/1.92E2);
        mat1d[14][8] = sqrt(8.58E2)*(-1.0/1.92E2);
        mat1d[14][10] = sqrt(8.58E2)*3.472222222222222E-4;
        mat1d[14][12] = sqrt(8.58E2)*(-1.052188552188552E-5);
        mat1d[14][14] = sqrt(8.58E2)*1.156251156251156E-7;
    }

    if (degree >= 15) {
        mat1d[15][1] = 3.781534080237807E1*(-3.0/6.4E1);
        mat1d[15][3] = 3.781534080237807E1*(7.0/6.4E1);
        mat1d[15][5] = 3.781534080237807E1*(-2.1E1/3.2E2);
        mat1d[15][7] = 3.781534080237807E1*(1.0/6.4E1);
        mat1d[15][9] = 3.781534080237807E1*(-1.0/5.76E2);
        mat1d[15][11] = 3.781534080237807E1*9.46969696969697E-5;
        mat1d[15][13] = 3.781534080237807E1*(-2.428127428127428E-6);
        mat1d[15][15] = 3.781534080237807E1*2.312502312502313E-8;
    }

    if (degree >= 16) {
        mat1d[16][0] = 3.781534080237807E1*(3.0/2.56E2);
        mat1d[16][2] = 3.781534080237807E1*(-3.0/3.2E1);
        mat1d[16][4] = 3.781534080237807E1*(7.0/6.4E1);
        mat1d[16][6] = 3.781534080237807E1*(-7.0/1.6E2);
        mat1d[16][8] = 3.781534080237807E1*(1.0/1.28E2);
        mat1d[16][10] = 3.781534080237807E1*(-6.944444444444444E-4);
        mat1d[16][12] = 3.781534080237807E1*3.156565656565657E-5;
        mat1d[16][14] = 3.781534080237807E1*(-6.937506937506938E-7);
        mat1d[16][16] = 3.781534080237807E1*5.781255781255781E-9;
    }

    if (degree >= 17) {
        mat1d[17][1] = 1.559166443969341E2*(3.0/2.56E2);
        mat1d[17][3] = 1.559166443969341E2*(-1.0/3.2E1);
        mat1d[17][5] = 1.559166443969341E2*(7.0/3.2E2);
        mat1d[17][7] = 1.559166443969341E2*(-1.0/1.6E2);
        mat1d[17][9] = 1.559166443969341E2*8.680555555555556E-4;
        mat1d[17][11] = 1.559166443969341E2*(-6.313131313131313E-5);
        mat1d[17][13] = 1.559166443969341E2*2.428127428127428E-6;
        mat1d[17][15] = 1.559166443969341E2*(-4.625004625004625E-8);
        mat1d[17][17] = 1.559166443969341E2*3.400738694856342E-10;
    }

    if (degree >= 18) {
        mat1d[18][0] = 1.102497165529236E2*(-1.0/2.56E2);
        mat1d[18][2] = 1.102497165529236E2*(9.0/2.56E2);
        mat1d[18][4] = 1.102497165529236E2*(-3.0/6.4E1);
        mat1d[18][6] = 1.102497165529236E2*(7.0/3.2E2);
        mat1d[18][8] = 1.102497165529236E2*(-3.0/6.4E2);
        mat1d[18][10] = 1.102497165529236E2*5.208333333333333E-4;
        mat1d[18][12] = 1.102497165529236E2*(-3.156565656565657E-5);
        mat1d[18][14] = 1.102497165529236E2*1.040626040626041E-6;
        mat1d[18][16] = 1.102497165529236E2*(-1.734376734376734E-8);
        mat1d[18][18] = 1.102497165529236E2*1.133579564952114E-10;
    }

    if (degree >= 19) {
        mat1d[19][1] = 4.805673730081975E2*(-1.0/2.56E2);
        mat1d[19][3] = 4.805673730081975E2*(3.0/2.56E2);
        mat1d[19][5] = 4.805673730081975E2*(-3.0/3.2E2);
        mat1d[19][7] = 4.805673730081975E2*(1.0/3.2E2);
        mat1d[19][9] = 4.805673730081975E2*(-5.208333333333333E-4);
        mat1d[19][11] = 4.805673730081975E2*4.734848484848485E-5;
        mat1d[19][13] = 4.805673730081975E2*(-2.428127428127428E-6);
        mat1d[19][15] = 4.805673730081975E2*6.937506937506938E-8;
        mat1d[19][17] = 4.805673730081975E2*(-1.020221608456903E-9);
        mat1d[19][19] = 4.805673730081975E2*5.966208236590074E-12;
    }

    if (degree >= 20) {
        mat1d[20][0] = 2.149162627629654E2*(1.0/5.12E2);
        mat1d[20][2] = 2.149162627629654E2*(-5.0/2.56E2);
        mat1d[20][4] = 2.149162627629654E2*(1.5E1/5.12E2);
        mat1d[20][6] = 2.149162627629654E2*(-1.0/6.4E1);
        mat1d[20][8] = 2.149162627629654E2*(1.0/2.56E2);
        mat1d[20][10] = 2.149162627629654E2*(-5.208333333333333E-4);
        mat1d[20][12] = 2.149162627629654E2*3.945707070707071E-5;
        mat1d[20][14] = 2.149162627629654E2*(-1.734376734376734E-6);
        mat1d[20][16] = 2.149162627629654E2*4.335941835941836E-8;
        mat1d[20][18] = 2.149162627629654E2*(-5.66789782476057E-10);
        mat1d[20][20] = 2.149162627629654E2*2.983104118295037E-12;
    }

    if (degree >= 21) {
        mat1d[21][1] = 9.848700421883082E2*(1.0/5.12E2);
        mat1d[21][3] = 9.848700421883082E2*(-5.0/7.68E2);
        mat1d[21][5] = 9.848700421883082E2*(3.0/5.12E2);
        mat1d[21][7] = 9.848700421883082E2*(-1.0/4.48E2);
        mat1d[21][9] = 9.848700421883082E2*4.340277777777778E-4;
        mat1d[21][11] = 9.848700421883082E2*(-4.734848484848485E-5);
        mat1d[21][13] = 9.848700421883082E2*3.035159285159285E-6;
        mat1d[21][15] = 9.848700421883082E2*(-1.156251156251156E-7);
        mat1d[21][17] = 9.848700421883082E2*2.550554021142256E-9;
        mat1d[21][19] = 9.848700421883082E2*(-2.983104118295037E-11);
        mat1d[21][21] = 9.848700421883082E2*1.420525770616684E-13;
    }

    if (degree >= 22) {
        mat1d[22][0] = 4.199499970234552E2*(-9.765625E-4);
        mat1d[22][2] = 4.199499970234552E2*1.07421875E-2;
        mat1d[22][4] = 4.199499970234552E2*(-1.790364583333333E-2);
        mat1d[22][6] = 4.199499970234552E2*1.07421875E-2;
        mat1d[22][8] = 4.199499970234552E2*(-3.069196428571429E-3);
        mat1d[22][10] = 4.199499970234552E2*4.774305555555556E-4;
        mat1d[22][12] = 4.199499970234552E2*(-4.340277777777778E-5);
        mat1d[22][14] = 4.199499970234552E2*2.38476800976801E-6;
        mat1d[22][16] = 4.199499970234552E2*(-7.949226699226699E-8);
        mat1d[22][18] = 4.199499970234552E2*1.558671901809157E-9;
        mat1d[22][20] = 4.199499970234552E2*(-1.64070726506227E-11);
        mat1d[22][22] = 4.199499970234552E2*7.102628853083421E-14;
    }

    if (degree >= 23) {
        mat1d[23][1] = 2.014009433940169E3*(-9.765625E-4);
        mat1d[23][3] = 2.014009433940169E3*3.580729166666667E-3;
        mat1d[23][5] = 2.014009433940169E3*(-3.580729166666667E-3);
        mat1d[23][7] = 2.014009433940169E3*1.534598214285714E-3;
        mat1d[23][9] = 2.014009433940169E3*(-3.410218253968254E-4);
        mat1d[23][11] = 2.014009433940169E3*4.340277777777778E-5;
        mat1d[23][13] = 2.014009433940169E3*(-3.338675213675214E-6);
        mat1d[23][15] = 2.014009433940169E3*1.58984533984534E-7;
        mat1d[23][17] = 2.014009433940169E3*(-4.67601570542747E-9);
        mat1d[23][19] = 2.014009433940169E3*8.203536325311351E-11;
        mat1d[23][21] = 2.014009433940169E3*(-7.812891738391763E-13);
        mat1d[23][23] = 2.014009433940169E3*3.088099501340618E-15;
    }

    if (degree >= 24) {
        mat1d[24][0] = 8.222159083841665E2*4.8828125E-4;
        mat1d[24][2] = 8.222159083841665E2*(-3.0/5.12E2);
        mat1d[24][4] = 8.222159083841665E2*1.07421875E-2;
        mat1d[24][6] = 8.222159083841665E2*(-7.161458333333333E-3);
        mat1d[24][8] = 8.222159083841665E2*2.301897321428571E-3;
        mat1d[24][10] = 8.222159083841665E2*(-4.092261904761905E-4);
        mat1d[24][12] = 8.222159083841665E2*4.340277777777778E-5;
        mat1d[24][14] = 8.222159083841665E2*(-2.861721611721612E-6);
        mat1d[24][16] = 8.222159083841665E2*1.192384004884005E-7;
        mat1d[24][18] = 8.222159083841665E2*(-3.117343803618313E-9);
        mat1d[24][20] = 8.222159083841665E2*4.922121795186811E-11;
        mat1d[24][22] = 8.222159083841665E2*(-4.261577311850053E-13);
        mat1d[24][24] = 8.222159083841665E2*1.544049750670309E-15;
    }

    if (degree >= 25) {
        mat1d[25][1] = 8.222159083841665E2*2.44140625E-3;
        mat1d[25][3] = 8.222159083841665E2*(-5.0/5.12E2);
        mat1d[25][5] = 8.222159083841665E2*1.07421875E-2;
        mat1d[25][7] = 8.222159083841665E2*(-5.115327380952381E-3);
        mat1d[25][9] = 8.222159083841665E2*1.278831845238095E-3;
        mat1d[25][11] = 8.222159083841665E2*(-1.860119047619048E-4);
        mat1d[25][13] = 8.222159083841665E2*1.669337606837607E-5;
        mat1d[25][15] = 8.222159083841665E2*(-9.539072039072039E-7);
        mat1d[25][17] = 8.222159083841665E2*3.507011779070603E-8;
        mat1d[25][19] = 8.222159083841665E2*(-8.203536325311351E-10);
        mat1d[25][21] = 8.222159083841665E2*1.171933760758764E-11;
        mat1d[25][23] = 8.222159083841665E2*(-9.264298504021853E-14);
        mat1d[25][25] = 8.222159083841665E2*3.088099501340618E-16;
    }

    if (degree >= 26) {
        mat1d[26][0] = 3.224996124028679E2*(-1.220703125E-3);
        mat1d[26][2] = 3.224996124028679E2*1.5869140625E-2;
        mat1d[26][4] = 3.224996124028679E2*(-3.173828125E-2);
        mat1d[26][6] = 3.224996124028679E2*2.327473958333333E-2;
        mat1d[26][8] = 3.224996124028679E2*(-8.312406994047619E-3);
        mat1d[26][10] = 3.224996124028679E2*1.662481398809524E-3;
        mat1d[26][12] = 3.224996124028679E2*(-2.015128968253968E-4);
        mat1d[26][14] = 3.224996124028679E2*1.550099206349206E-5;
        mat1d[26][16] = 3.224996124028679E2*(-7.750496031746032E-7);
        mat1d[26][18] = 3.224996124028679E2*2.53284184043988E-8;
        mat1d[26][20] = 3.224996124028679E2*(-5.332298611452378E-10);
        mat1d[26][22] = 3.224996124028679E2*6.925063131756335E-12;
        mat1d[26][24] = 3.224996124028679E2*(-5.018161689678504E-14);
        mat1d[26][26] = 3.224996124028679E2*1.544049750670309E-16;
    }

    if (degree >= 27) {
        mat1d[27][1] = 5.585857141030372E2*(-3.662109375E-3);
        mat1d[27][3] = 5.585857141030372E2*1.5869140625E-2;
        mat1d[27][5] = 5.585857141030372E2*(-1.904296875E-2);
        mat1d[27][7] = 5.585857141030372E2*9.974888392857143E-3;
        mat1d[27][9] = 5.585857141030372E2*(-2.770802331349206E-3);
        mat1d[27][11] = 5.585857141030372E2*4.534040178571429E-4;
        mat1d[27][13] = 5.585857141030372E2*(-4.650297619047619E-5);
        mat1d[27][15] = 5.585857141030372E2*3.100198412698413E-6;
        mat1d[27][17] = 5.585857141030372E2*(-1.367734593837535E-7);
        mat1d[27][19] = 5.585857141030372E2*3.999223958589284E-9;
        mat1d[27][21] = 5.585857141030372E2*(-7.617569444931969E-11);
        mat1d[27][23] = 5.585857141030372E2*9.032691041421307E-13;
        mat1d[27][25] = 5.585857141030372E2*(-6.021794027614205E-15);
        mat1d[27][27] = 5.585857141030372E2*1.715610834078121E-17;
    }

    if (degree >= 28) {
        mat1d[28][0] = 2.111255550614373E2*1.8310546875E-3;
        mat1d[28][2] = 2.111255550614373E2*(-2.5634765625E-2);
        mat1d[28][4] = 2.111255550614373E2*5.55419921875E-2;
        mat1d[28][6] = 2.111255550614373E2*(-4.443359375E-2);
        mat1d[28][8] = 2.111255550614373E2*1.74560546875E-2;
        mat1d[28][10] = 2.111255550614373E2*(-3.879123263888889E-3);
        mat1d[28][12] = 2.111255550614373E2*5.289713541666667E-4;
        mat1d[28][14] = 2.111255550614373E2*(-4.650297619047619E-5);
        mat1d[28][16] = 2.111255550614373E2*2.712673611111111E-6;
        mat1d[28][18] = 2.111255550614373E2*(-1.063793572984749E-7);
        mat1d[28][20] = 2.111255550614373E2*2.799456771012499E-9;
        mat1d[28][22] = 2.111255550614373E2*(-4.847544192229435E-11);
        mat1d[28][24] = 2.111255550614373E2*5.269069774162429E-13;
        mat1d[28][26] = 2.111255550614373E2*(-3.242504476407649E-15);
        mat1d[28][28] = 2.111255550614373E2*8.578054170390605E-18;
    }

    if (degree >= 29) {
        mat1d[29][1] = 1.13694590900359E3*1.8310546875E-3;
        mat1d[29][3] = 1.13694590900359E3*(-8.544921875E-3);
        mat1d[29][5] = 1.13694590900359E3*1.11083984375E-2;
        mat1d[29][7] = 1.13694590900359E3*(-6.34765625E-3);
        mat1d[29][9] = 1.13694590900359E3*1.939561631944444E-3;
        mat1d[29][11] = 1.13694590900359E3*(-3.526475694444444E-4);
        mat1d[29][13] = 1.13694590900359E3*4.069010416666667E-5;
        mat1d[29][15] = 1.13694590900359E3*(-3.100198412698413E-6);
        mat1d[29][17] = 1.13694590900359E3*1.595690359477124E-7;
        mat1d[29][19] = 1.13694590900359E3*(-5.598913542024997E-9);
        mat1d[29][21] = 1.13694590900359E3*1.333074652863095E-10;
        mat1d[29][23] = 1.13694590900359E3*(-2.107627909664972E-12);
        mat1d[29][25] = 1.13694590900359E3*2.107627909664972E-14;
        mat1d[29][27] = 1.13694590900359E3*(-1.200927583854685E-16);
        mat1d[29][29] = 1.13694590900359E3*2.957949713927795E-19;
    }

    if (degree >= 30) {
        mat1d[30][0] = 1.037884868374137E3*(-3.662109375E-4);
        mat1d[30][2] = 1.037884868374137E3*5.4931640625E-3;
        mat1d[30][4] = 1.037884868374137E3*(-1.28173828125E-2);
        mat1d[30][6] = 1.037884868374137E3*1.11083984375E-2;
        mat1d[30][8] = 1.037884868374137E3*(-4.7607421875E-3);
        mat1d[30][10] = 1.037884868374137E3*1.163736979166667E-3;
        mat1d[30][12] = 1.037884868374137E3*(-1.763237847222222E-4);
        mat1d[30][14] = 1.037884868374137E3*1.743861607142857E-5;
        mat1d[30][16] = 1.037884868374137E3*(-1.162574404761905E-6);
        mat1d[30][18] = 1.037884868374137E3*5.318967864923747E-8;
        mat1d[30][20] = 1.037884868374137E3*(-1.679674062607499E-9);
        mat1d[30][22] = 1.037884868374137E3*3.635658144172076E-11;
        mat1d[30][24] = 1.037884868374137E3*(-5.269069774162429E-13);
        mat1d[30][26] = 1.037884868374137E3*4.863756714611473E-15;
        mat1d[30][28] = 1.037884868374137E3*(-2.573416251117181E-17);
        mat1d[30][30] = 1.037884868374137E3*5.91589942785559E-20;
    }


    if (degree >= 31) {
        mat1d[31][1] = 5.778698382854049E3*(-3.662109375E-4);
        mat1d[31][3] = 5.778698382854049E3*1.8310546875E-3;
        mat1d[31][5] = 5.778698382854049E3*(-2.5634765625E-3);
        mat1d[31][7] = 5.778698382854049E3*1.5869140625E-3;
        mat1d[31][9] = 5.778698382854049E3*(-5.289713541666667E-4);
        mat1d[31][11] = 5.778698382854049E3*1.057942708333333E-4;
        mat1d[31][13] = 5.778698382854049E3*(-1.356336805555556E-5);
        mat1d[31][15] = 5.778698382854049E3*1.162574404761905E-6;
        mat1d[31][17] = 5.778698382854049E3*(-6.838672969187675E-8);
        mat1d[31][19] = 5.778698382854049E3*2.799456771012499E-9;
        mat1d[31][21] = 5.778698382854049E3*(-7.998447917178567E-11);
        mat1d[31][23] = 5.778698382854049E3*1.580720932248729E-12;
        mat1d[31][25] = 5.778698382854049E3*(-2.107627909664972E-14);
        mat1d[31][27] = 5.778698382854049E3*1.801391375782027E-16;
        mat1d[31][29] = 5.778698382854049E3*(-8.873849141783384E-19);
        mat1d[31][31] = 5.778698382854049E3*1.908354654146964E-21;
    }

    if (degree >= 32) {
        mat1d[32][0] = 8.172313625895668E3*4.57763671875E-5;
        mat1d[32][2] = 8.172313625895668E3*(-7.32421875E-4);
        mat1d[32][4] = 8.172313625895668E3*1.8310546875E-3;
        mat1d[32][6] = 8.172313625895668E3*(-1.708984375E-3);
        mat1d[32][8] = 8.172313625895668E3*7.9345703125E-4;
        mat1d[32][10] = 8.172313625895668E3*(-2.115885416666667E-4);
        mat1d[32][12] = 8.172313625895668E3*3.526475694444444E-5;
        mat1d[32][14] = 8.172313625895668E3*(-3.875248015873016E-6);
        mat1d[32][16] = 8.172313625895668E3*2.906436011904762E-7;
        mat1d[32][18] = 8.172313625895668E3*(-1.519705104263928E-8);
        mat1d[32][20] = 8.172313625895668E3*5.598913542024997E-10;
        mat1d[32][22] = 8.172313625895668E3*(-1.45426325766883E-11);
        mat1d[32][24] = 8.172313625895668E3*2.634534887081215E-13;
        mat1d[32][26] = 8.172313625895668E3*(-3.242504476407649E-15);
        mat1d[32][28] = 8.172313625895668E3*2.573416251117181E-17;
        mat1d[32][30] = 8.172313625895668E3*(-1.183179885571118E-19);
        mat1d[32][32] = 8.172313625895668E3*2.385443317683705E-22;
    }

    if (degree >= 33) {
        mat1d[33][1] = 4.694636759111401E4*4.57763671875E-5;
        mat1d[33][3] = 4.694636759111401E4*(-2.44140625E-4);
        mat1d[33][5] = 4.694636759111401E4*3.662109375E-4;
        mat1d[33][7] = 4.694636759111401E4*(-2.44140625E-4);
        mat1d[33][9] = 4.694636759111401E4*8.816189236111111E-5;
        mat1d[33][11] = 4.694636759111401E4*(-1.923532196969697E-5);
        mat1d[33][13] = 4.694636759111401E4*2.712673611111111E-6;
        mat1d[33][15] = 4.694636759111401E4*(-2.583498677248677E-7);
        mat1d[33][17] = 4.694636759111401E4*1.709668242296919E-8;
        mat1d[33][19] = 4.694636759111401E4*(-7.998447917178567E-10);
        mat1d[33][21] = 4.694636759111401E4*2.666149305726189E-11;
        mat1d[33][23] = 4.694636759111401E4*(-6.322883728994915E-13);
        mat1d[33][25] = 4.694636759111401E4*1.053813954832486E-14;
        mat1d[33][27] = 4.694636759111401E4*(-1.200927583854685E-16);
        mat1d[33][29] = 4.694636759111401E4*8.873849141783384E-19;
        mat1d[33][31] = 4.694636759111401E4*(-3.816709308293929E-21);
        mat1d[33][33] = 4.694636759111401E4*7.228616114193047E-24;
    }

    if (degree >= 34) {
        mat1d[34][0] = 8.051235619456184E3*(-4.57763671875E-5);
        mat1d[34][2] = 8.051235619456184E3*7.781982421875E-4;
        mat1d[34][4] = 8.051235619456184E3*(-2.0751953125E-3);
        mat1d[34][6] = 8.051235619456184E3*2.0751953125E-3;
        mat1d[34][8] = 8.051235619456184E3*(-1.03759765625E-3);
        mat1d[34][10] = 8.051235619456184E3*2.997504340277778E-4;
        mat1d[34][12] = 8.051235619456184E3*(-5.450007891414141E-5);
        mat1d[34][14] = 8.051235619456184E3*6.587921626984127E-6;
        mat1d[34][16] = 8.051235619456184E3*(-5.489934689153439E-7);
        mat1d[34][18] = 8.051235619456184E3*3.229373346560847E-8;
        mat1d[34][20] = 8.051235619456184E3*(-1.359736145920356E-9);
        mat1d[34][22] = 8.051235619456184E3*4.12041256339502E-11;
        mat1d[34][24] = 8.051235619456184E3*(-8.957418616076129E-13);
        mat1d[34][26] = 8.051235619456184E3*1.378064402473251E-14;
        mat1d[34][28] = 8.051235619456184E3*(-1.458269208966403E-16);
        mat1d[34][30] = 8.051235619456184E3*1.00570290273545E-18;
        mat1d[34][32] = 8.051235619456184E3*(-4.055253640062299E-21);
        mat1d[34][34] = 8.051235619456184E3*7.228616114193047E-24;
    }

    if (degree >= 35) {
        mat1d[35][1] = 9.526350455447249E3*(-2.288818359375E-4);
        mat1d[35][3] = 9.526350455447249E3*1.2969970703125E-3;
        mat1d[35][5] = 9.526350455447249E3*(-2.0751953125E-3);
        mat1d[35][7] = 9.526350455447249E3*1.482282366071429E-3;
        mat1d[35][9] = 9.526350455447249E3*(-5.764431423611111E-4);
        mat1d[35][11] = 9.526350455447249E3*1.362501972853535E-4;
        mat1d[35][13] = 9.526350455447249E3*(-2.096156881313131E-5);
        mat1d[35][15] = 9.526350455447249E3*2.195973875661376E-6;
        mat1d[35][17] = 9.526350455447249E3*(-1.614686673280423E-7);
        mat1d[35][19] = 9.526350455447249E3*8.498350912002228E-9;
        mat1d[35][21] = 9.526350455447249E3*(-3.237467014096087E-10);
        mat1d[35][23] = 9.526350455447249E3*8.957418616076129E-12;
        mat1d[35][25] = 9.526350455447249E3*(-1.791483723215226E-13);
        mat1d[35][27] = 9.526350455447249E3*2.551971115691205E-15;
        mat1d[35][29] = 9.526350455447249E3*(-2.514257256838626E-17);
        mat1d[35][31] = 9.526350455447249E3*1.62210145602492E-19;
        mat1d[35][33] = 9.526350455447249E3*(-6.14432369706409E-22);
        mat1d[35][35] = 9.526350455447249E3*1.032659444884721E-24;
    }

    if (degree >= 36) {
        mat1d[36][0] = 9.526350455447249E3*3.814697265625E-5;
        mat1d[36][2] = 9.526350455447249E3*(-6.866455078125E-4);
        mat1d[36][4] = 9.526350455447249E3*1.94549560546875E-3;
        mat1d[36][6] = 9.526350455447249E3*(-2.0751953125E-3);
        mat1d[36][8] = 9.526350455447249E3*1.111711774553571E-3;
        mat1d[36][10] = 9.526350455447249E3*(-3.458658854166667E-4);
        mat1d[36][12] = 9.526350455447249E3*6.812509864267677E-5;
        mat1d[36][14] = 9.526350455447249E3*(-8.983529491341991E-6);
        mat1d[36][16] = 9.526350455447249E3*8.234902033730159E-7;
        mat1d[36][18] = 9.526350455447249E3*(-5.382288910934744E-8);
        mat1d[36][20] = 9.526350455447249E3*2.549505273600668E-9;
        mat1d[36][22] = 9.526350455447249E3*(-8.829455492989328E-11);
        mat1d[36][24] = 9.526350455447249E3*2.239354654019032E-12;
        mat1d[36][26] = 9.526350455447249E3*(-4.134193207419752E-14);
        mat1d[36][28] = 9.526350455447249E3*5.468509533624011E-16;
        mat1d[36][30] = 9.526350455447249E3*(-5.028514513677251E-18);
        mat1d[36][32] = 9.526350455447249E3*3.041440230046724E-20;
        mat1d[36][34] = 9.526350455447249E3*(-1.084292417128957E-22);
        mat1d[36][36] = 9.526350455447249E3*1.721099074807868E-25;
    }

    if (degree >= 37) {
        mat1d[37][1] = 5.79465276008839E4*3.814697265625E-5;
        mat1d[37][3] = 5.79465276008839E4*(-2.288818359375E-4);
        mat1d[37][5] = 5.79465276008839E4*3.8909912109375E-4;
        mat1d[37][7] = 5.79465276008839E4*(-2.964564732142857E-4);
        mat1d[37][9] = 5.79465276008839E4*1.235235305059524E-4;
        mat1d[37][11] = 5.79465276008839E4*(-3.144235321969697E-5);
        mat1d[37][13] = 5.79465276008839E4*5.240392203282828E-6;
        mat1d[37][15] = 5.79465276008839E4*(-5.989019660894661E-7);
        mat1d[37][17] = 5.79465276008839E4*4.84406001984127E-8;
        mat1d[37][19] = 5.79465276008839E4*(-2.832783637334076E-9);
        mat1d[37][21] = 5.79465276008839E4*1.214050130286033E-10;
        mat1d[37][23] = 5.79465276008839E4*(-3.838893692604055E-12);
        mat1d[37][25] = 5.79465276008839E4*8.957418616076129E-14;
        mat1d[37][27] = 5.79465276008839E4*(-1.531182669414723E-15);
        mat1d[37][29] = 5.79465276008839E4*1.885692942628969E-17;
        mat1d[37][31] = 5.79465276008839E4*(-1.62210145602492E-19);
        mat1d[37][33] = 5.79465276008839E4*9.216485545596135E-22;
        mat1d[37][35] = 5.79465276008839E4*(-3.097978334654163E-24);
        mat1d[37][37] = 5.79465276008839E4*4.651619121102347E-27;
    }

    if (degree >= 38) {
        mat1d[38][0] = 1.880033611401669E4*(-1.9073486328125E-5);
        mat1d[38][2] = 1.880033611401669E4*3.62396240234375E-4;
        mat1d[38][4] = 1.880033611401669E4*(-1.087188720703125E-3);
        mat1d[38][6] = 1.880033611401669E4*1.232147216796875E-3;
        mat1d[38][8] = 1.880033611401669E4*(-7.040841238839286E-4);
        mat1d[38][10] = 1.880033611401669E4*2.346947079613095E-4;
        mat1d[38][12] = 1.880033611401669E4*(-4.978372593118687E-5);
        mat1d[38][14] = 1.880033611401669E4*7.11196084731241E-6;
        mat1d[38][16] = 1.880033611401669E4*(-7.11196084731241E-7);
        mat1d[38][18] = 1.880033611401669E4*5.113174465388007E-8;
        mat1d[38][20] = 1.880033611401669E4*(-2.691144455467372E-9);
        mat1d[38][22] = 1.880033611401669E4*1.048497839792483E-10;
        mat1d[38][24] = 1.880033611401669E4*(-3.039124173311544E-12);
        mat1d[38][26] = 1.880033611401669E4*6.545805911747941E-14;
        mat1d[38][28] = 1.880033611401669E4*(-1.039016811388562E-15);
        mat1d[38][30] = 1.880033611401669E4*1.194272196998347E-17;
        mat1d[38][32] = 1.880033611401669E4*(-9.631227395147961E-20);
        mat1d[38][34] = 1.880033611401669E4*5.150388981362546E-22;
        mat1d[38][36] = 1.880033611401669E4*(-1.635044121067475E-24);
        mat1d[38][38] = 1.880033611401669E4*2.325809560551173E-27;
    }

    if (degree >= 39) {
        mat1d[39][1] = 3.913602046708377E4*(-5.7220458984375E-5);
        mat1d[39][3] = 3.913602046708377E4*3.62396240234375E-4;
        mat1d[39][5] = 3.913602046708377E4*(-6.52313232421875E-4);
        mat1d[39][7] = 3.913602046708377E4*5.280630929129464E-4;
        mat1d[39][9] = 3.913602046708377E4*(-2.346947079613095E-4);
        mat1d[39][11] = 3.913602046708377E4*6.400764762581169E-5;
        mat1d[39][13] = 3.913602046708377E4*(-1.14885521379662E-5);
        mat1d[39][15] = 3.913602046708377E4*1.422392169462482E-6;
        mat1d[39][17] = 3.913602046708377E4*(-1.255051914231602E-7);
        mat1d[39][19] = 3.913602046708377E4*8.073433366402116E-9;
        mat1d[39][21] = 3.913602046708377E4*(-3.844492079239103E-10);
        mat1d[39][23] = 3.913602046708377E4*1.367605877990195E-11;
        mat1d[39][25] = 3.913602046708377E4*(-3.646949007973853E-13);
        mat1d[39][27] = 3.913602046708377E4*7.273117679719934E-15;
        mat1d[39][29] = 3.913602046708377E4*(-1.074844977298512E-16);
        mat1d[39][31] = 3.913602046708377E4*1.155747287417755E-18;
        mat1d[39][33] = 3.913602046708377E4*(-8.755661268316328E-21);
        mat1d[39][35] = 3.913602046708377E4*4.414619126882182E-23;
        mat1d[39][37] = 3.913602046708377E4*(-1.325711449514169E-25);
        mat1d[39][39] = 3.913602046708377E4*1.789084277347056E-28;
    }

    if (degree >= 40) {
        mat1d[40][0] = 6.187948161547574E4*5.7220458984375E-6;
        mat1d[40][2] = 6.187948161547574E4*(-1.1444091796875E-4);
        mat1d[40][4] = 6.187948161547574E4*3.62396240234375E-4;
        mat1d[40][6] = 6.187948161547574E4*(-4.3487548828125E-4);
        mat1d[40][8] = 6.187948161547574E4*2.640315464564732E-4;
        mat1d[40][10] = 6.187948161547574E4*(-9.387788318452381E-5);
        mat1d[40][12] = 6.187948161547574E4*2.133588254193723E-5;
        mat1d[40][14] = 6.187948161547574E4*(-3.282443467990343E-6);
        mat1d[40][16] = 6.187948161547574E4*3.555980423656205E-7;
        mat1d[40][18] = 6.187948161547574E4*(-2.789004253848004E-8);
        mat1d[40][20] = 6.187948161547574E4*1.614686673280423E-9;
        mat1d[40][22] = 6.187948161547574E4*(-6.989985598616551E-11);
        mat1d[40][24] = 6.187948161547574E4*2.279343129983658E-12;
        mat1d[40][26] = 6.187948161547574E4*(-5.610690781498235E-14);
        mat1d[40][28] = 6.187948161547574E4*1.039016811388562E-15;
        mat1d[40][30] = 6.187948161547574E4*(-1.433126636398017E-17);
        mat1d[40][32] = 6.187948161547574E4*1.444684109272194E-19;
        mat1d[40][34] = 6.187948161547574E4*(-1.030077796272509E-21);
        mat1d[40][36] = 6.187948161547574E4*4.905132363202425E-24;
        mat1d[40][38] = 6.187948161547574E4*(-1.395485736330704E-26);
        mat1d[40][40] = 6.187948161547574E4*1.789084277347056E-29;
    }

    if (degree > 40) {
        cout << "Degree too high" << endl; exit(0);
    }

    int nb = bin(degree + n_vars, n_vars);
    vector< vector<double> > matnd(nb, vector<double>(nb,0.));
    for (int i = 0; i < nb; ++i) {
        vector<int> m1 = ind2mult(i,degree,n_vars);
        for (int j = 0; j < nb; ++j) {
            vector<int> m2 = ind2mult(j,degree,n_vars);
            matnd[i][j] = 1.;
            for (int k = 0; k < n_vars; ++k) {
                matnd[i][j] *= mat1d[m1[k]][m2[k]];
            }
        }
    }

    this->hermiteCoeffs_1d = mat1d;
    this->hermiteCoeffs_nd = matnd;

    if ( poly_basis == "MONOMIAL" ) {
        this->herm_to_basis = this->hermiteCoeffs_nd;
    }
    else if ( poly_basis == "HERMITE" ) {
        this->herm_to_basis = vector< vector<double> > (nb, vector<double>(nb,0.));
        for (int i = 0; i < nb; ++i) {
            for (int j = 0; j < nb; ++j) {
                this->herm_to_basis[i][j] = (i == j);
            }
        }
    }
    else {
        cout << "Invalid basis of polynomials. " << endl;
        exit(0);
    }
}

int Solver_spectral::mult2ind(vector<int> m, int d) {
    int l = m.size() - 1; int i;
    for(i = l; (i > 0) & (m[i] == 0); i--);

    if ((i == 0) & (m[0] == 0))
        return 0;

    int s = 0;
    for (unsigned int j = 0; j < m.size(); ++j)
        s += m[j];

    int dr = d - s;
    int vr = l - i;
    m[i] = m[i] - 1;
    return bin(dr + vr + 1, vr) + mult2ind(m, d);
}

vector<int> Solver_spectral::ind2mult(int ind, int d, int n) {
    vector<int> m(n,0); int s = 0;
    for (int i = 0; i < ind; ++i) {
        if (s < d) {
            m[n-1] ++; s++;
        } else {
            int j; for(j = n-1; m[j] == 0; j--);
            s -= m[j]; m[j] = 0; m[j-1] ++; s ++;
        }
    }
    return m;
}

vector<double> Solver_spectral::basis2herm (vector<double> bcoeffs, int n, int d) {
    vector<double> result(bcoeffs.size(), 0.);
    for (unsigned int i = 0; i < bcoeffs.size(); ++i) {
        for (unsigned int j = 0; j <= i; ++j) {
            result[i] += herm_to_basis[i][j] * bcoeffs[j];
        }
    }
    return result;
}
