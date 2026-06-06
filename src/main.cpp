#include "lexer.hpp"
#include "parser.hpp"
#include "semantic.hpp"
#include "codegen.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <string>

static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "error: cannot open file '" << path << "'\n";
        std::exit(1);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static void printUsage() {
    std::cerr << "Astra Compiler (Strict Mode)\n"
              << "Usage:\n"
              << "  astra run    <file.astra>              Compile and run\n"
              << "  astra build  <file.astra> [-o output]  Compile to executable\n"
              << "  astra emit-ir <file.astra>             Dump LLVM IR\n";
}

// ── Compile pipeline: source → LLVM Module ─────────────────────────────
static std::unique_ptr<CodeGen> compile(const std::string& source, const std::string& filename) {
    // 1. Lex
    Lexer lexer(source, filename);
    auto tokens = lexer.tokenize();

    // 2. Parse
    Parser parser(tokens, filename);
    auto program = parser.parse();

    // 3. Semantic analysis
    SemanticAnalyzer sema;
    sema.analyze(program.get());

    // 4. Code generation
    auto codegen = std::make_unique<CodeGen>(filename);
    codegen->generate(program.get(), sema);

    return codegen;
}

// ═══════════════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage();
        return 1;
    }

    std::string mode = argv[1];
    std::string inputFile = argv[2];

    std::string source = readFile(inputFile);

    try {
        if (mode == "emit-ir") {
            auto codegen = compile(source, inputFile);
            codegen->dumpIR();
            return 0;
        }

        if (mode == "run") {
            auto codegen = compile(source, inputFile);

            // Emit object file to temp
            std::string objFile = "/tmp/astra_output.o";
            if (!codegen->emitObjectFile(objFile)) {
                std::cerr << "error: failed to emit object file\n";
                return 1;
            }

            // Link with clang
            std::string exeFile = "/tmp/astra_output";
            std::string linkCmd = "/opt/homebrew/opt/llvm/bin/clang++ " + objFile +
                                  " -o " + exeFile + " -lc 2>&1";
            int linkResult = std::system(linkCmd.c_str());
            if (linkResult != 0) {
                std::cerr << "error: linking failed\n";
                return 1;
            }

            // Run
            int runResult = std::system(exeFile.c_str());
            return WEXITSTATUS(runResult);
        }

        if (mode == "build") {
            auto codegen = compile(source, inputFile);

            // Determine output name
            std::string outputFile = "a.out";
            for (int i = 3; i < argc - 1; i++) {
                if (std::string(argv[i]) == "-o") {
                    outputFile = argv[i + 1];
                    break;
                }
            }

            // Emit object file
            std::string objFile = "/tmp/astra_build.o";
            if (!codegen->emitObjectFile(objFile)) {
                std::cerr << "error: failed to emit object file\n";
                return 1;
            }

            // Link
            std::string linkCmd = "/opt/homebrew/opt/llvm/bin/clang++ " + objFile +
                                  " -o " + outputFile + " -lc 2>&1";
            int linkResult = std::system(linkCmd.c_str());
            if (linkResult != 0) {
                std::cerr << "error: linking failed\n";
                return 1;
            }

            std::cout << "Built: " << outputFile << "\n";
            return 0;
        }

        std::cerr << "error: unknown mode '" << mode << "'\n";
        printUsage();
        return 1;

    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}