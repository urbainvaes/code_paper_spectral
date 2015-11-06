#ifndef GLOBAL_H
#define GLOBAL_H

#define VERBOSE 0
#define SUMMARY 1
#define DEBUG 0

#include<vector>

// Definition of types
namespace std {
    typedef std::vector< std::vector< std::vector<double> > > cube;
    typedef std::vector< std::vector<double> > mat;
    typedef std::vector<double> vec;
}

// Structures
struct SDE_coeffs {
    std::vector<double> drif;
    std::vector< std::vector<double> > diff;
};

#endif
