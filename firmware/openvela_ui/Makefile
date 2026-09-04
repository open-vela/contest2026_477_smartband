include $(APPDIR)/Make.defs

PROGNAME  = openvela_ui
PRIORITY  = 100
STACKSIZE = 65536
MODULE    = $(CONFIG_LVX_USE_DEMO_OPENVELA_UI)

MAINSRC = openvela_ui_main_mooncat.c
CSRCS   = openvela_ui.c openvela_ui_weather.c openvela_ui_sport.c \
	  openvela_ui_sync.c openvela_ui_agent_bridge.c

ifeq ($(CONFIG_EXAMPLES_AI_AGENT_VELA),y)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/packages/ai_agent/src
endif

ifeq ($(CONFIG_OPENVELA_UI_HTTP_TIMESYNC),y)
CSRCS += openvela_ui_timesync.c
endif

include $(APPDIR)/Application.mk
