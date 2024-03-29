dnl ***************************
dnl *** Initialize autoconf ***
dnl ***************************
AC_INIT([gooroom-notifyd], [0.4.3])
AC_PREREQ(2.53)

dnl ***********************
dnl Initialize automake ***
dnl ***********************
AM_INIT_AUTOMAKE([1.8 dist-xz no-dist-gzip foreign])
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])
m4_define([notifications_spec_version], [1.2])

dnl ********************************
dnl *** Check for basic programs ***
dnl ********************************
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LD()
AC_PROG_INSTALL()
IT_PROG_INTLTOOL([0.35.0])

dnl **********************
dnl Initialize libtool ***
dnl **********************
LT_PREREQ([2.2.6])
LT_INIT([disable-static])

dnl **********************************
dnl *** Check for standard headers ***
dnl **********************************
AC_HEADER_STDC()
AC_CHECK_HEADERS([errno.h fcntl.h  grp.h signal.h stdlib.h string.h \
                  sys/stat.h sys/time.h sys/types.h sys/wait.h \
                  unistd.h])

dnl ******************************
dnl *** Check for i18n support ***
dnl ******************************
GETTEXT_PACKAGE=gooroom-notifyd
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Gettext Package])
AM_GLIB_GNU_GETTEXT

dnl ***************************
dnl *** check for GSettings ***
dnl ***************************
GLIB_GSETTINGS

dnl create #define for spec version
AC_DEFINE_UNQUOTED([NOTIFICATIONS_SPEC_VERSION], ["notifications_spec_version"],
                   [Set to the version of the freedesktop.org notifications spec we support])

dnl ***********************************
dnl *** Check for required packages ***
dnl ***********************************
PKG_CHECK_MODULES(GTK, gtk+-3.0 >= 3.20.0)
PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.44.0)
PKG_CHECK_MODULES(GIO, gio-2.0)
PKG_CHECK_MODULES(GIO_UNIX, gio-unix-2.0 >= 2.42.0)
PKG_CHECK_MODULES(LIBNOTIFY, libnotify >= 0.7.0)
PKG_CHECK_MODULES(X11, x11 >= 1.6.7)

AC_OUTPUT([
  Makefile
  src/Makefile
  po/Makefile.in
])
