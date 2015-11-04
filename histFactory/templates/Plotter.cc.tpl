// vim: syntax=cpp

#include <Plotter.h>

#include <memory>
#include <iostream>

#include <TChain.h>
#include <TH1F.h>
#include <TDirectory.h>
#include <TFile.h>

void Plotter::plot() {
    std::cout << "Plotting" << std::endl;

    {{HISTS_DECLARATION}}

    size_t index = 1;
    while (tree.next()) {

        if ((index - 1) % 1000 == 0)
            std::cout << "Processing entry " << index << " of " << tree.getEntries() << std::endl;

        double __weight = 0;

        {{PLOTS}}

        index++;
    }

    TDirectory* plots_directory = gDirectory;

    {{SAVE_PLOTS}}
}

int main(int argc, char** argv) {

    std::unique_ptr<TChain> t(new TChain("t"));
    {{FILES}}

    ROOT::TreeWrapper wrapped_tree(t.get());

    Plotter p(wrapped_tree);
    p.plot();
}
