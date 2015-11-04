// Read some trees, output an histos
// A. Mertens (September 2015)

#include <Python.h>

#include <json/json.h>

#include <iostream>
#include <fstream>
#include <memory>
#include <cstdio>

#include <TVirtualTreePlayer.h>
#include <TMultiDrawTreePlayer.h>
#include <TChain.h>
#include <TTree.h>
#include <TFile.h>
#include <TApplication.h>
#include <TObject.h>  // For kWriteDelete

// Ugly hack to access list of leaves in the formula
#define protected public
#include <TTreeFormula.h>
#undef protected

#include <uuid/uuid.h>

#include <tclap/CmdLine.h>

#include <ctemplate/template.h>

struct Branch {
    std::string name;
    std::string type;
};

struct Plot {
    std::string name;
    std::string variable;
    std::string plot_cut;
    std::string binning;

    std::shared_ptr<TTreeFormula> var;
    std::shared_ptr<TTreeFormula> selector;
};

struct Run;

struct Dataset {
    std::string name;
    std::string db_name;
    std::string output_name;
    std::string tree_name;
    std::string path;
    std::vector<std::string> files;
    std::string cut;

    std::vector<Run> runs;

    PyObject* toPyObject() {
        PyObject* o = PyDict_New();

        PyDict_SetItemString(o, "name", PyString_FromString(name.c_str()));
        PyDict_SetItemString(o, "db_name", PyString_FromString(db_name.c_str()));
        PyDict_SetItemString(o, "output_name", PyString_FromString(output_name.c_str()));
        PyDict_SetItemString(o, "tree_name", PyString_FromString(tree_name.c_str()));
        PyDict_SetItemString(o, "path", PyString_FromString(tree_name.c_str()));
        PyDict_SetItemString(o, "cut", PyString_FromString(cut.c_str()));

        return o;
    }

    void fromPyObject(PyObject* o) {
        name = PyString_AsString(PyDict_GetItemString(o, "name"));
        db_name = PyString_AsString(PyDict_GetItemString(o, "db_name"));
        output_name = PyString_AsString(PyDict_GetItemString(o, "output_name"));
        tree_name = PyString_AsString(PyDict_GetItemString(o, "tree_name"));
        path = PyString_AsString(PyDict_GetItemString(o, "path"));
        cut = PyString_AsString(PyDict_GetItemString(o, "cut"));
    }
};

struct Run {
    Dataset dataset;
    std::map<std::string, std::string> options;
    std::vector<Plot> plots;

    // Key is unique name, value is plot name
    std::map<std::string, std::string> unique_names;
};

#define CHECK_AND_GET(var, obj) if (PyDict_Contains(value, obj) == 1) { \
    PyObject* item = PyDict_GetItem(value, obj); \
    if (! PyString_Check(item)) {\
        std::cerr << "Error: the '" << PyString_AsString(obj) << "' value must be a string" << std::endl; \
        return false; \
    } \
    var = PyString_AsString(item); \
} else { \
    std::cerr << "Error: '" << PyString_AsString(obj) << "' key is missing" << std::endl; \
    return false; \
}

bool plot_from_PyObject(PyObject* value, Plot& plot) {
    static PyObject* PY_NAME = PyString_FromString("name");
    static PyObject* PY_VARIABLE = PyString_FromString("variable");
    static PyObject* PY_PLOT_CUT = PyString_FromString("plot_cut");
    static PyObject* PY_BINNING = PyString_FromString("binning");

    if (! PyDict_Check(value)) {
        std::cerr << "Error: plots dictionnary value must be a dictionnary" << std::endl;
    }

    CHECK_AND_GET(plot.name, PY_NAME);
    CHECK_AND_GET(plot.variable, PY_VARIABLE);
    CHECK_AND_GET(plot.plot_cut, PY_PLOT_CUT);
    CHECK_AND_GET(plot.binning, PY_BINNING);

    return true;
}

std::string get_uuid() {
    uuid_t out;
    uuid_generate(out);

    std::string uuid;
    uuid.resize(37);

    uuid_unparse(out, &uuid[0]);

    uuid[8] = '_';
    uuid[13] = '_';
    uuid[18] = '_';
    uuid[23] = '_';

    // Remove null terminator
    uuid.resize(36);

    // Ensure name starts with a letter to be a valid C++ identifier
    uuid = "p_" + uuid;

    return uuid;
}

inline TBranch* getTopBranch(TBranch* branch) {
    if (! branch)
        return nullptr;

    if (branch == branch->GetMother())
        return branch;

    return getTopBranch(branch->GetMother());
}

bool execute(const std::vector<Dataset>& datasets, const std::string& config_file, std::string output_dir = "");
bool parse_datasets(const std::string& json_file, std::vector<Dataset>& datasets);

bool get_plots(const std::string& python_file, Run& run) {

    std::vector<Plot>& plots = run.plots;

    plots.clear();

    std::FILE* f = std::fopen(python_file.c_str(), "r");
    if (!f) {
        std::cerr << "Failed to open '" << python_file << "'" <<std::endl;
        return false;
    }

    const std::string PLOTS_KEY_NAME = "plots";

    // Get a reference to the main module
    // and global dictionary
    PyObject* main_module = PyImport_AddModule("__main__");
    PyObject* global_dict = PyModule_GetDict(main_module);

    // Inject each options into an 'options' global dict
    PyObject* options_dict = PyDict_New();
    for (const auto& option: run.options) {
        PyObject* value = PyString_FromString(option.second.c_str());
        PyDict_SetItemString(options_dict, option.first.c_str(), value);
    }
    PyDict_SetItemString(global_dict, "options", options_dict);

    // Inject the dataset into a 'dataset' global dict
    PyObject* dataset_dict = run.dataset.toPyObject();
    PyDict_SetItemString(global_dict, "dataset", dataset_dict);

    // If PyROOT is used inside the script, it performs some cleanups when the python env. is destroyed. This cleanup makes ROOT unusable afterwards.
    // The cleanup function is registered with the `atexit` module.
    // The solution is to not execute the cleanup function. For that, before destroying the python env, we check the list of exit functions,
    // and delete the one from PyROOT if found

    // Ensure the module is loaded
    PyObject* atexit_module = PyImport_ImportModule("atexit");

    // Execute the script
    PyObject* script_result = PyRun_File(f, python_file.c_str(), Py_file_input, global_dict, global_dict);

    if (! script_result) {
        PyErr_Print();
        return false;
    } else {
        PyObject* py_plots = PyDict_GetItemString(global_dict, "plots");
        if (!py_plots) {
            std::cerr << "No 'plots' variable declared in python script" << std::endl;
            return false;
        }

        if (! PyList_Check(py_plots)) {
            std::cerr << "The 'plots' variable is not a list" << std::endl;
            return false;
        }

        size_t l = PyList_Size(py_plots);
        if (! l)
            return true;

        for (size_t i = 0; i < l; i++) {
            PyObject* item = PyList_GetItem(py_plots, i);

            Plot plot;
            if (plot_from_PyObject(item, plot)) {
                plots.push_back(plot);
            }
        }

        run.dataset.fromPyObject(PyDict_GetItemString(global_dict, "dataset"));
    }

    PyObject* atexit_exithandlers = PyObject_GetAttrString(atexit_module, "_exithandlers");
    for (size_t i = 0; i < PySequence_Size(atexit_exithandlers); i++) {
        PyObject* tuple = PySequence_GetItem(atexit_exithandlers, i);
        PyObject* f = PySequence_GetItem(tuple, 0);
        PyObject* module = PyFunction_GetModule(f);

        if (module && strcmp(PyString_AsString(module), "ROOT") == 0) {
            PySequence_DelItem(atexit_exithandlers, i);
            break;
        }
    }

    return true;
}

bool execute(std::vector<Dataset>& datasets, const std::string& config_file, std::string output_dir/* = ""*/) {

    // If an output directory is specified, use it, otherwise use the current directory
    if (output_dir == "")
      output_dir = ".";

    // Setting the TVirtualTreePlayer
    TVirtualTreePlayer::SetPlayer("TMultiDrawTreePlayer");

    TDirectory* plots_directory = gDirectory;

    for (Dataset& dataset: datasets) {

        std::cout << std::endl;
        std::cout << "Running on sample '" << dataset.name << "', with " << dataset.runs.size() << " run(s)." << std::endl;
        size_t run_number = 1;
        for (const Run& run: dataset.runs) {
            std::cout << "    Run " << run_number << " options: ";
            size_t index = 0;
            for (const auto& option: run.options) {
                std::cout << option.first << " = " << option.second;
                if (index != run.options.size() - 1)
                    std::cout << ", ";
                index++;
            }
            std::cout << std::endl;
            run_number++;
        }

        std::map<std::string, std::string> unique_names;

        // Gather all plots for all runs
        for (Run& run: dataset.runs) {
            get_plots(config_file, run);

            std::cout << "List of requested plots for this run: ";
            for (size_t i = 0; i < run.plots.size(); i++) {
                std::cout << "'" << run.plots[i].name << "'";
                if (i != run.plots.size() - 1)
                    std::cout << ", ";
            }
            std::cout << std::endl;

            // Convert plots name to unique name to avoid collision between different runs
            for (Plot& plot: run.plots) {
                std::string uuid = get_uuid();
                unique_names[uuid] = plot.name;
                plot.name = uuid;
            }
        }

        // Check if all output names are unique, or files will be overwritten
        for (size_t i = 0; i < dataset.runs.size(); i++) {
            std::string& output = dataset.runs[i].dataset.output_name;
            for (size_t j = i + 1; j < dataset.runs.size(); j++) {
                if (output == dataset.runs[j].dataset.output_name) {
                    output += "_run" + std::to_string(i + 1);
                    std::cout << "Warning: output file for run " << i + 1 << " is not unique. Changing it to '" << output << "'" << std::endl;
                    break;
                }
            }
        }

        std::unique_ptr<TChain> t(new TChain(dataset.tree_name.c_str()));
        std::string text_files;
        for (const auto& file: dataset.files) {
            text_files += "t->Add(\"" + file + "\");\n";
            t->Add(file.c_str());
        }

        TMultiDrawTreePlayer* player = dynamic_cast<TMultiDrawTreePlayer*>(t->GetPlayer());

        // Looping over the different plots
        for (Run& run: dataset.runs) {
            for (auto& p: run.plots) {
                //std::string plot_var = p.variable + ">>" + p.name + p.binning;
                //player->queueDraw(plot_var.c_str(), p.plot_cut.c_str(), "goff");
                // Create formulas
                p.var.reset(new TTreeFormula("var", p.variable.c_str(), t.get()));
                p.selector.reset(new TTreeFormula("selector", p.plot_cut.c_str(), t.get()));
            }
        }

        std::vector<Branch> branches;
        std::function<void(TTreeFormula*)> getBranches = [&branches, &getBranches](TTreeFormula* f) {
            if (!f)
                return;

            for (size_t i = 0; i < f->GetNcodes(); i++) {
                TLeaf* leaf = f->GetLeaf(i);
                if (! leaf)
                    continue;

                TBranch* p_branch = getTopBranch(leaf->GetBranch());

                Branch branch;
                branch.name = p_branch->GetName();
                if (std::find_if(branches.begin(), branches.end(), [&branch](const Branch& b) {  return b.name == branch.name;  }) == branches.end()) {
                    branch.type = p_branch->GetClassName();
                    if (branch.type.empty())
                        branch.type = leaf->GetTypeName();

                    branches.push_back(branch);
                }

                for (size_t j = 0; j < f->fNdimensions[i]; j++) {
                    if (f->fVarIndexes[i][j])
                        getBranches(f->fVarIndexes[i][j]);
                }
            }

            for (size_t i = 0; i < f->fAliases.GetEntriesFast(); i++) {
                getBranches((TTreeFormula*) f->fAliases.UncheckedAt(i));
            }
        };

        for (Run& run: dataset.runs) {
            for (auto& p: run.plots) {
                getBranches(p.var.get());
                getBranches(p.selector.get());
            }
        }

        // Sort alphabetically
        std::sort(branches.begin(), branches.end(), [](const Branch& a, const Branch& b) {
            return a.name < b.name;
        });

        std::string text_branches;
        for (const auto& branch: branches)  {
            text_branches += "const " + branch.type + "& " + branch.name + " = tree[\"" + branch.name + "\"].read<" + branch.type + ">();\n        ";
        }

        // Create plots code
        std::string hists_declaration;
        std::string text_plots;

        for (Run& run: dataset.runs) {
            for (auto& p: run.plots) {

                std::string binning = p.binning;
                binning.erase(std::remove_if(binning.begin(), binning.end(), [](char chr) { return chr == '(' || chr == ')'; }), binning.end());
                hists_declaration += "TH1* " + p.name + " = new TH1F(\"" + p.name + "\", \"\", " + binning + ");\n";

                ctemplate::TemplateDictionary plot("plot");
                plot.SetValue("CUT", p.plot_cut);
                plot.SetValue("VAR", p.variable);
                plot.SetValue("HIST", p.name);
                
                ctemplate::ExpandTemplate("../templates/Plot.tpl", ctemplate::DO_NOT_STRIP, &plot, &text_plots);
            }
        }

        ctemplate::TemplateDictionary header("header");
        header.SetValue("BRANCHES", text_branches);

        std::string output;
        ctemplate::ExpandTemplate("../templates/Plotter.h.tpl", ctemplate::DO_NOT_STRIP, &header, &output);

        std::ofstream out("out/Plotter.h");
        out << output;
        out.close();

        output.clear();

        std::string text_save_plots;
        for (const Run& run: dataset.runs) {

            std::string text_save_plot;
            for (auto& p: run.plots) {
                ctemplate::TemplateDictionary save_plot("save_plot");
                save_plot.SetValue("UNIQUE_NAME", p.name);
                save_plot.SetValue("PLOT_NAME", unique_names[p.name]);
                ctemplate::ExpandTemplate("../templates/SavePlot.tpl", ctemplate::DO_NOT_STRIP, &save_plot, &text_save_plot);
            }

            std::string output_file = output_dir + "/" + run.dataset.output_name + ".root";

            ctemplate::TemplateDictionary save_plots("save_plots");
            save_plots.SetValue("OUTPUT_FILE", output_file);
            save_plots.SetValue("ALL_PLOTS", text_save_plot);
            ctemplate::ExpandTemplate("../templates/SavePlots.tpl", ctemplate::DO_NOT_STRIP, &save_plots, &text_save_plots);
        }

        ctemplate::TemplateDictionary source("source");
        source.SetValue("HISTS_DECLARATION", hists_declaration);
        source.SetValue("PLOTS", text_plots);
        source.SetValue("SAVE_PLOTS", text_save_plots);
        source.SetValue("FILES", text_files);
        ctemplate::ExpandTemplate("../templates/Plotter.cc.tpl", ctemplate::DO_NOT_STRIP, &source, &output);

        out.open("out/Plotter.cc");
        out << output;
        out.close();
    }

    return true;
}

bool parse_datasets(const std::string& json_file, std::vector<Dataset>& datasets) {

    Json::Value root;
    {
        std::ifstream f(json_file);
        Json::Reader reader;
        if (! reader.parse(f, root, false)) {
            std::cerr << "Failed to parse '" << json_file << "'" << std::endl;
            return false;
        }
    }

    datasets.clear();

    // Looping over the different samples
    Json::Value::Members samples_str = root.getMemberNames();
    for (size_t index = 0; index < root.size(); index++) {

        const Json::Value& sample = root[samples_str[index]];
        Dataset dataset;

        // Mandatory fields
        dataset.name = samples_str[index];
        dataset.db_name = sample["db_name"].asString();
        dataset.cut = sample.get("sample_cut", "1").asString();
        dataset.tree_name = sample.get("tree_name", "t").asString();

        //dataset.output_name
        if (sample.isMember("output_name")) {
            dataset.output_name = sample["output_name"].asString();
        } else {
            dataset.output_name = dataset.db_name + "_histos";
        }

        // Runs
        if (sample.isMember("runs")) {

            for (Json::Value::ArrayIndex i = 0; i < sample["runs"].size(); i++) {
                Run run;
                run.dataset = dataset;
                run.dataset.runs.clear(); // Not needed

                for (auto it = sample["runs"][i].begin(); it != sample["runs"][i].end(); it++) {
                    run.options[it.name()] = it->asString();
                }

                if (run.options.empty()) {
                    std::cout << "A run for '" << dataset.name << "' does not have any options. Dropping it." << std::endl;
                    continue;
                }
                dataset.runs.push_back(run);
            }
        }

        if (dataset.runs.empty()) {
            // Add a default empty run if none are specified by the user
            Run run;
            run.dataset = dataset;
            dataset.runs.push_back(run);
        }

        // If a list of files is specified, only use those
        if (sample.isMember("files")) {
            Json::Value files = sample["files"];

            for(auto it = files.begin(); it != files.end(); ++it) {
                dataset.files.push_back((*it).asString());
            }
        } else if (sample.isMember("path")) {
            dataset.path = sample["path"].asString();
            dataset.files.push_back(dataset.path + "/*.root");
        }

        datasets.push_back(dataset);
    }

    return true;
}

int main( int argc, char* argv[]) {

    try {

        TCLAP::CmdLine cmd("Create histograms from trees", ' ', "0.2.0");

        TCLAP::ValueArg<std::string> datasetArg("d", "dataset", "Input datasets", true, "", "JSON file", cmd);
        TCLAP::ValueArg<std::string> outputArg("o", "output", "Output directory", false, "", "ROOT file", cmd);
        TCLAP::UnlabeledValueArg<std::string> plotsArg("plots", "A python script which will be executed and should returns a list of plots", true, "", "Python script", cmd);

        cmd.parse(argc, argv);

        std::vector<Dataset> datasets;
        parse_datasets(datasetArg.getValue(), datasets);

        if (datasets.empty()) {
            std::cerr << "Error: no input datasets specified." << std::endl;
            return 1;
        }

        /*
         * When PyROOT is loaded, it creates it's own ROOT application ([1] and [2]). We do not want this to happen,
         * because it messes with our already loaded ROOT.
         *
         * To prevent this, we create here our own application (which does nothing), just to prevent `CreatePyROOTApplication`
         * to do anything.
         *
         * [1] https://github.com/root-mirror/root/blob/0a62e34aa86b812651cfcf9526ba03b975adaa5c/bindings/pyroot/ROOT.py#L476
         * [2] https://github.com/root-mirror/root/blob/0a62e34aa86b812651cfcf9526ba03b975adaa5c/bindings/pyroot/src/TPyROOTApplication.cxx#L117
         */

        std::unique_ptr<TApplication> app(new TApplication("dummy", 0, NULL));

        Py_Initialize();

        bool ret = execute(datasets, plotsArg.getValue(), outputArg.getValue());

        Py_Finalize();

        return (ret ? 0 : 1);

    } catch (TCLAP::ArgException &e) {
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
        return 1;
    }

    return 0;
}

