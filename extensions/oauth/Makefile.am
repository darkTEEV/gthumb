if ENABLE_WEB_SERVICES

SUBDIRS = data

extensiondir = $(pkglibdir)/extensions
extension_LTLIBRARIES = liboauth.la

liboauth_la_SOURCES = 				\
	main.c					\
	oauth-account.c				\
	oauth-account.h				\
	oauth-account-chooser-dialog.c		\
	oauth-account-chooser-dialog.h		\
	oauth-account-manager-dialog.c		\
	oauth-account-manager-dialog.h		\
	oauth-authentication.c			\
	oauth-authentication.h			\
	oauth-connection.c			\
	oauth-connection.h

liboauth_la_CFLAGS = $(GTHUMB_CFLAGS) $(LIBSOUP_CFLAGS) $(GNOME_KEYRING_CFLAGS) -I$(top_srcdir) -I$(top_builddir)/gthumb 
liboauth_la_LDFLAGS = $(EXTENSION_LIBTOOL_FLAGS)
liboauth_la_LIBADD = $(GTHUMB_LIBS) $(JPEG_LIBS) $(GNOME_KEYRING_LIBS)
liboauth_la_DEPENDENCIES = $(top_builddir)/gthumb/gthumb$(EXEEXT)

extensioninidir = $(extensiondir)
extensionini_in_files = oauth.extension.in.in
extensionini_DATA = $(extensionini_in_files:.extension.in.in=.extension)

@GTHUMB_EXTENSION_IN_RULE@
@GTHUMB_EXTENSION_RULE@

EXTRA_DIST = $(extensionini_in_files) 

DISTCLEANFILES = $(extensionini_DATA)

endif

-include $(top_srcdir)/git.mk
