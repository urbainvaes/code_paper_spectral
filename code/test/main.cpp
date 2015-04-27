// Includes
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <time.h>
#include <functional>

#define PI 3.141592653589793238463

// Namespace
using namespace std;

// Weights for the integration against the weight e^(-x^2)
static vector<double> nodes = {-6.863345293529891581061, -6.138279220123934620395, -5.533147151567495725118, -4.988918968589943944486, -4.483055357092518341887, -4.003908603861228815228, -3.544443873155349886925, -3.099970529586441748689, -2.667132124535617200571, -2.243391467761504072473, -1.826741143603688038836, -1.415527800198188511941, -1.008338271046723461805, -0.6039210586255523077782, -0.2011285765488714855458, 0.2011285765488714855458, 0.6039210586255523077782, 1.008338271046723461805, 1.415527800198188511941, 1.826741143603688038836, 2.243391467761504072473, 2.667132124535617200571, 3.099970529586441748689, 3.544443873155349886925, 4.003908603861228815228, 4.483055357092518341887, 4.988918968589943944486, 5.533147151567495725118, 6.138279220123934620395, 6.863345293529891581061};
static vector<double> weights = {2.90825470013122622941E-21, 2.8103336027509037088E-17, 2.87860708054870606219E-14, 8.10618629746304420399E-12, 9.178580424378528209E-10, 5.10852245077594627739E-8, 1.57909488732471028835E-6, 2.9387252289229876415E-5, 3.48310124318685523421E-4, 0.00273792247306765846299, 0.01470382970482668351528, 0.05514417687023425116808, 0.1467358475408900997517, 0.2801309308392126674135, 0.3863948895418138625556, 0.3863948895418138625556, 0.2801309308392126674135, 0.1467358475408900997517, 0.05514417687023425116808, 0.01470382970482668351528, 0.00273792247306765846299, 3.48310124318685523421E-4, 2.9387252289229876415E-5, 1.57909488732471028835E-6, 5.10852245077594627739E-8, 9.178580424378528209E-10, 8.10618629746304420399E-12, 2.87860708054870606219E-14, 2.8103336027509037088E-17, 2.90825470013122622941E-21};

double gauss_hermite_1D(function<double (double)> f, double sigma) {
    double result = 0.;
    double x_aux;
    for (int i = 0; i < nodes.size(); ++i) {
        x_aux = sqrt(2)*sigma*nodes[i];
        result += weights[i]*f(x_aux);
    }
    return result/sqrt(PI);
}

double gauss_hermite_nD(function<double (vector<double>)> f, vector<double> sigmas) {
    double result = 0.;
    int size = sigmas.size();
    if (size == 1) {
        auto lambda = [f](double x) -> double {
            vector<double> aux_vec(1,x);
            return f(aux_vec);
        };
        return gauss_hermite_1D(lambda, sigmas[0]);
    }
    else {
        auto lambda = [f,size,sigmas](vector<double> x) -> double {
            auto nested_lambda = [f,&x](double y) -> double {
                x.push_back(y);
                double result = f(x);
                x.pop_back();
                return result;
            };
            return gauss_hermite_1D(nested_lambda, sigmas[size-1]);
        };
        sigmas.pop_back();
        return gauss_hermite_nD(lambda, sigmas);
    }
    return result;
}

int main(int argc, char *argv[])
{
    auto lambda = [](vector<double> x) -> double {return x[0]*x[0]*x[0]*x[0] + x[1]*x[1];};
    vector<double> sigmas = {3.0, 2.0};
    cout << gauss_hermite_nD(lambda, sigmas) << endl;;
    return 0;
}
