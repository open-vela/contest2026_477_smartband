# ##############################################################################
# Native openvela smart-band UI demo
# ##############################################################################

if(CONFIG_LVX_USE_DEMO_OPENVELA_UI)
  set(PROGNAME openvela_ui)
  set(PRIORITY 100)
  set(STACKSIZE 65536)
  set(MODULE ${CONFIG_LVX_USE_DEMO_OPENVELA_UI})
  set(MAINSRC openvela_ui_main_mooncat.c)
  set(UI_SRCS openvela_ui.c openvela_ui_weather.c openvela_ui_sport.c
              openvela_ui_sync.c openvela_ui_agent_bridge.c)
  set(UI_INCDIRS)

  if(CONFIG_EXAMPLES_AI_AGENT_VELA)
    list(APPEND UI_INCDIRS
         ${CMAKE_CURRENT_LIST_DIR}/../../ai_agent/include
         ${CMAKE_CURRENT_LIST_DIR}/../../ai_agent/src)
  endif()

  if(CONFIG_OPENVELA_UI_HTTP_TIMESYNC)
    list(APPEND UI_SRCS openvela_ui_timesync.c)
  endif()

  nuttx_add_application(
    NAME ${PROGNAME}
    PRIORITY ${PRIORITY}
    STACKSIZE ${STACKSIZE}
    MODULE ${MODULE}
    SRCS ${MAINSRC} ${UI_SRCS}
    INCLUDE_DIRECTORIES ${UI_INCDIRS}
  )
endif()
