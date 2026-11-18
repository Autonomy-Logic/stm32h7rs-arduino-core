# SPDX-License-Identifier: Apache-2.0
# Host tests of core code that needs no hardware; runs on Linux and macOS.

CXXFLAGS := -std=gnu++17 -O1 -Wall -Wextra -Werror -I../../../libraries/Ethernet/src

.PHONY: check clean

test_client_table: test_client_table.cpp ../../../libraries/Ethernet/src/ClientTable.h
	$(CXX) $(CXXFLAGS) -o $@ test_client_table.cpp

check: test_client_table
	./test_client_table

clean:
	rm -f test_client_table
