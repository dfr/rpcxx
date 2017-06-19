/*-
 * Copyright (c) 2016-present Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

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
