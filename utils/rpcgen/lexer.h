// -*- c++ -*-

#pragma once

#include <istream>
#include <string>

namespace oncrpc {

namespace rpcgen {

using namespace std;

struct Location
{
    string filename;
    int line = 1;
    int column = 1;
};

static inline ostream&
operator<<(ostream& str, const Location& loc)
{
    str << loc.filename << ":" << loc.line << ":" << loc.column;
    return str;
}

class Token
{
public:
    enum {
        END_OF_FILE = 256,
        IDENTIFIER,
        INTEGER,
        STRING,

        KBOOL,
        KCASE,
        KCONST,
        KDEFAULT,
        KDOUBLE,
        KENUM,
        KFLOAT,
        KHYPER,
        KINT,
        KONEWAY,
        KOPAQUE,
        KOPAQUEREF,
        KPROGRAM,
        KQUADRUPLE,
        KSTRUCT,
        KSTRING,
        KSWITCH,
        KTYPEDEF,
        KUNION,
        KUNSIGNED,
        KVERSION,
        KVOID,
    };

    Token(const Location& loc, int type);
    Token(const Location& loc, int type, const string& value);
    Token(const Location& loc, int type, int value);

    static string to_string(int type);

    int type() const { return type_; }
    const string svalue() const { return svalue_; }
    int ivalue() const { return ivalue_; }
    const Location& loc() const { return loc_; }

private:
    int type_;
    string svalue_;
    int ivalue_ = 0;
    Location loc_;
};

class Lexer
{
public:
    Lexer(const string& filename, istream& in, ostream& out);
    Lexer(istream& in, ostream& out);

    Token nextToken();

private:
    char get();
    char peek();

    istream& in_;
    ostream& out_;
    Location loc_;
};

}
}
