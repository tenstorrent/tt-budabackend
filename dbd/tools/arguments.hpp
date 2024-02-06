#include <string>
#include <map>

const std::string BOOLEAN_FALSE = "FALSE";
const std::string BOOLEAN_TRUE  = "TRUE";

struct ProgramArgument {
    typedef enum {STRING, BOOLEAN, INT} ProgramArgumentType;
    std::string name;
    std::string value;
    std::string description;
    ProgramArgumentType type;
    bool required;
};


typedef std::map<std::string, ProgramArgument> ProgramArgumentsMap;

class ProgramArguments {
    public:
        ProgramArguments(const ProgramArgumentsMap& arguments) 
        : _arguments(arguments) {

        }
        bool has(const std::string& name) const {
            return !value(name).empty();
        }
        const std::string value(const std::string& name) const {
            auto it = _arguments.find(name);
            if (it != _arguments.end())
                return it->second.value;
            return "";
        }
    private:
        ProgramArgumentsMap _arguments;
};

class ProgramArgumentsParser {
    public:
        static void print_usage();
        static ProgramArguments parse_arguments(int argc, char** argv);
        static std::string usage_header;
        static ProgramArgumentsMap default_program_arguments;
};