cdata = configuration_data()
cdata.set('PACKAGE', '"@0@"'.format(meson.project_name()))
cdata.set('VERSION', '"@0@"'.format(meson.project_version()))

doxygen = find_program('doxygen', required : true)

if find_program('dot', required : false).found()
  cdata.set('HAVE_DOT', 'YES')
else
  cdata.set('HAVE_DOT', 'NO')
endif

cdata.set('TOP_SRCDIR', join_paths(meson.project_source_root()))
cdata.set('TOP_BUILDDIR', meson.project_build_root())

doxyfile = configure_file(input: 'Doxyfile.in',
                          output: 'Doxyfile',
                          configuration: cdata,
                          install: false)

xml_target = custom_target('media-library-docs',
                            input: doxyfile,
                            output: 'xml',
                            command: [doxygen, doxyfile])