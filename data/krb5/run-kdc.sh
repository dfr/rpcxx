#! /usr/bin/env bash

OS=$(uname -s | tr 'A-Z' 'a-z')
case $OS in
darwin)
    KDC='/System/Library/PrivateFrameworks/Heimdal.framework/Helpers/kdc --no-sandbox'
    KADMIN=/usr/sbin/kadmin
    ;;
freebsd)
    KDC=/usr/libexec/kdc
    KADMIN=/usr/bin/kadmin
    ;;
*)
    echo "Unsupported operating system: $OS"
    exit 1
esac

set -x
rm -f data/krb5/db.db
$KADMIN --config=data/krb5/krb5.conf --local \
    init --realm-max-ticket-life=1day --realm-max-renewable-life=1day TEST_REALM
$KADMIN --config=data/krb5/krb5.conf --local \
    load data/krb5/db.dump
exec $KDC --config=data/krb5/krb5.conf
