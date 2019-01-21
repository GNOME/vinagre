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

  i18n.merge_file(
    desktop,
    type: 'desktop',
    input: desktop_in,
    output: '@BASENAME@',
    po_dir: po_dir,
    install: true,
    install_dir: vinagre_datadir / 'applications',
  )
endforeach

appdata = 'vinagre.appdata.xml'

i18n.merge_file(
  appdata,
  input: appdata + '.in',
  output: '@BASENAME@',
  po_dir: po_dir,
  install: true,
  install_dir: vinagre_datadir / 'metainfo',
)

install_data(
  vinagre_namespace + '.gschema.xml',
  install_dir: gio_schemasdir,
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

  i18n.merge_file(
    mimedata,
    input: mimedata + '.in',
    output: '@BASENAME@',
    po_dir: po_dir,
    data_dirs: its_dir,
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
    install_dir: dbus_session_bus_services_dir,
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
