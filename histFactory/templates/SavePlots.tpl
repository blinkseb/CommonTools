{
    std::unique_ptr<TFile> outfile(TFile::Open("{{OUTPUT_FILE}}", "recreate"));

    {{ALL_PLOTS}}
}
