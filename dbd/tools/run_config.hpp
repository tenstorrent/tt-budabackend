#pragma once
#include <string>
#include <map>
#include "verif/verif_test_config.hpp"
#include "verif/verif_comparison_types.hpp"

typedef std::map <std::string, std::string> ProgramParameters;
typedef std::unordered_map<std::string, VerifStimulusConfig> StimulusConfig;
typedef std::unordered_map<std::string, VerifComparisonConfig> ComparsionConfig;

class ConfigBuilder {
    public:
        static StimulusConfig build_default_stimulus() {
            VerifStimulusConfig config;
            config.type = StimulusType::Uniform;
            config.uniform_lower_bound = -1.0;
            config.uniform_upper_bound = 1.0;
            StimulusConfig stimulus_config;
            stimulus_config.insert({verif::test_config::c_default_config_attr_name, config});
            return stimulus_config;
        }

        static ComparsionConfig build_default_comparsion() {
            VerifComparisonConfig config;
            config.type = ComparisonType::AllClose;
            ComparsionConfig comparsion_config;
            comparsion_config.insert({verif::test_config::c_default_config_attr_name, config});
            return comparsion_config;
        }
};

