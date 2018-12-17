# vala sources
sources = files(
  'vinagre-dirs.vala',
  'vinagre-utils.vala',
)

deps = [
  config_dep,
  gtk_dep,
]

c_flags = [
  '-DDATADIR="@0@"'.format(vinagre_prefix / vinagre_datadir),
  '-DLIBDIR="@0@"'.format(vinagre_prefix / vinagre_libdir),
  '-DVINAGRE_DATADIR="@0@"'.format(vinagre_prefix / vinagre_pkgdatadir),
  '-DGETTEXT_PACKAGE="@0@"'.format(vinagre_name),
]

libvinagre = static_library(
  vinagre_name,
  sources,
  include_directories: top_inc,
  vala_header: 'vinagre-vala.h',
  dependencies: deps,
  c_args: c_flags,
)

sources = vnc_sources + files(
  'view/autoDrawer.c',
  'view/drawer.c',
  'view/ovBox.c',
  'vinagre-bookmarks.c',
  'vinagre-bookmarks-entry.c',
  'vinagre-bookmarks-migration.c',
  'vinagre-bookmarks-tree.c',
  'vinagre-bookmarks-ui.c',
  'vinagre-cache-prefs.c',
  'vinagre-commands.c',
  'vinagre-connect.c',
  'vinagre-connection.c',
  'vinagre-debug.c',
  'vinagre-main.c',
  'vinagre-notebook.c',
  'vinagre-options.c',
  'vinagre-plugins-engine.c',
  'vinagre-prefs.c',
  'vinagre-protocol.c',
  'vinagre-reverse-vnc-listener.c',
  'vinagre-reverse-vnc-listener-dialog.c',
  'vinagre-ssh.c',
  'vinagre-static-extension.c',
  'vinagre-tab.c',
  'vinagre-window.c',
)

incs = [
  top_inc,
  vnc_inc,
]

deps = [
  gio_dep,
  glib_dep,
  gthread_dep,
  gtk_dep,
  gtk_vnc_dep,
  libsecret_dep,
  libxml_dep,
]

c_flags = [
  '-DPACKAGE_LOCALE_DIR="@0@"'.format(vinagre_prefix / vinagre_localedir),
  '-DSSH_PROGRAM="@0@"'.format(ssh.path()),
]

if not have_ifaddrs
  sources += files('if/getifaddrs.c')
endif

if have_pty_open
  sources += files('pty_open.c')
endif

if have_login_tty
  deps += util_dep
endif

if have_socket
  deps += socket_deps
endif

if have_avahi
  sources += files('vinagre-mdns.c')

  deps += [
    avahi_gobject_dep,
    avahi_ui_gtk_dep,
  ]
endif

# Telepathy stuff
if have_telepathy
  sources += files(
    'vinagre-tube-handler.c',
    'vinagre-tubes-manager.c',
  )

  deps += [
    dbus_glib_dep,
    telepathy_glib_dep,
  ]
endif

if have_rdp
  sources += rdp_sources
  incs += rdp_inc

  deps += [
    freerdp_dep,
    m_dep,
    x11_dep,
  ]
endif

if have_spice
  sources += spice_sources
  incs += spice_inc
  deps += spice_client_gtk_dep
endif

if have_ssh
  sources += ssh_sources
  incs += ssh_inc
  deps += vte_dep
endif

executable(
  vinagre_name,
  sources: sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: c_flags,
  link_with: libvinagre,
  install: true,
)
