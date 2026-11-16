#include "mtl_parser.hpp"
#include <cctype>
#include <charconv>
#include <limits>

namespace rybench::generator {

namespace {

enum class TK {
    Ident,
    LParen,
    RParen,
    LBracket,
    RBracket,
    Comma,
    Number,
    End,
};

struct Token {
    TK          kind;
    std::string text;
    long long   numVal = 0;
};

struct Lexer {
    std::string_view src;
    size_t           pos = 0;

    void skipWS() {
        while (pos < src.size() && std::isspace((unsigned char)src[pos]))
            ++pos;
    }

    Token next() {
        skipWS();
        if (pos >= src.size()) return {TK::End, ""};

        char c = src[pos];

        if (c == '(') { ++pos; return {TK::LParen,   "("}; }
        if (c == ')') { ++pos; return {TK::RParen,   ")"}; }
        if (c == '[') { ++pos; return {TK::LBracket, "["}; }
        if (c == ']') { ++pos; return {TK::RBracket, "]"}; }
        if (c == ',') { ++pos; return {TK::Comma,    ","}; }

        if (std::isdigit((unsigned char)c)) {
            size_t start = pos;
            while (pos < src.size() && std::isdigit((unsigned char)src[pos]))
                ++pos;
            Token t{TK::Number, std::string(src.substr(start, pos - start))};
            std::from_chars(t.text.data(), t.text.data() + t.text.size(), t.numVal);
            return t;
        }

        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t start = pos;
            while (pos < src.size() &&
                   (std::isalnum((unsigned char)src[pos]) || src[pos] == '_'))
                ++pos;
            return {TK::Ident, std::string(src.substr(start, pos - start))};
        }

        throw std::runtime_error(
            std::string("MTL parser: unexpected character '") + c + "'");
    }

    Token peek() {
        size_t saved = pos;
        Token t = next();
        pos = saved;
        return t;
    }
};

struct Parser {
    Lexer lex;

    Token eat(TK expected, const char* msg) {
        Token t = lex.next();
        if (t.kind != expected)
            throw std::runtime_error(std::string("MTL parser: expected ") + msg +
                                     ", got '" + t.text + "'");
        return t;
    }

    // A time bound is a non-negative integer or the keyword 'inf' (= unbounded).
    uint64_t parseBound(const char* what) {
        Token t = lex.next();
        if (t.kind == TK::Number)
            return static_cast<uint64_t>(t.numVal);
        if (t.kind == TK::Ident && t.text == "inf")
            return std::numeric_limits<uint64_t>::max();
        throw std::runtime_error(std::string("MTL parser: expected ") + what +
                                 " (number or 'inf'), got '" + t.text + "'");
    }

    MTLNode parsePhi() {
        Token t = lex.peek();

        if (t.kind == TK::Ident && t.text == "not") {
            lex.next();
            // 'not' applies to any subformula (atom, parenthesized expr, or a
            // temporal/unary node). The preprocessor pushes the negation inward
            // (De Morgan, ◇/□ duality) to reach NNF.
            MTLNode f;
            f.op    = MTLNode::Op::Not;
            f.child = std::make_unique<MTLNode>(parsePhi());
            return f;
        }

        if (t.kind == TK::LParen) {
            lex.next();
            MTLNode inner = parsePhi();

            if (lex.peek().kind == TK::RParen) {
                lex.next();
                return inner;
            }

            Token op = eat(TK::Ident, "binary operator or ')'");

            MTLNode f;
            if      (op.text == "and")     f.op = MTLNode::Op::And;
            else if (op.text == "or")      f.op = MTLNode::Op::Or;
            else if (op.text == "implies") f.op = MTLNode::Op::Implies;
            else if (op.text == "until" || op.text == "release") {
                f.op = (op.text == "until") ? MTLNode::Op::Until
                                            : MTLNode::Op::Release;
                // optional interval: until[lo,hi] / release[lo,hi]
                // (bare operator = unbounded). 'release' is the De Morgan dual of
                // 'until': ¬(φ U ψ) ≡ (¬φ) R (¬ψ).
                if (lex.peek().kind == TK::LBracket) {
                    lex.next();
                    f.timeLow  = parseBound("lower bound");
                    eat(TK::Comma, "','");
                    f.timeHigh = parseBound("upper bound");
                    eat(TK::RBracket, "']'");
                }
            }
            else throw std::runtime_error(
                "MTL parser: unknown binary operator '" + op.text + "'");

            MTLNode rhs = parsePhi();
            eat(TK::RParen, "')'");

            f.left  = std::make_unique<MTLNode>(std::move(inner));
            f.right = std::make_unique<MTLNode>(std::move(rhs));
            return f;
        }

        if (t.kind == TK::Ident &&
            (t.text == "eventually" || t.text == "always")) {
            lex.next();
            MTLNode f;
            f.op = (t.text == "eventually") ? MTLNode::Op::Eventually
                                            : MTLNode::Op::Always;

            eat(TK::LBracket, "'['");
            f.timeLow  = parseBound("lower bound");
            eat(TK::Comma, "','");
            f.timeHigh = parseBound("upper bound");
            eat(TK::RBracket, "']'");

            f.child = std::make_unique<MTLNode>(parsePhi());
            return f;
        }

        if (t.kind == TK::Ident) {
            lex.next();
            MTLNode f;
            f.op       = MTLNode::Op::Atom;
            f.variable = t.text;
            return f;
        }

        throw std::runtime_error("MTL parser: unexpected token '" + t.text + "'");
    }

    MTLNode parse() {
        MTLNode f = parsePhi();
        Token tail = lex.peek();
        if (tail.kind != TK::End)
            throw std::runtime_error(
                "MTL parser: unexpected trailing token '" + tail.text + "'");
        return f;
    }
};

} // anonymous namespace

MTLNode parseMTLNode(std::string_view input) {
    Parser p;
    p.lex.src = input;
    return p.parse();
}

} // namespace rybench::generator
