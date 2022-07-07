# SPDX-License-Identifier: GPL-2.0-or-later
CMAKE_MINIMUM_REQUIRED(VERSION 3.17)

PROJECT(AirVoice C)
ADD_DEFINITIONS(-Wall -Wextra -Wno-unused-parameter -Wstrict-aliasing -Wunreachable-code -Wcast-align -Wcast-qual -Wformat=2 -Winit-self -Wlogical-op -Wmissing-include-dirs -Wredundant-decls -Wshadow -Wstrict-overflow=5 -Wswitch-default -Wundef -Wformat-nonliteral -fdiagnostics-show-option)

FIND_PACKAGE(PkgConfig REQUIRED)

SET(CMAKE_INCLUDE_CURRENT_DIR ON)

PKG_SEARCH_MODULE(GLIB REQUIRED glib-2.0>=2.64.2)
PKG_SEARCH_MODULE(GIO REQUIRED gio-2.0)
PKG_SEARCH_MODULE(MM-GLIB REQUIRED mm-glib>=1.12.8)
PKG_SEARCH_MODULE(LIBCONFIG REQUIRED libconfig>=1.7.2)
PKG_SEARCH_MODULE(BCTOOLBOX REQUIRED bctoolbox>=4.4.0)
PKG_SEARCH_MODULE(ORTP REQUIRED ortp>=4.4.0)

# eXosip2 and osip2
FIND_PATH(eXosip2_include_dir eXosip2/eXosip.h)
FIND_PATH(osip2_include_dir osip2/osip.h)
FIND_PATH(osipparser2_include_dir osipparser2/osip_parser.h)

SET(SOURCES
	# Skeleton
	av.c

	# common utilities
	av_utils.c

	# Objects storage
	av_storage.c

	# AV Threads. Yes, I tought I could do without threads, but instead...
	av_thread.c

	# Threads communication support
	av_threadcomm.c

	# AvModem object definition
	av_modem_gobject.c

	# MM interaction base structure
	av_mm.c

	# MM Manager object handling
	av_mm_manager.c

	# MMModem object handling code
	av_mm_modem.c

	# MMModemVoice object
	av_mm_voice.c

	# MMCall object
	av_mm_call.c

	# SIP communications
	av_sip.c

	# Configuration file
	av_config.c

	# audio thread
	av_audio.c
)

SET(LIBS
	eXosip2 osip2 osipparser2)

IF(DEBUG)
  ADD_DEFINITIONS(-g3 -ggdb)
  IF(NO_OPTIMIZE)
    ADD_DEFINITIONS(-O0)
  ENDIF()
ENDIF()

IF(AV_SIP_DEBUG)
	ADD_DEFINITIONS(-D AV_SIP_DEBUG)
ENDIF()

ADD_EXECUTABLE(av ${SOURCES} ${GLIB_LIBRARY} ${GIO_LIBRARY} ${MM-GLIB_LIBRARY} ${LIBCONFIG_LIBRARY} ${ORTP_LIBRARY} ${BCTOOLBOX_LIBRARY})

TARGET_LINK_LIBRARIES(av ${LIBS} ${GLIB_LDFLAGS} ${GIO_LDFLAGS} ${MM-GLIB_LDFLAGS} ${LIBCONFIG_LDFLAGS} ${ORTP_LDFLAGS} ${BCTOOLBOX_LDFLAGS})

TARGET_INCLUDE_DIRECTORIES(av PRIVATE ${GLIB_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS} ${MM-GLIB_INCLUDE_DIRS} ${LIBCONFIG_INCLUDE_DIRS} ${ORTP_INCLUDE_DIRS} ${BCTOOLBOX_INCLUDE_DIRS})
INCLUDE_DIRECTORIES(${eXosip2_include_dir})
INCLUDE_DIRECTORIES(${osip2_include_dir})
INCLUDE_DIRECTORIES(${osipparser2_include_dir})

INSTALL(TARGETS av
	RUNTIME DESTINATION bin
)
