// vim: syntax=cpp

#pragma once

#include <type_traits>
#include <vector>
#include <string>
#include <map>

// ROOT
#include <Rtypes.h>
#include <TH1.h>
#include <TH2.h>
#include <TH3.h>
#include <TChain.h>
#include <TTreeFormula.h>
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

struct Dataset {
    std::string name;
    std::string db_name;
    std::string output_name;
    std::string tree_name;
    std::string path;
    std::vector<std::string> files;
    std::string cut;
    std::string sample_weight_key;
    uint64_t event_start = 0;
    uint64_t event_end;
    bool event_end_filled = false;
};

class Skimmer {
    public:
        Skimmer(const Dataset& dataset, ROOT::TreeWrapper& tree, TChain* raw_tree):
            m_dataset(dataset), tree(tree), raw_tree(raw_tree) {
                if(dataset.cut != "" && dataset.cut != "1"){
                    m_sample_cut = new TTreeFormula("sample_cut", dataset.cut.c_str(), raw_tree);
                    raw_tree->SetNotify(m_sample_cut);
                }
                
                tree.setEntry(dataset.event_start);
                tree.stopAt(dataset.event_end);
            }
        virtual ~Skimmer() {};

        void skim(const std::string&);

    private:
        
        double getSampleWeight();

        Dataset m_dataset;
        ROOT::TreeWrapper& tree;
        TTreeFormula* m_sample_cut;
        TChain* raw_tree;

        // List of input branches
        {{BRANCHES}}
};
