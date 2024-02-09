dbd/server: dbd/server/app dbd/server/lib

ifndef DEBUDA_SERVER_LIB_SRCS
  include $(BUDA_HOME)/dbd/server/lib/module.mk
endif
include $(BUDA_HOME)/dbd/server/unit_tests/module.mk
include $(BUDA_HOME)/dbd/server/app/module.mk
