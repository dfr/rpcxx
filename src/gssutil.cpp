#include <iostream>
#include <sstream>

#include <rpc++/errors.h>
#include <rpc++/gss.h>
#include <glog/logging.h>

namespace oncrpc {
namespace _detail {

[[noreturn]] void
reportError(gss_OID mech, uint32_t maj, uint32_t min)
{
    VLOG(2) << "reporting GSS-API error, major=" << maj << ", min=" << min;

#ifdef __APPLE__
    {
        CFErrorRef err = GSSCreateError(mech, maj, min);
        CFStringRef str = CFErrorCopyDescription(err);
        char buf[512];
        CFStringGetCString(str, buf, 512, kCFStringEncodingUTF8);
        VLOG(2) << buf;
    }
#endif

    uint32_t min_stat;
    uint32_t message_context;
    gss_buffer_desc buf;
    std::ostringstream ss;

    ss << "GSS-API error: "
       << "major_stat=" << maj
       << ", minor_stat=" << min
       << ": ";

    message_context = 0;
    do {
        gss_display_status(
            &min_stat, maj, GSS_C_GSS_CODE, GSS_C_NO_OID,
            &message_context, &buf);
        if (message_context != 0)
            ss << ", ";
        ss << std::string((const char*) buf.value, buf.length);
        gss_release_buffer(&min_stat, &buf);
    } while (message_context);
    if (mech) {
	ss << ", ";
        message_context = 0;
        do {
            gss_display_status(
                &min_stat, min, GSS_C_MECH_CODE, mech,
                &message_context, &buf);
            if (message_context != 0)
                ss << ", ";
            ss << std::string((const char*) buf.value, buf.length);
            gss_release_buffer(&min_stat, &buf);
        } while (message_context);
    }
    LOG(ERROR) << ss.str();
    throw GssError(ss.str());
}

void badSequence(uint32_t seq, uint32_t checkSeq)
{
    std::ostringstream ss;
    ss << "Bad RPCSEC_GSS sequence number: expected " << checkSeq
       << ", received " << seq;
    throw RpcError(ss.str());
}

}
}
