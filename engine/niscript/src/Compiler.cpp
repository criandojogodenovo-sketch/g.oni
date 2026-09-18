/// Compiler do NI-Script — AST anotado (Sema) → bytecode (FASE 11 §6.4).
///
/// Convenções do emitter:
///   - Desvios JMP/JMPF/REPEAT_*: `pc += int32(a)` com pc JÁ avançado
///     (a = alvo - (posição+1)); ENTER_REPAIR usa `a` como pc ABSOLUTO da
///     continuação.
///   - Escrita de atribuição: RHS completo ANTES da escrita (atomicidade
///     por instrução — design §5.3.3).
///   - CHECK_TYPE só quando o alvo é tipado e o RHS é Dynamic (o resto o
///     Sema já provou estático).
///   - `@init` é um handler implícito com os inicializadores globais, na
///     ordem de declaração (executado pelo host após criar a instância).

#include "NiAst.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace eng::ni {
namespace {

[[nodiscard]] bool fieldIdOf(const std::string& name, std::uint32_t& id)
{
    if (name == "x" || name == "r") { id = static_cast<std::uint32_t>(NiField::X); return true; }
    if (name == "y" || name == "g") { id = static_cast<std::uint32_t>(NiField::Y); return true; }
    if (name == "z" || name == "b") { id = static_cast<std::uint32_t>(NiField::Z); return true; }
    if (name == "w" || name == "a") { id = static_cast<std::uint32_t>(NiField::W); return true; }
    if (name == "position") { id = static_cast<std::uint32_t>(NiField::Position); return true; }
    if (name == "rotation") { id = static_cast<std::uint32_t>(NiField::Rotation); return true; }
    if (name == "scale") { id = static_cast<std::uint32_t>(NiField::Scale); return true; }
    return false;
}

class Emitter final {
public:
    Emitter(const SemaResult& sema, const NiNativeTable& natives,
            NiProgram& program)
        : sema_(sema), natives_(natives), program_(program)
    {
    }

    [[nodiscard]] bool run(const std::vector<TopLevel>& top,
                           std::vector<NiDiag>& diags)
    {
        // funcs/handlers na MESMA ordem de índices do Sema (declaração)
        for (const TopLevel& item : top) {
            switch (item.kind) {
            case TopLevel::Kind::Func: {
                NiFunc& f = program_.funcs.emplace_back();
                f.name = item.name;
                f.nparams = static_cast<std::uint16_t>(item.params.size());
                const auto found = sema_.funcs.find(item.name);
                if (found != sema_.funcs.end()) {
                    f.paramTypes = found->second.paramTypes;
                }
                break;
            }
            case TopLevel::Kind::Handler: {
                NiFunc& f = program_.funcs.emplace_back();
                f.name = "@" + item.name; // nome interno distinto
                f.nparams = 0;
                break;
            }
            default: break;
            }
        }
        // globais (slots = ordem de declaração)
        for (const auto& [name, type] : sema_.globals) {
            NiGlobal g;
            g.name = name;
            g.type = type;
            g.zero = niZero(type);
            program_.globals.push_back(std::move(g));
        }
        program_.handlers = {};
        for (const auto& [event, funcIndex] : sema_.handlers) {
            NiHandler h;
            h.name = event;
            h.funcIndex = funcIndex;
            program_.handlers.push_back(std::move(h));
        }
        program_.modules = sema_.modules;
        program_.nativeCount = natives_.size();

        // corpos
        std::size_t funcCursor = 0;
        for (const TopLevel& item : top) {
            if (item.kind != TopLevel::Kind::Func
                && item.kind != TopLevel::Kind::Handler) {
                continue;
            }
            NiFunc& f = program_.funcs[funcCursor++];
            func_ = &f;
            nextSlot_ = 0;
            pushScope();
            if (item.kind == TopLevel::Kind::Func) {
                for (std::size_t p = 0; p < item.params.size(); ++p) {
                    (void)p;
                    ++nextSlot_; // params ocupam slots 0..n-1
                }
            }
            if (item.body != nullptr) {
                emitBlock(item.body);
            }
            // retorno implícito nil
            emit(OpCode::ZERO, static_cast<std::uint32_t>(NiType::Nil), 0,
                 item.line, item.col);
            emit(OpCode::GIVE, 0, 0, item.line, item.col);
            popScope();
            const auto found = sema_.localCount.find(&item);
            f.nlocals = found != sema_.localCount.end()
                            ? std::max<std::uint16_t>(found->second, f.nparams)
                            : f.nparams;
        }

        // @init: inicializadores globais em ordem de declaração
        bool hasInit = false;
        for (const TopLevel& item : top) {
            if (item.kind == TopLevel::Kind::Global && item.init != nullptr) {
                hasInit = true;
                break;
            }
        }
        if (hasInit) {
            NiFunc& f = program_.funcs.emplace_back();
            f.name = "@init";
            f.nparams = 0;
            f.nlocals = 0;
            func_ = &f;
            std::uint32_t slot = 0;
            for (const TopLevel& item : top) {
                if (item.kind != TopLevel::Kind::Global) {
                    continue;
                }
                const auto it = std::find_if(
                    program_.globals.begin(), program_.globals.end(),
                    [&](const NiGlobal& g) { return g.name == item.name; });
                if (it == program_.globals.end()) {
                    continue; // redeclaração inválida — Sema já reportou
                }
                const std::uint32_t gslot = slot++;
                if (item.init != nullptr) {
                    emitExpr(item.init);
                    if (it->type != NiType::Dynamic
                        && sema_.exprTypes.at(item.init) == NiType::Dynamic) {
                        emit(OpCode::CHECK_TYPE,
                             static_cast<std::uint32_t>(it->type), 0,
                             item.line, item.col);
                    }
                    emit(OpCode::STORE_G, gslot, 0, item.line, item.col);
                }
            }
            emit(OpCode::ZERO, static_cast<std::uint32_t>(NiType::Nil), 0,
                 1, 1);
            emit(OpCode::GIVE, 0, 0, 1, 1);
            NiHandler h;
            h.name = "@init";
            h.funcIndex = static_cast<std::uint32_t>(program_.funcs.size() - 1);
            program_.handlers.push_back(std::move(h));
        }
        (void)diags;
        return true;
    }

private:
    const SemaResult& sema_;
    const NiNativeTable& natives_;
    NiProgram& program_;
    NiFunc* func_ = nullptr;
    std::vector<std::unordered_map<std::string, std::uint16_t>> scopes_;
    std::uint16_t nextSlot_ = 0;

    // --- infra --------------------------------------------------------------

    std::uint32_t emit(OpCode op, std::uint32_t a, std::uint32_t b,
                       std::uint32_t line, std::uint32_t col)
    {
        NiInstr in;
        in.op = op;
        in.a = a;
        in.b = b;
        func_->code.push_back(in);
        func_->lines.push_back(line);
        func_->cols.push_back(col);
        return static_cast<std::uint32_t>(func_->code.size() - 1);
    }

    std::uint32_t emit(OpCode op, std::uint32_t a, std::uint32_t b,
                       const Expr* at)
    {
        return emit(op, a, b, at->line, at->col);
    }

    std::uint32_t emit(OpCode op, std::uint32_t a, std::uint32_t b,
                       const Stmt* at)
    {
        return emit(op, a, b, at->line, at->col);
    }

    [[nodiscard]] std::uint32_t pc() const noexcept
    {
        return static_cast<std::uint32_t>(func_->code.size());
    }

    /// Preenche salto relativo em `at` para o pc CORRENTE (alvo).
    void patchHere(std::uint32_t at) noexcept
    {
        func_->code[at].a =
            static_cast<std::int32_t>(pc()) - (static_cast<std::int32_t>(at) + 1);
    }
    /// Preenche salto relativo em `at` para `target`.
    void patchTo(std::uint32_t at, std::uint32_t target) noexcept
    {
        func_->code[at].a =
            static_cast<std::int32_t>(target)
            - (static_cast<std::int32_t>(at) + 1);
    }

    [[nodiscard]] std::uint32_t intConst(std::int64_t v)
    {
        for (std::size_t i = 0; i < program_.consts.size(); ++i) {
            if (program_.consts[i].kind == NiConst::Kind::Int
                && program_.consts[i].i == v) {
                return static_cast<std::uint32_t>(i);
            }
        }
        NiConst c;
        c.kind = NiConst::Kind::Int;
        c.i = v;
        program_.consts.push_back(std::move(c));
        return static_cast<std::uint32_t>(program_.consts.size() - 1);
    }

    [[nodiscard]] std::uint32_t floatConst(double v)
    {
        for (std::size_t i = 0; i < program_.consts.size(); ++i) {
            if (program_.consts[i].kind == NiConst::Kind::Float
                && program_.consts[i].d == v) {
                return static_cast<std::uint32_t>(i);
            }
        }
        NiConst c;
        c.kind = NiConst::Kind::Float;
        c.d = v;
        program_.consts.push_back(std::move(c));
        return static_cast<std::uint32_t>(program_.consts.size() - 1);
    }

    [[nodiscard]] std::uint32_t boolConst(bool v)
    {
        for (std::size_t i = 0; i < program_.consts.size(); ++i) {
            if (program_.consts[i].kind == NiConst::Kind::Bool
                && program_.consts[i].i == (v ? 1 : 0)) {
                return static_cast<std::uint32_t>(i);
            }
        }
        NiConst c;
        c.kind = NiConst::Kind::Bool;
        c.i = v ? 1 : 0;
        program_.consts.push_back(std::move(c));
        return static_cast<std::uint32_t>(program_.consts.size() - 1);
    }

    [[nodiscard]] std::uint32_t stringConst(const std::string& v)
    {
        for (std::size_t i = 0; i < program_.consts.size(); ++i) {
            if (program_.consts[i].kind == NiConst::Kind::String
                && program_.consts[i].s == v) {
                return static_cast<std::uint32_t>(i);
            }
        }
        NiConst c;
        c.kind = NiConst::Kind::String;
        c.s = v;
        program_.consts.push_back(std::move(c));
        return static_cast<std::uint32_t>(program_.consts.size() - 1);
    }

    [[nodiscard]] std::uint32_t chainConst(const std::vector<std::uint32_t>& ids)
    {
        for (std::size_t i = 0; i < program_.consts.size(); ++i) {
            if (program_.consts[i].kind == NiConst::Kind::FieldChain
                && program_.consts[i].chain == ids) {
                return static_cast<std::uint32_t>(i);
            }
        }
        NiConst c;
        c.kind = NiConst::Kind::FieldChain;
        c.chain = ids;
        program_.consts.push_back(std::move(c));
        return static_cast<std::uint32_t>(program_.consts.size() - 1);
    }

    // (nil usa ZERO(Nil) — não há constante de nil no pool)

    void pushScope() { scopes_.push_back({}); }
    void popScope() { scopes_.pop_back(); }

    void declareLocal(const std::string& name)
    {
        scopes_.back()[name] = nextSlot_++;
    }

    // --- statements ---------------------------------------------------------

    void emitBlock(const Block* block)
    {
        pushScope();
        for (const Stmt* s : block->stmts) {
            emitStmt(s);
        }
        popScope();
    }

    void emitStmt(const Stmt* s)
    {
        switch (s->kind) {
        case Stmt::Kind::Var: {
            const auto found = sema_.varSlots.find(s);
            const std::uint16_t slot = found != sema_.varSlots.end()
                                          ? found->second
                                          : nextSlot_;
            declareLocal(s->varName);
            if (s->init != nullptr) {
                emitExpr(s->init);
                const NiType declared =
                    s->varType.value_or(sema_.exprTypes.at(s->init));
                if (declared != NiType::Dynamic
                    && sema_.exprTypes.at(s->init) == NiType::Dynamic) {
                    emit(OpCode::CHECK_TYPE,
                         static_cast<std::uint32_t>(declared), 0, s);
                }
                emit(OpCode::STORE_L, slot, 0, s);
            } else {
                emitZero(s->varType.value(), s);
                emit(OpCode::STORE_L, slot, 0, s);
            }
            return;
        }
        case Stmt::Kind::If: {
            emitExpr(s->cond);
            const std::uint32_t jmpElse = emit(OpCode::JMPF, 0, 0, s->cond);
            emitBlock(s->body);
            if (s->elseBody != nullptr) {
                const std::uint32_t jmpEnd = emit(OpCode::JMP, 0, 0, s);
                patchHere(jmpElse);
                emitBlock(s->elseBody);
                patchHere(jmpEnd);
            } else {
                patchHere(jmpElse);
            }
            return;
        }
        case Stmt::Kind::Repeat: {
            emitExpr(s->cond);
            const std::uint32_t init = emit(OpCode::REPEAT_INIT, 0, 0, s->cond);
            const std::uint32_t bodyStart = pc();
            emitBlock(s->body);
            const std::uint32_t step = emit(OpCode::REPEAT_STEP, 0, 0, s);
            patchTo(step, bodyStart); // contador ainda > 0: volta ao corpo
            patchHere(init);          // contagem <= 0: pula o loop inteiro
            return;
        }
        case Stmt::Kind::Repair: {
            const std::uint32_t enter = emit(OpCode::ENTER_REPAIR, 0, 0, s);
            emitBlock(s->body);
            emit(OpCode::EXIT_REPAIR, 0, 0, s);
            const std::uint32_t cont = pc();
            func_->code[enter].a = cont; // pc absoluto da continuação
            return;
        }
        case Stmt::Kind::Timeout: {
            emitExpr(s->cond);
            emit(OpCode::ENTER_TIMEOUT, 0, 0, s->cond);
            emitBlock(s->body);
            emit(OpCode::EXIT_TIMEOUT, 0, 0, s);
            return;
        }
        case Stmt::Kind::Link: {
            emitExpr(s->target);
            emit(OpCode::LINK_TO, 0, 0, s);
            return;
        }
        case Stmt::Kind::Emit: {
            emit(OpCode::EMIT, stringConst(s->eventName), 0, s);
            return;
        }
        case Stmt::Kind::Give: {
            if (s->target != nullptr) {
                emitExpr(s->target);
            } else {
                emit(OpCode::ZERO, static_cast<std::uint32_t>(NiType::Nil),
                     0, s);
            }
            emit(OpCode::GIVE, 0, 0, s);
            return;
        }
        case Stmt::Kind::Assign: {
            emitAssign(s);
            return;
        }
        case Stmt::Kind::Expr: {
            emitExpr(s->value);
            emit(OpCode::POP, 0, 0, s);
            return;
        }
        }
    }

    void emitZero(NiType type, const Stmt* at)
    {
        // ZERO cobre TODOS os tipos (design §3: zero-value é contrato da
        // linguagem — entity → handle nulo, transform → identidade, ...).
        emit(OpCode::ZERO, static_cast<std::uint32_t>(type), 0, at);
    }

    void emitAssign(const Stmt* s)
    {
        // cadeia do lvalue
        std::vector<const Expr*> chain;
        const Expr* root = s->lvalue;
        while (root->kind == Expr::Kind::Member) {
            chain.push_back(root);
            root = root->base;
        }
        std::reverse(chain.begin(), chain.end());
        const auto rootRef = sema_.identRefs.at(root); // Sema garantiu

        const auto loadRoot = [&] {
            if (rootRef.kind == SemaResult::IdentRef::Kind::Local) {
                emit(OpCode::LOAD_L, rootRef.slot, 0, root);
            } else {
                emit(OpCode::LOAD_G, rootRef.slot, 0, root);
            }
        };
        const auto storeRoot = [&] {
            if (rootRef.kind == SemaResult::IdentRef::Kind::Local) {
                emit(OpCode::STORE_L, rootRef.slot, 0, root);
            } else {
                emit(OpCode::STORE_G, rootRef.slot, 0, root);
            }
        };

        if (chain.empty()) {
            emitExpr(s->value);
            if (rootRef.type != NiType::Dynamic
                && sema_.exprTypes.at(s->value) == NiType::Dynamic) {
                emit(OpCode::CHECK_TYPE,
                     static_cast<std::uint32_t>(rootRef.type), 0, s->value);
            }
            storeRoot();
            return;
        }

        const NiType baseType = rootRef.type;
        if (baseType == NiType::Vec2 || baseType == NiType::Vec3
            || baseType == NiType::Color || baseType == NiType::Transform) {
            // cadeia estrutural estática: NEST_SET com a cadeia completa
            loadRoot();
            emitExpr(s->value);
            std::vector<std::uint32_t> ids;
            for (const Expr* m : chain) {
                std::uint32_t id = 0;
                (void)fieldIdOf(m->s, id); // Sema validou
                ids.push_back(id);
            }
            emit(OpCode::NEST_SET, chainConst(ids), 0, s->lvalue);
            storeRoot();
            return;
        }

        // caminho dinâmico (entity/dynamic/compview): DYN_GET* + DYN_SET.
        // DYN_SET SEMPRE empilha a base resultante (balanceamento):
        //   - Entity: TO_ENTITY normaliza → STORE devolve o handle;
        //   - CompView: o slot NUNCA mudou → POP (descarta o acumulado);
        //   - Dynamic: guarda o valor modificado (value semantics).
        loadRoot();
        for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
            emit(OpCode::DYN_GET, stringConst(chain[i]->s), 0, chain[i]);
        }
        emitExpr(s->value);
        emit(OpCode::DYN_SET, stringConst(chain.back()->s), 0,
             s->lvalue);
        if (rootRef.type == NiType::Entity) {
            emit(OpCode::TO_ENTITY, 0, 0, s->lvalue);
            storeRoot();
        } else if (rootRef.type == NiType::CompView) {
            emit(OpCode::POP, 0, 0, s->lvalue);
        } else {
            storeRoot();
        }
    }

    // --- expressões ------------------------------------------------------------

    void emitExpr(const Expr* e)
    {
        switch (e->kind) {
        case Expr::Kind::Int:
            emit(OpCode::CONST, intConst(e->i), 0, e);
            return;
        case Expr::Kind::Float:
            emit(OpCode::CONST, floatConst(e->f), 0, e);
            return;
        case Expr::Kind::Bool:
            emit(OpCode::CONST, boolConst(e->b), 0, e);
            return;
        case Expr::Kind::Str:
            emit(OpCode::CONST, stringConst(e->s), 0, e);
            return;
        case Expr::Kind::Ident: {
            const auto ref = sema_.identRefs.at(e);
            if (ref.kind == SemaResult::IdentRef::Kind::Local) {
                emit(OpCode::LOAD_L, ref.slot, 0, e);
            } else {
                emit(OpCode::LOAD_G, ref.slot, 0, e);
            }
            return;
        }
        case Expr::Kind::Call: {
            for (const Expr* arg : e->args) {
                emitExpr(arg);
            }
            const auto ref = sema_.identRefs.at(e);
            if (ref.kind == SemaResult::IdentRef::Kind::Func) {
                emit(OpCode::CALL_F, ref.index, 0, e);
            } else {
                // b = argc REAL (variadic — ver NiNativeEntry)
                emit(OpCode::CALL_N, ref.index,
                     static_cast<std::uint32_t>(e->args.size()), e);
            }
            return;
        }
        case Expr::Kind::Unary:
            emitExpr(e->base);
            emit(e->unOp == UnOp::Neg ? OpCode::NEG : OpCode::NOT, 0, 0, e);
            return;
        case Expr::Kind::Binary:
            emitBinary(e);
            return;
        case Expr::Kind::Member:
            emitMember(e);
            return;
        }
    }

    void emitBinary(const Expr* e)
    {
        // curto-circuito booleano (design §3.1)
        if (e->binOp == BinOp::And) {
            emitExpr(e->left);
            const std::uint32_t jf = emit(OpCode::JMPF, 0, 0, e);
            emitExpr(e->right);
            const std::uint32_t jEnd = emit(OpCode::JMP, 0, 0, e);
            patchHere(jf);
            emit(OpCode::CONST, boolConst(false), 0, e);
            patchHere(jEnd);
            return;
        }
        if (e->binOp == BinOp::Or) {
            emitExpr(e->left);
            const std::uint32_t jf = emit(OpCode::JMPF, 0, 0, e);
            emit(OpCode::CONST, boolConst(true), 0, e);
            const std::uint32_t jEnd = emit(OpCode::JMP, 0, 0, e);
            patchHere(jf);
            emitExpr(e->right);
            patchHere(jEnd);
            return;
        }
        emitExpr(e->left);
        emitExpr(e->right);
        OpCode op = OpCode::ADD;
        switch (e->binOp) {
        case BinOp::Add: op = OpCode::ADD; break;
        case BinOp::Sub: op = OpCode::SUB; break;
        case BinOp::Mul: op = OpCode::MUL; break;
        case BinOp::Div: op = OpCode::DIV; break;
        case BinOp::Mod: op = OpCode::MOD; break;
        case BinOp::Eq: op = OpCode::EQ; break;
        case BinOp::Ne: op = OpCode::NE; break;
        case BinOp::Lt: op = OpCode::LT; break;
        case BinOp::Le: op = OpCode::LE; break;
        case BinOp::Gt: op = OpCode::GT; break;
        case BinOp::Ge: op = OpCode::GE; break;
        case BinOp::And: case BinOp::Or: break; // tratados acima
        }
        emit(op, 0, 0, e);
    }

    void emitMember(const Expr* e)
    {
        const NiType baseType = sema_.exprTypes.at(e->base);
        emitExpr(e->base);
        if (baseType == NiType::Vec2 || baseType == NiType::Vec3
            || baseType == NiType::Color || baseType == NiType::Transform) {
            std::uint32_t id = 0;
            (void)fieldIdOf(e->s, id); // Sema validou
            emit(OpCode::VEC_GET, id, 0, e);
            return;
        }
        // entity/dynamic/compview: caminho dinâmico
        emit(OpCode::DYN_GET, stringConst(e->s), 0, e);
    }
};

} // namespace

bool emitBytecode(const std::vector<TopLevel>& top, const SemaResult& sema,
                  const NiNativeTable& natives, NiProgram& program,
                  std::vector<NiDiag>& diags)
{
    Emitter emitter(sema, natives, program);
    return emitter.run(top, diags);
}

} // namespace eng::ni
