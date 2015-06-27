#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

//#define GSSKRB_APPLE_DEPRECATED(x)
//#include <gssapi/gssapi.h>
#include <GSS/GSS.h>
//#include <CoreFoundation/CoreFoundation.h>

void
usage()
{

    printf("Usage: gsstest client [mech] [service-name] | server\n");
    exit(1);
}

#if 1

void
report_error(gss_OID mech, OM_uint32 maj, OM_uint32 min)
{
    OM_uint32 maj_stat, min_stat;
    OM_uint32 message_context;
    gss_buffer_desc buf;

    printf("major_stat=%d, minor_stat=%d\n", maj, min);

    message_context = 0;
    do {
	maj_stat = gss_display_status(&min_stat, maj,
				      GSS_C_GSS_CODE, GSS_C_NO_OID, &message_context, &buf);
	printf("%.*s\n", (int)buf.length, (char *) buf.value);
	gss_release_buffer(&min_stat, &buf);
    } while (message_context);
    if (mech) {
	message_context = 0;
	do {
	    maj_stat = gss_display_status(&min_stat, min,
					  GSS_C_MECH_CODE, mech, &message_context, &buf);
	    printf("%.*s\n", (int)buf.length, (char *) buf.value);
	    gss_release_buffer(&min_stat, &buf);
	} while (message_context);
    }
    exit(1);
}

#else

void
report_error(gss_OID mech, OM_uint32 maj, OM_uint32 min)
{
    CFErrorRef err = GSSCreateError(mech, maj, min);
    CFStringRef str = CFErrorCopyDescription(err);
    char buf[512];
    CFStringGetCString(str, buf, 512, kCFStringEncodingUTF8);
    printf("%s", buf);
    exit(1);
}

#endif


void
send_token_to_peer(const gss_buffer_t token)
{
    const uint8_t *p;
    size_t i;

    printf("send token:\n");
    p = (const uint8_t *) token->value;
    int cksum = 0;
    for (i = 0; i < token->length; i++) {
	if (i > 0 && (i % 36) == 0)
	    printf("\n");
	cksum += *p;
	printf("%02x", *p++);
    }
    printf("\n\n");
    printf("checksum: %d\n", cksum);
}

void
receive_token_from_peer(gss_buffer_t token)
{
    uint8_t buf[8192];
    char line[512];
    char *p;
    uint8_t *q;
    int len, val;

    printf("receive token:\n");
    q = buf;
    int cksum = 0;
    for (;;) {
	fgets(line, sizeof(line), stdin);
	if (line[0] == '\n')
	    break;
	if (line[strlen(line) - 1] != '\n' || (strlen(line) & 1) == 0) {
	    printf("token truncated\n");
	    exit(1);
	}
	p = line;
	while (*p != '\n') {
	    int val;
	    if (sscanf(p, "%02x", &val) != 1) {
		printf("bad token\n");
		exit(1);
	    }
	    cksum += val;
	    p += 2;
	    *q++ = val;
	}
    }

    token->length = q - buf;
    token->value = malloc(token->length);
    memcpy(token->value, buf, token->length);

    printf("checksum: %d\n", cksum);
    if (0) {
	const uint8_t *p;
	size_t i;

	printf("received token:\n");
	printf("%d", (int) token->length);
	p = (const uint8_t *) token->value;
	for (i = 0; i < token->length; i++) {
	    if ((i % 36) == 0)
		printf("\n");
	    printf("%02x", *p++);
	}
	printf("\n");
    }
}

void
server(int argc, char** argv)
{
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc input_token, output_token;
    gss_ctx_id_t context_hdl = GSS_C_NO_CONTEXT;
    gss_name_t client_name;
    gss_OID mech_type;

    if (argc != 1)
	usage();

    do {
	receive_token_from_peer(&input_token);
	maj_stat = gss_accept_sec_context(&min_stat,
					  &context_hdl,
					  GSS_C_NO_CREDENTIAL,
					  &input_token,
					  GSS_C_NO_CHANNEL_BINDINGS,
					  &client_name,
					  &mech_type,
					  &output_token,
					  NULL,
					  NULL,
					  NULL);
	if (GSS_ERROR(maj_stat)) {
	    report_error(mech_type, maj_stat, min_stat);
	}
	if (output_token.length != 0) {
	    send_token_to_peer(&output_token);
	    gss_release_buffer(&min_stat, &output_token);
	}
	if (GSS_ERROR(maj_stat)) {
	    if (context_hdl != GSS_C_NO_CONTEXT)
		gss_delete_sec_context(&min_stat,
				       &context_hdl,
				       GSS_C_NO_BUFFER);
	    break;
	}
    } while (maj_stat & GSS_S_CONTINUE_NEEDED);

    if (client_name) {
	gss_buffer_desc name_desc;
	char buf[512];

	gss_display_name(&min_stat, client_name, &name_desc, NULL);
	memcpy(buf, name_desc.value, name_desc.length);
	buf[name_desc.length] = 0;
	gss_release_buffer(&min_stat, &name_desc);
	printf("client name is %s\n", buf);
    }

    receive_token_from_peer(&input_token);
    gss_unwrap(&min_stat, context_hdl, &input_token, &output_token,
	       NULL, NULL);
    printf("%.*s\n", (int)output_token.length, (char *) output_token.value);
    gss_release_buffer(&min_stat, &output_token);
}

void
client(int argc, char** argv)
{
    OM_uint32 maj_stat, min_stat;
    int context_established = 0;
    gss_ctx_id_t context_hdl = GSS_C_NO_CONTEXT;
    gss_cred_id_t cred_hdl = GSS_C_NO_CREDENTIAL;
    gss_name_t name;
    gss_buffer_desc name_desc;
    gss_buffer_desc input_token, output_token, buf;
    gss_OID mech, actual_mech;
    static gss_OID_desc krb5_desc =
	{9, (void *)"\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"};
    static gss_OID_desc spnego_desc =
	{6, (void *)"\x2b\x06\x01\x05\x05\x02"};
    static gss_OID_desc ntlm_desc =
	{10, (void *)"\x2b\x06\x01\x04\x01\x82\x37\x02\x02\x0a"};
	

    mech = GSS_C_NO_OID;
    if (argc > 1) {
	const char *mech_name = argv[1];
	if (!strcmp(mech_name, "krb5")) {
	    mech = &krb5_desc;
	} else if (!strcmp(mech_name, "spnego")) {
	    mech = &spnego_desc;
	} else if (!strcmp(mech_name, "ntlm")) {
	    mech = &ntlm_desc;
	} else {
	    printf("supported mechs: krb5 spnego ntlm\n");
	    exit(1);
	}
	argc--;
	argv++;
    }

    if (argc > 1) {
	name_desc.value = argv[1];
	argc--;
	argv++;
    } else {
	char hbuf[512];
	static char sbuf[512];
	gethostname(hbuf, sizeof(hbuf));
	snprintf(sbuf, sizeof(sbuf), "host@%s", hbuf);
	name_desc.value = sbuf;
    }

    name_desc.length = strlen((const char *) name_desc.value);
    gss_import_name(&min_stat, &name_desc, GSS_C_NT_HOSTBASED_SERVICE,
		    &name);

    input_token.length = 0;
    input_token.value = NULL;
    while (!context_established) {
	output_token.length = 0;
	output_token.value = NULL;
	maj_stat = gss_init_sec_context(&min_stat,
					cred_hdl,
					&context_hdl,
					name,
					mech,
					GSS_C_MUTUAL_FLAG|GSS_C_CONF_FLAG|GSS_C_INTEG_FLAG,
					0,
					GSS_C_NO_CHANNEL_BINDINGS,
					&input_token,
					&actual_mech,
					&output_token,
					NULL,
					NULL);
	if (GSS_ERROR(maj_stat)) {
	    report_error(mech, maj_stat, min_stat);
	}

	if (output_token.length != 0) {
	    send_token_to_peer(&output_token);
	    gss_release_buffer(&min_stat, &output_token);
	}
	if (GSS_ERROR(maj_stat)) {
	    if (context_hdl != GSS_C_NO_CONTEXT)
		gss_delete_sec_context(&min_stat,
				       &context_hdl,
				       GSS_C_NO_BUFFER);
	    break;
	}

	if (maj_stat & GSS_S_CONTINUE_NEEDED) {
	    receive_token_from_peer(&input_token);
	} else {
	    context_established = 1;
	}
    }

    buf.length = strlen("Hello world");
    buf.value = (void *) "Hello world";
    gss_wrap(&min_stat, context_hdl,
	     1, GSS_C_QOP_DEFAULT, &buf, NULL, &output_token);
    send_token_to_peer(&output_token);
    gss_release_buffer(&min_stat, &output_token);
}

int
main(int argc, char** argv)
{
    OM_uint32 min_stat;
    gss_OID_set mechs;
    int i;

    if (argc < 2)
	usage();

    setenv("KRB5_KTNAME", "test.keytab", true);

    gss_indicate_mechs(&min_stat, &mechs);
    for (i = 0; i < mechs->count; i++) {
	gss_buffer_desc b;
	gss_oid_to_str(&min_stat, &mechs->elements[i], &b);
	printf("%.*s\n", (int)b.length, (char *) b.value);
	gss_release_buffer(&min_stat, &b);
    }

    if (!strcmp(argv[1], "client"))
	client(argc - 1, argv + 1);
    else if (!strcmp(argv[1], "server"))
	server(argc - 1, argv + 1);
    else
	usage();

    return 0;
}
