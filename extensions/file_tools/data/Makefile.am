SUBDIRS = ui

schemadir = @GCONF_SCHEMA_FILE_DIR@
schema_in_files = gthumb_crop_options.schemas.in gthumb_resize_options.schemas.in
schema_DATA = $(schema_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

if GCONF_SCHEMAS_INSTALL
install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/extensions/file_tools/data/$(schema_DATA)
endif

EXTRA_DIST = $(schema_in_files)
CLEANFILES = $(schema_DATA)

-include $(top_srcdir)/git.mk
