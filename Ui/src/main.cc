// The text front end, and the composition root: it owns the document and hands it
// to the Editor and the Planner. It gathers a line, asks the parser for one
// structured instruction, hands that to the Planner and prints what comes back. It
// never decides what an instruction means, and it never reaches for Model.
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "Document.hh"
#include "Editor.hh"
#include "IntentParser.hh"
#include "Planner.hh"
#include "Protocol.hh"

namespace {

// One visitor instead of a chain of tests: the parse either produced an
// instruction or the reason it could not.
struct RunInstruction {
    Planner& planner;

    bool operator()(const ParseError& error) const {
        std::cout << "[parse] failed: " << error.message << "\n";
        return false;
    }

    bool operator()(const Intent& intent) const {
        try {
            const Outcome outcome = planner.dispatch(intent);
            std::cout << "[" << kind_name(outcome.kind) << "] " << (outcome.ok ? "" : "failed: ")
                      << outcome.message << "\n";
            return outcome.ok;
        } catch (const std::exception& error) {
            // The front end is the boundary: whatever a lower layer throws comes out
            // as a line here, never as a crash.
            std::cout << "[failed] " << error.what() << "\n";
            return false;
        }
    }
};

bool run_instruction(Planner& planner, std::string_view instruction) {
    if (instruction.find_first_not_of(" \t\r\n") == std::string_view::npos) return true;
    return std::visit(RunInstruction{planner}, parse(instruction));
}

// Instructions are separated by ';', so one argument can drive a whole session.
bool run_instructions(Planner& planner, std::string_view instructions) {
    bool ok = true;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = instructions.find(';', start);
        const std::string_view instruction =
            end == std::string_view::npos ? instructions.substr(start) : instructions.substr(start, end - start);
        ok = run_instruction(planner, instruction) && ok;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    Document document;
    Editor editor{document};
    Planner planner{document, editor};

    if (argc > 1) {
        std::string instructions;
        for (int index = 1; index < argc; ++index) {
            if (index > 1) instructions += ' ';
            instructions += argv[index];
        }
        return run_instructions(planner, instructions) ? 0 : 1;
    }

    std::cout << "svg-editor - one instruction per line, 'quit' to leave\n\n" << usage();
    for (std::string line; std::getline(std::cin, line); ) {
        if (line == "quit" || line == "exit") break;
        run_instructions(planner, line);
    }
    return 0;
}
