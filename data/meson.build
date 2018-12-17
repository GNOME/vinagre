desktop_conf = configuration_data()
desktop_conf.set('VERSION', meson.project_version())

desktops = [
  'vinagre.desktop',
  'vinagre-file.desktop',
]

foreach desktop: desktops
  desktop_in = configure_file(
    input: desktop + '.in.in',
    output: '@BASENAME@',
    configuration: desktop_conf,
  )

  custom_target(
    desktop,
    input: desktop_in,
    output: '@BASENAME@',
    command: intltool_desktop_cmd,
    install: true,
    install_dir: vinagre_datadir / 'applications',
  )
endforeach

appdata = 'vinagre.appdata.xml'

custom_target(
  appdata,
  input: appdata + '.in',
  output: '@BASENAME@',
  command: intltool_xml_cmd,
  install: true,
  install_dir: vinagre_datadir / 'metainfo',
)

install_data(
  vinagre_namespace + '.gschema.xml',
  install_dir: schemasdir,
)

install_data(
  vinagre_namespace + '.convert',
  install_dir: vinagre_datadir / 'GConf/gsettings',
)

ui_data = files(
  'vinagre.ui',
  'vinagre-ui.xml',
)

install_data(
  ui_data,
  install_dir: vinagre_pkgdatadir,
)

install_man('vinagre.1')

if have_telepathy
  mimedata = 'vinagre-mime.xml'

  custom_target(
    mimedata,
    input: mimedata + '.in',
    output: '@BASENAME@',
    command: intltool_xml_cmd,
    install: true,
    install_dir: vinagre_datadir / 'mime/packages',
  )

  service_conf = configuration_data()
  service_conf.set('bindir', vinagre_prefix / vinagre_bindir)

  configure_file(
    input: 'org.freedesktop.Telepathy.Client.Vinagre.service.in',
    output: '@BASENAME@',
    configuration: service_conf,
    install: true,
    install_dir: dbus_servicedir,
  )
endif

foreach size: ['16x16', '22x22', '24x24', '32x32', '48x48']
  mimetype_path = 'icons' / size / 'mimetypes'
  mimetype_data = [
    mimetype_path / 'application-x-remote-connection.png',
    mimetype_path / 'application-x-vnc.png',
  ]

  install_data(
    mimetype_data,
    install_dir: vinagre_datadir / 'icons/hicolor' / size / 'mimetypes',
  )
endforeach

mimetype_path = 'icons/scalable/mimetypes'
mimetype_data = [
  mimetype_path / 'application-x-remote-connection.svg',
  mimetype_path / 'application-x-vnc.svg',
]

install_data(
  mimetype_data,
  install_dir: vinagre_datadir / 'icons/hicolor/scalable/mimetypes',
)

foreach size: ['16x16', '22x22', '32x32', '48x48']
  install_data(
    'icons' / size / 'status/view-minimize.png',
    install_dir: vinagre_datadir / 'icons/hicolor' / size / 'status',
  )
endforeach
