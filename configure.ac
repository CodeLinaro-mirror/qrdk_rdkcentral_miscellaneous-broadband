##########################################################################
# If not stated otherwise in this file or this component's Licenses.txt
# file the following copyright and licenses apply:
#
# Copyright 2015 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################
#                                              -*- Autoconf -*-
# Process this file with autoconf to produce configure script.
#

AC_PREREQ([2.65])
AC_INIT([CcspMisc], [1.0], [BUG-REPORT-ADDRESS])
AM_INIT_AUTOMAKE([subdir-objects])
LT_INIT

AC_ARG_ENABLE([vts_bridge_util],
             AS_HELP_STRING([--enable-vts_bridge_util],[enable vts_bridge_util (default is no)]),
             [
              case "${enableval}" in
               yes) VTS_BRIDGE_UTIL_ENABLED=true;;
               no)  VTS_BRIDGE_UTIL_ENABLED=false;;
               *) AC_MSG_ERROR([bad value ${enableval} for --enable-vts_bridge_util]);;
              esac
             ],
             [echo "vts_bridge_util is disabled"])
AM_CONDITIONAL(FEATURE_VTS_BRIDGE_UTIL, test "x$VTS_BRIDGE_UTIL_ENABLED" = xtrue)

AC_ARG_ENABLE([onestacksupport],
             AS_HELP_STRING([--enable-onestacksupport],[enable onestack support (default is no)]),
             [
              case "${enableval}" in
               yes) ONESTACK_PRODUCT_REQ=true;;
               no)  ONESTACK_PRODUCT_REQ=false;;
               *) AC_MSG_ERROR([bad value ${enableval} for --enable-onestacksupport ]);;
              esac
             ],
             [echo "onestack is disabled"])
AM_CONDITIONAL(ONESTACK_PRODUCT_REQ, test "x$ONESTACK_PRODUCT_REQ" = xtrue)

# Checks for Core net lib Feature Support
AC_ARG_ENABLE([core_net_lib_feature_support],
[  --enable-core_net_lib_feature_support=val Turn on core_net_lib Feature, val=true or false],
[case "${enableval}" in
  yes) core_net_lib_feature_support=true 
  	GTEST_CORE_NET_LIB="-DCORE_NET_LIB";;
  no)  core_net_lib_feature_support=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-core_net_lib_feature_support]) ;;
esac],[core_net_lib_feature_support=false])
AM_CONDITIONAL(CORE_NET_LIB_FEATURE_SUPPORT, test x"$core_net_lib_feature_support" = x"true")
AC_SUBST(GTEST_CORE_NET_LIB)

AC_PREFIX_DEFAULT(`pwd`)
AC_ENABLE_SHARED
AC_DISABLE_STATIC

AC_ARG_ENABLE([notifylease],
[  --enable-notifylease    Turn on leasenotify ],
[case "${enableval}" in
  yes) NOTIFYLEASE_ENABLE=true ;;
  no)  NOTIFYLEASE_ENABLE=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-notifylease]) ;;
esac],[NOTIFYLEASE_ENABLE=false])
AM_CONDITIONAL([NOTIFYLEASE_ENABLE], [test x$NOTIFYLEASE_ENABLE = xtrue])

AC_ARG_ENABLE([setLED],
 [AS_HELP_STRING([--enable-setLED], [Turn on setLED])], ,
 [setled_enable=true])
AM_CONDITIONAL([SETLED_ENABLE], [test x$setled_enable = xtrue])

AC_ARG_ENABLE([multipartUtilEnable],
[  --enable-multipartUtilEnable    Turn on multipartUtility ],
[case "${enableval}" in
  yes) MULTIPART_UTIL_ENABLE=true ;;
  no)  MULTIPART_UTIL_ENABLE=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-multipartUtilEnable]) ;;
esac],[MULTIPART_UTIL_ENABLE=false])
AM_CONDITIONAL([MULTIPART_UTIL_ENABLE], [test x$MULTIPART_UTIL_ENABLE = xtrue])

AC_ARG_ENABLE([bridgeUtilsBin],
[  --enable-bridgeUtilsBin    Turn on bridgeUtilsBin ],
[case "${enableval}" in
  yes) BRIDGE_UTILS_BIN_ENABLE=true ;;
  no)  BRIDGE_UTILS_BIN_ENABLE=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-bridgeUtilsBin]) ;;
esac],[BRIDGE_UTILS_BIN_ENABLE=false])
AM_CONDITIONAL([BRIDGE_UTILS_BIN_ENABLE], [test x$BRIDGE_UTILS_BIN_ENABLE = xtrue])

AC_ARG_ENABLE([wbCfgTestAppEnable],
[  --enable-wbCfgTestAppEnable    Turn on wbCfgTestApp ],
[case "${enableval}" in
  yes) WEBCFG_TESTAPP_ENABLE=true ;;
  no)  WEBCFG_TESTAPP_ENABLE=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-wbCfgTestAppEnable]) ;;
esac],[WEBCFG_TESTAPP_ENABLE=false])
AM_CONDITIONAL([WEBCFG_TESTAPP_ENABLE], [test x$WEBCFG_TESTAPP_ENABLE = xtrue])

AC_ARG_ENABLE([rdkSchedulerTestAppEnable],
[  --enable-rdkSchedulerTestAppEnable    Turn on rdkSchedulerTestAppEnable ],
[case "${enableval}" in
  yes) RDKSCHEDULER_TESTAPP_ENABLE=true ;;
  no)  RDKSCHEDULER_TESTAPP_ENABLE=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-wbCfgTestAppEnable]) ;;
esac],[RDKSCHEDULER_TESTAPP_ENABLE=false])
AM_CONDITIONAL([RDKSCHEDULER_TESTAPP_ENABLE], [test x$RDKSCHEDULER_TESTAPP_ENABLE = xtrue])


AC_ARG_ENABLE([socketExampleEnable],
[  --enable-socketExampleEnable    Turn on socketExampleEnable],
[case "${enableval}" in
  yes) SOCKET_EXAMPLE_ENABLE=true ;;
  no)  SOCKET_EXAMPLE_ENABLE=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-socketExampleEnable]) ;;
esac],[SOCKET_EXAMPLE_ENABLE=false])
AM_CONDITIONAL([SOCKET_EXAMPLE_ENABLE], [test x$SOCKET_EXAMPLE_ENABLE = xtrue])

AC_ARG_ENABLE([unitTestDockerSupport],
             AS_HELP_STRING([--enable-unitTestDockerSupport],[enable docker support for unit testing (default is no)]),
             [
              UNIT_TEST_DOCKER_SUPPORT=""
               case "${enableval}" in
                yes) UNIT_TEST_DOCKER_SUPPORT=true
                     m4_if(m4_sysval,[0],[AC_CONFIG_FILES([source/test/Makefile source/test/bridge_utils/Makefile])]);;
                no) UNIT_TEST_DOCKER_SUPPORT=false;;
                 *) AC_MSG_ERROR([bad value ${enableval} for --enable-unitTestDockerSupport ]);;
               esac
             ],
             [echo "Docker support is disabled"])
AM_CONDITIONAL([UNIT_TEST_DOCKER_SUPPORT], [test x$UNIT_TEST_DOCKER_SUPPORT = xtrue])

AC_ARG_ENABLE([dhcp_manager],
[  --enable-dhcp_manager    Turn on dhcp_manager ],
[case "${enableval}" in
yes) DHCP_MANAGER_ENABLE=true ;;
no)  DHCP_MANAGER_ENABLE=false ;;
*) AC_MSG_ERROR([bad value ${enableval} for --enable-dhcp_manager]) ;;
esac],[DHCP_MANAGER_ENABLE=false])
AM_CONDITIONAL([DHCP_MANAGER_ENABLE], [test x$DHCP_MANAGER_ENABLE = xtrue])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AM_PROG_CC_C_O
AM_PROG_LIBTOOL(libtool)

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE

# Checks for library functions.
AC_FUNC_MALLOC

AC_CONFIG_FILES(
	source/Psmcli/Makefile
	source/LTime/Makefile
	source/TimeConv/Makefile
  source/FwDownloadChk/Makefile
	source/parcon/Makefile
	source/ServiceCtrl/Makefile
	source/webcfg_decoder/Makefile
	source/MemFrag_Calc/Makefile
	source/EventSubscription/Makefile
	source/Makefile
	source/dhcp_client_utils/Makefile
	source/msgq_util/Makefile
  source/FwBankInfo/Makefile
	Makefile
)

AM_COND_IF([NOTIFYLEASE_ENABLE], [AC_CONFIG_FILES([source/Notify_Lease/Makefile])])

AM_COND_IF([SETLED_ENABLE], [AC_CONFIG_FILES([source/SetLED/Makefile])])

AM_COND_IF([MULTIPART_UTIL_ENABLE], [AC_CONFIG_FILES([source/multipartRoot/Makefile])])

AM_COND_IF([WEBCFG_TESTAPP_ENABLE], [AC_CONFIG_FILES([source/wbCfgTestDaemon/Makefile] [source/wbCfgTestApp/Makefile] )])

AM_COND_IF([BRIDGE_UTILS_BIN_ENABLE], [AC_CONFIG_FILES([source/bridge_utils/bridge_utils_bin/Makefile] [source/bridge_utils/Makefile] )])

AM_COND_IF([RDKSCHEDULER_TESTAPP_ENABLE], [AC_CONFIG_FILES([source/RdkSchdeuler_TestApp/Makefile])])

AM_COND_IF([SOCKET_EXAMPLE_ENABLE], [AC_CONFIG_FILES([source/Socket_Example/Makefile])])

AC_SUBST(GTEST_CORE_NET_LIB)

AC_SUBST(UNIT_TEST_DOCKER_SUPPORT)

AC_OUTPUT

