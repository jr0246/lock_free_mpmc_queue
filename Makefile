BDIR:=./build
ISBOOSTAVAILABLE:=$(shell if [ -f /usr/local/boost_1_90_0/boost/lockfree/queue.hpp ] ; then echo 1 ; else echo 0 ; fi)
CXXFLAGS:= --std=c++23 -mcx16 -pthread -W -Wall -Wshadow -Wextra -Wpedantic -I. -Isrc -O3 -march=native -mtune=native -flto
LINK.o := $(LINK.cc)

$(BDIR)/q_bandwidth.o: src/q_bandwidth.cpp src/lock_free_mpmc_queue.hpp src/mpmc_queue_timing.h | $(BDIR)/.f
	$(CXX) $(CXXFLAGS) -I. -Isrc -DCOMPARE_BOOST=$(ISBOOSTAVAILABLE) -pthread $< -c -o $@

$(BDIR)/q_bandwidth: $(BDIR)/q_bandwidth.o

.PHONY: report
report: | $(BDIR)/q_bandwidth
	./scripts/bw-report.sh ./build/q_bandwidth

$(BDIR)/.f:
	@mkdir -p $(dir $@)
	@touch $@

.PRECIOUS: %/.f

cmake:
	mkdir -p cbuild
	(cd cbuild ; CXX=$(CXX) CC=$(CC) cmake .. ; CXX=$(CXX) CC=$(CC) make -j )