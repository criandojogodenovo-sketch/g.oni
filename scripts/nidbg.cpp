/// nidbg — ferramenta de DEV do NI-Script (FASE 11, uso interno do motor).
///
/// Compila uma fonte .nis embutida e imprime o bytecode (funcs/handlers/
/// consts/instruções com linha) + resultado da execução de um evento.
/// Não é produto: existe para depurar o compilador/VM durante o
/// desenvolvimento (docs/ni-script/08).
///
/// Build (a partir da raiz do repo, preset linux-debug já configurado):
///   g++ -std=c++20 -g -fsanitize=address,undefined \
///     -Iengine/niscript/include -Ibuild/linux-debug/engine/niscript/src \
///     -Iengine/core/include -Iengine/math/include \
///     -Iengine/ecs/include -Iengine/reflect/include \
///     scripts/nidbg.cpp -o scripts/nidbg \
///     -Lbuild/linux-debug/engine/niscript -leng_niscript \
///     -Lbuild/linux-debug/engine/reflect -leng_reflect \
///     -Lbuild/linux-debug/engine/core -leng_core \
///     -Lbuild/linux-debug/engine/math -leng_math \
///     -Lbuild/linux-debug/engine/ecs -leng_ecs \
///     -Wl,-rpath,$PWD/build/linux-debug/engine/niscript ... (idem demais)

#include <cstdio>
#include <string>
#include <vector>

#include "eng/niscript/NiScript.hpp"
#include "eng/niscript/NiVm.hpp"

using namespace eng::ni;

namespace {

const char* kSource =
    "var n: int = 0\n"
    "up update:\n"
    "    repeat 3:\n"
    "        n = n + 1\n"
    "    stop\n"
    "stop\n";

const char* opName(OpCode op)
{
    switch (op) {
    case OpCode::CONST: return "CONST";
    case OpCode::ZERO: return "ZERO";
    case OpCode::LOAD_L: return "LOAD_L";
    case OpCode::STORE_L: return "STORE_L";
    case OpCode::LOAD_G: return "LOAD_G";
    case OpCode::STORE_G: return "STORE_G";
    case OpCode::CHECK_TYPE: return "CHECK_TYPE";
    case OpCode::ADD: return "ADD";
    case OpCode::SUB: return "SUB";
    case OpCode::MUL: return "MUL";
    case OpCode::DIV: return "DIV";
    case OpCode::MOD: return "MOD";
    case OpCode::NEG: return "NEG";
    case OpCode::NOT: return "NOT";
    case OpCode::EQ: return "EQ";
    case OpCode::NE: return "NE";
    case OpCode::LT: return "LT";
    case OpCode::LE: return "LE";
    case OpCode::GT: return "GT";
    case OpCode::GE: return "GE";
    case OpCode::JMP: return "JMP";
    case OpCode::JMPF: return "JMPF";
    case OpCode::CALL_F: return "CALL_F";
    case OpCode::CALL_N: return "CALL_N";
    case OpCode::GIVE: return "GIVE";
    case OpCode::POP: return "POP";
    case OpCode::VEC_GET: return "VEC_GET";
    case OpCode::NEST_SET: return "NEST_SET";
    case OpCode::DYN_GET: return "DYN_GET";
    case OpCode::DYN_SET: return "DYN_SET";
    case OpCode::TO_ENTITY: return "TO_ENTITY";
    case OpCode::SELF: return "SELF";
    case OpCode::LINK_TO: return "LINK_TO";
    case OpCode::EMIT: return "EMIT";
    case OpCode::ENTER_REPAIR: return "ENTER_REPAIR";
    case OpCode::EXIT_REPAIR: return "EXIT_REPAIR";
    case OpCode::ENTER_TIMEOUT: return "ENTER_TIMEOUT";
    case OpCode::EXIT_TIMEOUT: return "EXIT_TIMEOUT";
    case OpCode::REPEAT_INIT: return "REPEAT_INIT";
    case OpCode::REPEAT_STEP: return "REPEAT_STEP";
    }
    return "?";
}

} // namespace

int main()
{
    NiNativeTable natives;
    natives.addBaseLibrary();
    natives.addStandardHost();

    std::vector<NiDiag> diags;
    auto result = compile(kSource, CompileOptions{&natives}, &diags);
    if (!result.ok()) {
        std::printf("compilacao FALHOU (%zu diag):\n", diags.size());
        for (const NiDiag& d : diags) {
            std::printf("  %u:%u %s\n", d.line, d.col, d.message.c_str());
        }
        return 1;
    }
    auto program = std::move(result).value();

    for (const NiHandler& h : program->handlers) {
        std::printf("handler '%s' -> func %u\n", h.name.c_str(),
                    h.funcIndex);
    }
    for (std::size_t fi = 0; fi < program->funcs.size(); ++fi) {
        const NiFunc& f = program->funcs[fi];
        std::printf("func[%zu] %s params=%u locals=%u code=%zu\n", fi,
                    f.name.c_str(), f.nparams, f.nlocals, f.code.size());
        for (std::size_t pc = 0; pc < f.code.size(); ++pc) {
            std::printf("  %3zu L%-2u %-13s a=%d b=%d\n", pc, f.lines[pc],
                        opName(f.code[pc].op), static_cast<int>(f.code[pc].a),
                        static_cast<int>(f.code[pc].b));
        }
    }

    NiInstanceSet set;
    NiVm vm;
    auto& state = set.create(program, eng::ecs::Entity{1, 0});
    NiExecContext::Params params;
    params.natives = &natives;
    params.set = &set;
    (void)vm.run(state, "@init", params);
    const auto fault = vm.run(state, "update", params);
    std::printf("fault? %d\n", fault.has_value() ? 1 : 0);
    if (const NiValue* n = state.global("n")) {
        std::printf("n=%lld\n", static_cast<long long>(n->i));
    }
    return 0;
}
