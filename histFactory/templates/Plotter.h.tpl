// vim: syntax=cpp

#pragma once

#include <Rtypes.h>
#include <vector>
#include <string>
#include <map>
#include <Math/Vector4D.h>

// Generated automatically
#include <classes.h>

#include <cp3_llbb/TreeWrapper/interface/TreeWrapper.h>

// No other choices, as ROOT strips the 'std' namespace from types...
using namespace std;

template<typename T>
size_t Length$(const T& t) {
    return t.size();
}

class Plotter {
    public:
        Plotter(ROOT::TreeWrapper& tree):
            tree(tree) {};
        virtual ~Plotter() {};

        void plot(const std::string&);

    private:
        // List of branches
        ROOT::TreeWrapper& tree;

        {{BRANCHES}}
};
