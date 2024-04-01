#
# "main" pseudo-component makefile.
#
# (Uses default behaviour of compiling all source files in directory, adding 'include' to include path.)

ifdef CONFIG_AUDIO_BOARD_CUSTOM
COMPONENT_ADD_INCLUDEDIRS += ./max_9867_driver
COMPONENT_SRCDIRS += ./max_9867_driver

COMPONENT_ADD_INCLUDEDIRS += ./kitzune_v1_0
COMPONENT_SRCDIRS += ./kitzune_v1_0
endif
