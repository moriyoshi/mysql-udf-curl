pkglibdir=@MYSQL_PLUGIN_DIR@
pkglib_LTLIBRARIES=curl.la
noinst_FILES=curl.la curl.a
curl_la_SOURCES=mysql_udf_curl.c
curl_la_LDFLAGS=-module -avoid-version
curl_la_LIBADD=@LIBCURL@
INCLUDES=@INCLUDES@
EXTRA_DIST=COPYING NEWS README CREDITS ChangeLog INSTALL install-sh missing mkinstalldirs
