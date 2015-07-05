#include <iostream>
#include <sstream>

#include <rpc++/errors.h>
#include <rpc++/gss.h>
#include <rpc++/util.h>

namespace oncrpc {
namespace _gssdetail {

[[noreturn]] void
reportError(gss_OID mech, uint32_t maj, uint32_t min)
{
    {
        CFErrorRef err = GSSCreateError(mech, maj, min);
        CFStringRef str = CFErrorCopyDescription(err);
        char buf[512];
        CFStringGetCString(str, buf, 512, kCFStringEncodingUTF8);
        std::cout << buf << std::endl;
    }

    uint32_t maj_stat, min_stat;
    uint32_t message_context;
    gss_buffer_desc buf;
    std::ostringstream ss;

    ss << "GSS-API error: "
       << "major_stat=" << maj
       << ", minor_stat=" << min
       << ": ";

    message_context = 0;
    do {
        maj_stat = gss_display_status(
            &min_stat, maj, GSS_C_GSS_CODE, GSS_C_NO_OID,
            &message_context, &buf);
        if (message_context != 0)
            ss << ", ";
        ss << std::string((const char*) buf.value, buf.length);
        gss_release_buffer(&min_stat, &buf);
    } while (message_context);
    if (mech) {
        message_context = 0;
        do {
            maj_stat = gss_display_status(
                &min_stat, min, GSS_C_MECH_CODE, mech,
                &message_context, &buf);
            if (message_context != 0)
                ss << ", ";
            ss << std::string((const char*) buf.value, buf.length);
            gss_release_buffer(&min_stat, &buf);
        } while (message_context);
    }
    throw RpcError(ss.str());
}

}
}
