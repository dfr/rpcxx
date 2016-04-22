#include "parser.h"

using namespace oncrpc::rpcgen;

void Procedure::print(Indent indent, ostream& str) const
{
    str << indent << "//";
    retType_->print(indent, str);
    str << " " << name_ << "(";
    string sep = "";
    for (const auto& argType: argTypes_) {
        str << sep;
        sep = ", ";
        argType->print(indent, str);
    }
    str << ") = " << proc_ << ";" << endl;
}

void Procedure::printDeclaration(
    Indent indent,
    int namePrefixLen,
    const string& methodPrefix,
    const string& methodSuffix,
    ostream& str)
{
    str << indent << methodPrefix
         << *retType_ << " " << methodName(namePrefixLen);
    str << "(";
    string sep = "";
    int i = 0;
    for (const auto& argType: *this) {
        if (argType->isVoid())
            continue;
        str << sep << "const " << *argType << "& _arg" << i;
        sep = ", ";
        i++;
    }
    str << ")" << methodSuffix << endl;
}

void Procedure::printClientBody(Indent indent, ostream& str) const
{
    str << indent << "{" << endl;
    ++indent;
    if (retType_->isOneway()) {
        str << indent << "channel_->send(" << endl;
        ++indent;
        str << indent << "client_.get(), "
            << name() << "," << endl
            << indent << "[&](oncrpc::XdrSink* xdrs) {" << endl;
        ++indent;
        int i = 0;
        for (const auto& argType: *this) {
            if (argType->isVoid())
                continue;
            str << indent << "xdr(_arg" << i << ", xdrs);" << endl;
            i++;
        }
        --indent;
        str << indent << "});" << endl;
        --indent;
        --indent;
        str << indent << "}" << endl;
    }
    else {
        if (!retType_->isVoid())
            str << indent << *retType() << " _res;" << endl;
        str << indent << "channel_->call(" << endl;
        ++indent;
        str << indent << "client_.get(), "
            << name() << "," << endl
            << indent << "[&](oncrpc::XdrSink* xdrs) {" << endl;
        ++indent;
        int i = 0;
        for (const auto& argType: *this) {
            if (argType->isVoid())
                continue;
            str << indent << "xdr(_arg" << i << ", xdrs);" << endl;
            i++;
        }
        --indent;
        str << indent << "}," << endl;
        str << indent << "[&](oncrpc::XdrSource* xdrs) {" << endl;
        ++indent;
        if (!retType_->isVoid())
            str << indent << "xdr(_res, xdrs);" << endl;
        --indent;
        str << indent << "});" << endl;
        --indent;
        if (!retType_->isVoid())
            str << indent << "return _res;" << endl;
        --indent;
        str << indent << "}" << endl;
    }
}

void ProgramVersion::print(Indent indent, ostream& str) const
{
    str << indent << "//version " << name_ << " {" << endl;
    ++indent;
    for (const auto& proc: procs_)
        proc->print(indent, str);
    --indent;
    str << indent << "//} = " << vers_ << ";" << endl;
}

void ProgramVersion::printInterface(
    Indent indent, ProgramDefinition* def, ostream& str) const
{
    str << "constexpr int " << name_ << " = " << vers_ << ";" << endl;
    vector<string> methods;
    for (const auto& proc: *this) {
        methods.push_back(proc->name());
        str << "constexpr int " << proc->name()
            << " = " << proc->proc() << ";" << endl;
    }
    str << endl;
    int prefixlen = longestCommonPrefix(methods);

    string className = def->name() + "_" + to_string(vers_);
    className = formatIdentifier(UCAMEL, parseIdentifier(className));
    str << "class " << "I" << className << " {" << endl
         << "public:" << endl;

    ++indent;

    str << indent << "virtual size_t bufferSize() const { return 0; }" << endl;
    str << indent << "virtual void setBufferSize(size_t sz) {}" << endl;

    for (const auto& proc: *this) {
        proc->printDeclaration(
            indent, prefixlen, "virtual ", " = 0;", str);
    }
    --indent;

    str << indent << "};" << endl << endl;
}

void ProgramVersion::printClientStubs(
    Indent indent, ProgramDefinition* def, ostream& str) const
{
    vector<string> methods;
    for (const auto& proc: *this) {
        methods.push_back(proc->name());
    }
    int prefixlen = longestCommonPrefix(methods);

    string className = def->name() + "_" + to_string(vers_);
    className = formatIdentifier(UCAMEL, parseIdentifier(className));
    str << indent << "template <typename CL = oncrpc::Client>" << endl;
    str << indent << "class " << className
        << ": public I" << className << " {" << endl
        << indent << "public:" << endl;

    ++indent;
    str << indent << "template <typename... Args>" << endl
        << indent << className << "("
        << "const std::string& host, Args&&... args)" << endl;
    ++indent;
    str << indent << ": channel_(oncrpc::Channel::open(host, "
        << def->name() << ", " << name_
        << ", \"tcp\"))," << endl
        << indent << "  client_(std::make_shared<CL>(" << endl;
    ++indent;
    str << indent << def->name() << ", " << name_ << ", "
        << "std::forward<Args>(args)...))" << endl;
    --indent;
    --indent;
    str << indent << "{}" << endl << endl;

    str << indent << "template <typename... Args>" << endl
        << indent << className << "("
        << "std::shared_ptr<oncrpc::Channel> channel, "
        << "Args&&... args)" << endl;
    ++indent;
    str << indent << ": channel_(channel)," << endl
        << indent << "  client_(std::make_shared<CL>(" << endl;
    ++indent;
    str << indent << def->name() << ", " << name_ << ", "
        << "std::forward<Args>(args)...))" << endl;
    --indent;
    --indent;
    str << indent << "{}" << endl << endl;

    str << indent << "size_t bufferSize() const override" << endl;
    str << indent << "{" << endl;
    ++indent;
    str << indent << "return channel_->bufferSize();" << endl;
    --indent;
    str << indent << "}" << endl;

    str << indent << "void setBufferSize(size_t sz) override" << endl;
    str << indent << "{" << endl;
    ++indent;
    str << indent << "channel_->setBufferSize(sz);" << endl;
    --indent;
    str << indent << "}" << endl;

    str << indent << "auto channel() const { return channel_; }" << endl;
    str << indent << "auto client() const { return client_; }" << endl;

    for (const auto& proc: *this) {
        proc->printDeclaration(
            indent, prefixlen, "", " override", str);
        proc->printClientBody(
            indent, str);
    }
    --indent;

    str << indent << "private:" << endl;
    ++indent;
    str << indent
        << "std::shared_ptr<oncrpc::Channel> channel_;" << endl
        << indent
        << "std::shared_ptr<CL> client_;" << endl;
    --indent;
    str << indent << "};" << endl << endl;
}

void ProgramVersion::printServerStubs(
    Indent indent, ProgramDefinition* def, ostream& str) const
{
    vector<string> methods;
    for (const auto& proc: *this) {
        methods.push_back(proc->name());
    }
    int prefixlen = longestCommonPrefix(methods);

    string className = def->name() + "_" + to_string(vers_);
    className = formatIdentifier(UCAMEL, parseIdentifier(className));
    str << indent << "class " << className << "Service"
        << ": public I" << className << " {" << endl
        << indent << "public:" << endl;

    ++indent;
    str << indent << "virtual void dispatch(oncrpc::CallContext&& ctx)" << endl
        << indent << "{" << endl;
    ++indent;
    str << indent << "switch (ctx.proc()) {" << endl;
    for (auto proc: *this) {
        str << indent << "case " << proc->name() << ": {" << endl;
        ++indent;
        int i = 0;
        for (auto argType: *proc) {
            if (!argType->isVoid()) {
                str << indent << *argType << " " << "_arg" << i << ";" << endl;
                ++i;
            }
        }
        if (i > 0) {
            str << indent << "ctx.getArgs([&](oncrpc::XdrSource* xdrs) {"
                << endl;
            ++indent;
            i = 0;
            for (auto argType: *proc) {
                if (!argType->isVoid()) {
                    str << indent << "xdr(_arg" << i << ", xdrs);" << endl;
                    ++i;
                }
            }
            --indent;
            str << indent << "});" << endl;
        }
        if (!proc->retType()->isVoid()) {
            str << indent << *proc->retType() << " _ret;" << endl;
        }
        i = 0;
        str << indent;
        if (!proc->retType()->isVoid()) {
            str << "_ret = ";
        }
        str << proc->methodName(prefixlen) << "(";
        for (auto argType: *proc) {
            if (!argType->isVoid()) {
                if (i > 0) str << ", ";
                str << "std::move(_arg" << i << ")";
                i++;
            }
        }
        str << ");" << endl;
        if (!proc->retType()->isOneway()) {
            if (!proc->retType()->isVoid()) {
                str << indent << "ctx.sendReply([&](oncrpc::XdrSink* xdrs) {"
                    << endl;
                ++indent;
                str << indent << "xdr(_ret, xdrs);" << endl;
                --indent;
                str << indent << "});" << endl;
            }
            else {
                str << indent << "ctx.sendReply([](oncrpc::XdrSink*){});" << endl;
            }
        }
        str << indent << "break;" << endl;
        --indent;
        str << indent << "}" << endl;
    }
    str << indent << "default: ctx.procedureUnavailable();" << endl;
    str << indent << "}" << endl;
    --indent;
    str << indent << "}" << endl << endl;

    str << indent
        << "void bind(std::shared_ptr<oncrpc::ServiceRegistry> svcreg)"
        << endl << indent << "{" << endl;
    ++indent;
    str << indent << "svcreg->add(" << def->name() << ", " << name_ << ", "
        << "std::bind(&" << className << "Service::dispatch, this, "
        << "std::placeholders::_1));" << endl;
    --indent;
    str << indent << "}" << endl << endl;

    str << indent
        << "void unbind(std::shared_ptr<oncrpc::ServiceRegistry> svcreg)"
        << endl << indent << "{" << endl;
    ++indent;
    str << indent << "svcreg->remove(" << def->name() << ", " << name_
        << ");" << endl;
    --indent;
    str << indent << "}" << endl;

    --indent;
    str << indent << "};" << endl << endl;
}
