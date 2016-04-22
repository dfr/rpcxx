#include <cassert>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "lexer.h"

using namespace oncrpc::rpcgen;
using namespace std;

static unordered_map<string, int> keywords = {
    { "bool", Token::KBOOL },
    { "case", Token::KCASE },
    { "const", Token::KCONST },
    { "default", Token::KDEFAULT },
    { "double", Token::KDOUBLE },
    { "enum", Token::KENUM },
    { "float", Token::KFLOAT },
    { "hyper", Token::KHYPER },
    { "int", Token::KINT },
    { "oneway", Token::KONEWAY },
    { "opaque", Token::KOPAQUE },
    { "opaqueref", Token::KOPAQUEREF },
    { "program", Token::KPROGRAM },
    { "quadruple", Token::KQUADRUPLE },
    { "struct", Token::KSTRUCT },
    { "string", Token::KSTRING },
    { "switch", Token::KSWITCH },
    { "typedef", Token::KTYPEDEF },
    { "union", Token::KUNION },
    { "unsigned", Token::KUNSIGNED },
    { "version", Token::KVERSION },
    { "void", Token::KVOID }
};

Token::Token(const Location& loc, int type)
    : type_(type),
      loc_(loc)
{
}

Token::Token(const Location& loc, int type, const string& value)
    : type_(type),
      svalue_(value),
      loc_(loc)
{
}

Token::Token(const Location& loc, int type, int value)
    : type_(type),
      ivalue_(value),
      loc_(loc)
{
}

string
Token::to_string(int type)
{
    switch (type) {
    case END_OF_FILE:
        return "end of file";

    case IDENTIFIER:
        return "identifier";

    case INTEGER:
        return "integer";

    case STRING:
        return "string";

    default:
        break;
    }

    for (const auto& i: keywords)
        if (type == i.second)
            return i.first;

    if (type < 256) {
        ostringstream ss;
        ss << "'" << char(type) << "'";
        return ss.str();
    }

    return std::to_string(type);
}

Lexer::Lexer(const string& filename, istream& in, ostream& out)
    : in_(in),
      out_(out)
{
    loc_.filename = filename;
}

Lexer::Lexer(istream& in, ostream& out)
    : in_(in),
      out_(out)
{
    loc_.filename = "<unknown>";
}

Token
Lexer::nextToken()
{
    for (;;) {
        auto loc = loc_;
        auto ch = get();

        if (ch == istream::traits_type::eof())
            return Token(loc, Token::END_OF_FILE);

        // Skip comments
        if (ch == '/' && peek() == '*') {
            get();
            do {
                ch = get();
            } while (ch != '*' || peek() != '/');
            get();
            continue;
        }

        // Skip whitespace
        if (isspace(ch))
            continue;

        if (isalpha(ch)) {
            string identifier;
            identifier += ch;
            while (isalnum(peek()) || peek() == '_')
                identifier += get();
            auto i = keywords.find(identifier);
            if (i == keywords.end())
                return Token(loc, Token::IDENTIFIER, identifier);
            else
                return Token(loc, i->second);
        }

        if (isdigit(ch) || ch == '-') {
            int num;
            int sign;

            if (ch == '-') {
                if (!isdigit(peek()))
                    return Token(loc, ch);
                ch = get();
                sign = -1;
            }
            else {
                sign = 1;
            }
            num = ch - '0';

            // Check for 0x prefix
            if (ch == '0' && peek() == 'x') {
                get();
                while (isxdigit(peek())) {
                    ch = get();
                    int digit = 0;
                    if (isdigit(ch))
                        digit = ch - '0';
                    else if (ch >= 'a' && ch <= 'f')
                        digit = 10 + ch - 'a';
                    else if (ch >= 'A' && ch <= 'F')
                        digit = 10 + ch - 'A';
                    else
                        assert(false);
                    num = 16 * num + digit;
                }
            }
            else {
                while (isdigit(peek()))
                    num = 10 * num + (get() - '0');
            }
            return Token(loc, Token::INTEGER, sign * num);
        }

        return Token(loc, ch);
    }
}

char
Lexer::get()
{
    for (;;) {
        auto ch = in_.get();
        if (ch == '%' && loc_.column == 1) {
            ch = in_.get();
            while (ch != istream::traits_type::eof() && ch != '\n') {
                out_.put(ch);
                ch = in_.get();
            }
            if (ch == '\n')
                out_ << endl;
            loc_.line++;
            continue;
        }
        if (ch == '\n') {
            loc_.line++;
            loc_.column = 1;
        }
        else {
            loc_.column++;
        }
        return ch;
    }
}

char
Lexer::peek()
{
    return in_.peek();
}
