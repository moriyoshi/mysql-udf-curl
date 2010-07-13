dnl $Id: acinclude.m4,v 1.15 2005/09/21 06:33:58 moriyoshi Exp $

AC_DEFUN([MYSQL_CHECK_CONST], [
  AC_CACHE_CHECK([$1 availability], [ac_cv_const_[]$1], [
    AC_TRY_COMPILE([$4], [
      int dummy = (int)$1;
    ], [
      ac_cv_const_[]$1=yes
    ], [
      ac_cv_const_[]$1=no
    ])
  ])
  if test "$ac_cv_const_[]$1" = "yes"; then
    ifelse([$2],[],[:],[$2])
  else
    ifelse([$3],[],[:],[$3])
  fi
])

AC_DEFUN([MYSQL_CHECK_LIBMYSQLCLIENT], [
  AC_MSG_CHECKING([if] $1 [is a mysql_config script])

  _cfg="$1"
  if test -x "$_cfg" -a -r "$_cfg" -a -f "$_cfg"; then
    dnl $1 may be a path to mysql_config
    AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_MYSQL_H], [1], [Define to `1' if you have the <mysql.h> header file.])
    mysql_config="$1"
  else
    AC_MSG_RESULT([no])
    MYSQL_LIB_DIR=
    MYSQL_INCLUDE_PATH=
    mysql_lib_name=mysqlclient

    for _pfx in $1; do
      _cfg="$_pfx/bin/mysql_config"

      AC_MSG_CHECKING([mysql_config availability in $_pfx/bin])

      if test -x "$_cfg" -a -r "$_cfg" -a -f "$_cfg"; then
        AC_MSG_RESULT([yes])
        AC_DEFINE([HAVE_MYSQL_H], [1], [Define to `1' if you have the <mysql.h> header file.])
        mysql_config="$_cfg"
        break
      else
        AC_MSG_RESULT([no])
      fi

      for dir in "$_pfx/lib" "$_pfx/lib/mysql"; do
        AC_MSG_CHECKING([$mysql_lib_name availability in $dir])
        name="$mysql_lib_name"

        if eval test -e "$dir/$libname_spec$shrext_cmds" -o -e "$dir/$libname_spec.$libext"; then
          AC_MSG_RESULT([yes])

          AC_MSG_CHECKING([$dir/$name usability])
          ac_save_LIBS="$LIBS"
          LIBS="$LIBS -L$dir"
          AC_CHECK_LIB([$mysql_lib_name], [mysql_init], [
            AC_MSG_RESULT([yes])
            MYSQL_LIB_DIR="$dir"
          ], [
            AC_MSG_RESULT([no])
          ])
          LIBS="$ac_save_LIBS"

          if test ! -z "$MYSQL_LIB_DIR"; then
            break
          fi
        else
          AC_MSG_RESULT([no])
        fi
      done

      for dir in "$_pfx/include" "$_pfx/include/mysql"; do
        AC_MSG_CHECKING([mysql headers availability in $dir])
        if test -e "$dir/mysql.h"; then
          AC_MSG_RESULT([yes])
          AC_MSG_CHECKING([mysql headers usability])
          ac_save_CPPFLAGS="$CPPFLAGS"
          CPPFLAGS="$CPPFLAGS -I$dir"
          AC_CHECK_HEADER([mysql.h], [
            AC_MSG_RESULT([yes])
            AC_DEFINE([HAVE_MYSQL_H], [1], [Define to `1' if you have the <mysql.h> header file.])
            MYSQL_INCLUDE_PATH="$dir"
          ], [
            AC_MSG_RESULT([no])
          ])
          CPPFLAGS="$ac_save_CPPFLAGS"

          if test ! -z "$MYSQL_INCLUDE_PATH"; then
            break
          fi
        else
          AC_MSG_RESULT([no])
        fi
      done
    done
  fi

  if test -z "$mysql_config"; then
    if test -z "$MYSQL_LIB_DIR" -o -z "$MYSQL_INCLUDE_PATH"; then
      AC_MSG_ERROR([Cannot locate mysql client library. Please check your mysql installation.])
    fi

    INCLUDES="$INCLUDES -I$MYSQL_INCLUDE_PATH"
    LIBS="$LIBS -L$MYSQL_LIB_DIR -l$mysql_lib_name"
    MYSQL_PLUGIN_DIR="$MYSQL_LIB_DIR/plugin"
  else
    mysql_libs="`\"$mysql_config\" --libs`"
    CFLAGS="$CFLAGS `\"$mysql_config\" --cflags`"
    LIBS="$LIBS $mysql_libs"
    MYSQL_LIB_DIR="`echo \"$mysql_libs\" | sed -e \"s/.*-L\([[^ ]]*\).*/\1/g\"`"
    MYSQL_PLUGIN_DIR="`\"$mysql_config\"` --plugindir" || MYSQL_PLUGIN_DIR="$MYSQL_LIB_DIR/plugin"
  fi

  ac_save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $INCLUDES"
  AC_CHECK_FUNCS([mysql_real_query mysql_real_escape_string make_scrambled_password_323], [], [])
  CPPFLAGS="$ac_save_CPPFLAGS"
])

AC_DEFUN([MYSQL_CHECK_DEFINES], [
  AC_FOREACH([AC_Header], [$2], [
    AH_TEMPLATE(AS_TR_CPP(HAVE_[]AC_Header),
	    [Define to 1 if ]AC_Header[ is an usable constant.])
  ])

  for ac_def in $2; do
    AC_MSG_CHECKING([$ac_def availability])
    AC_TRY_COMPILE([$1], [
int dummy = (int)$ac_def;
    ], [
      AC_MSG_RESULT([yes])
      AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_$ac_def), 1)
    ], [
      AC_MSG_RESULT([no])
    ])
  done
])

dnl vim600: sts=2 sw=2 ts=2 et
