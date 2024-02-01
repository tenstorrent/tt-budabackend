include $(BUDA_HOME)/infra/common.mk

all ::
	$(MAKE) -C stream_test
	$(MAKE) -C packet_test
	$(MAKE) -C sw_load_test
	$(MAKE) -C eth_bw_test

stream_test_app ::
	$(MAKE) -C stream_test --always-make

packet_test_app ::
	$(MAKE) -C packet_test --always-make

sw_load_test_app ::
	$(MAKE) -C sw_load_test --always-make

eth_bw_test_app ::
	$(MAKE) -C eth_bw_test --always-make

clean ::
	$(MAKE) -C stream_test clean
	$(MAKE) -C packet_test clean
	$(MAKE) -C sw_load_test clean
	$(MAKE) -C eth_bw_test clean
