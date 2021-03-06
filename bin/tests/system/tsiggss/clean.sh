#!/bin/sh
#
# Copyright (C) 2010, 2011, 2014-2016  Internet Systems Consortium, Inc. ("ISC")
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#
# Clean up after tsiggss tests.
#

rm -f ns1/*.jnl ns1/update.txt ns1/auth.sock
rm -f ns1/*.db ns1/K*.key ns1/K*.private
rm -f ns1/_default.tsigkeys
rm -f */named.memstats
rm -f */named.run
rm -f authsock.pid
rm -f ns1/core
rm -f nsupdate.out
rm -f ns*/named.lock
