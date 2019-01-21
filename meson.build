project(
  'Vinagre', ['c', 'vala'],
  version: '3.22.0',
  license: 'GPL2',
  default_options: 'buildtype=debugoptimized',
  meson_version: '>= 0.49.0',
)

vinagre_name = meson.project_name().to_lower()

vinagre_prefix = get_option('prefix')
vinagre_bindir = get_option('bindir')
vinagre_datadir = get_option('datadir')
vinagre_libdir = get_option('libdir')
vinagre_localedir = get_option('localedir')
vinagre_sysconfdir = get_option('sysconfdir')

vinagre_pkgdatadir = vinagre_datadir / vinagre_name

vinagre_namespace = 'org.gnome.' + meson.project_name()

gnome = import('gnome')
i18n = import('i18n')

source_root = meson.current_source_dir()

its_dir = source_root / 'gettext'
po_dir = source_root / 'po'
vapi_dir = source_root / 'vinagre/vapi'

top_inc = include_directories('.')

cc = meson.get_compiler('c')
valac = meson.get_compiler('vala')

config_h = configuration_data()

# package defines
set_defines = [
  # package
  ['PACKAGE_NAME', meson.project_name()],
  ['PACKAGE_TARNAME', vinagre_name],
  ['PACKAGE_URL', 'https://wiki.gnome.org/Apps/Vinagre'],
  ['PACKAGE_VERSION', meson.project_version()],
  # i18n
  ['GETTEXT_PACKAGE', vinagre_name],
]

foreach define: set_defines
  config_h.set_quoted(define[0], define[1])
endforeach

# Checks for header files. See vinagre/pyt_open.c for usage.
check_headers = [
  'ifaddrs.h',
  'stropts.h',
  'sys/un.h',
  'termios.h',
  'util.h',
  'utmp.h',
]

foreach header: check_headers
  config_h.set('HAVE_' + header.underscorify().to_upper(), cc.has_header(header))
endforeach

# Checks for library functions. See vinagre/pty_open.c for usage.
check_functions = [
  'getpt',
  'posix_openpt',
  'ptsname',
  'ptsname_r',
  'unlockpt',
]

foreach function: check_functions
  config_h.set('HAVE_' + function.underscorify().to_upper(), cc.has_function(function))
endforeach

have_pty_open = cc.has_function('grantpt')
config_h.set('HAVE_GRANTPT', have_pty_open)

# Checks for typedefs, structures, and compiler characteristics.
check_types = [
  # type, fallback type
  ['pid_t', 'int'],
  ['size_t', 'unsigned int'],
  ['ssize_t', 'int'],
  # FIXME: UINT64_T is missing
]

foreach type: check_types
  if not cc.has_type(type[0], prefix: '#include<sys/types.h>')
    config_h.set(type[0], type[1])
  endif
endforeach

# compiler flags
common_flags = [
  '-DHAVE_CONFIG_H',
  # deprecates
  '-DBONOBO_DISABLE_DEPRECATED',
  '-DBONOBO_DISABLE_SINGLE_INCLUDES',
  '-DBONOBO_UI_DISABLE_DEPRECATED',
  '-DBONOBO_UI_DISABLE_SINGLE_INCLUDES',
  '-DGCONF_DISABLE_DEPRECATED',
  '-DGCONF_DISABLE_SINGLE_INCLUDES',
  '-DGNOME_DISABLE_DEPRECATED',
  '-DGNOME_DISABLE_SINGLE_INCLUDES',
  '-DGNOME_VFS_DISABLE_DEPRECATED',
  '-DGNOME_VFS_DISABLE_SINGLE_INCLUDES',
  '-DLIBGLADE_DISABLE_DEPRECATED',
  '-DLIBGLADE_DISABLE_SINGLE_INCLUDES',
  '-DLIBSOUP_DISABLE_DEPRECATED',
  '-DLIBSOUP_DISABLE_SINGLE_INCLUDES',
  '-DWNCK_DISABLE_DEPRECATED',
  '-DWNCK_DISABLE_SINGLE_INCLUDES',
]

if get_option('buildtype').contains('debug')
  common_flags += cc.get_supported_arguments([
    '-Wnested-externs',
    '-Wstrict-prototypes',
    '-Werror=format=2',
    '-Werror=implicit-function-declaration',
    '-Werror=init-self',
    '-Werror=missing-include-dirs',
    '-Werror=missing-prototypes',
    '-Werror=pointer-arith',
    '-Werror=return-type',
  ])
endif

add_project_arguments(common_flags, language: 'c')

# Check required libraries
glib_req_version = '>= 2.32.0'
gio_dep_name = (host_machine.system() == 'windows') ? 'gio-windows-2.0' : 'gio-unix-2.0'

gio_dep = dependency(gio_dep_name, version: glib_req_version)
glib_dep = dependency('glib-2.0', version: glib_req_version)
gthread_dep = dependency('gthread-2.0', version: '>= 2.0.0')
gtk_dep = dependency('gtk+-3.0', version: '>= 3.9.6')
libsecret_dep = dependency('libsecret-1')
libxml_dep = dependency('libxml-2.0', version: '>= 2.6.31')

gtk_vnc_dep = dependency('gtk-vnc-2.0', version: '>= 0.4.3')

config_dep = valac.find_library('config', dirs: vapi_dir)

dbus_session_bus_services_dir = dependency('dbus-1').get_pkgconfig_variable(
  'session_bus_services_dir',
  define_variable: ['datadir', vinagre_prefix / vinagre_datadir],
)

gio_schemasdir = dependency('gio-2.0').get_pkgconfig_variable(
  'schemasdir',
  define_variable: ['datadir', vinagre_prefix / vinagre_datadir],
  default: vinagre_prefix / vinagre_datadir / 'glib-2.0/schemas',
)

# Whether to enable support for SSH.
vte_dep = dependency('vte-2.91', version: '>= 0.20', required: get_option('ssh'))
have_ssh = vte_dep.found()
if have_ssh
  ssh = find_program('ssh')
endif

enable_rdp = get_option('rdp')
freerdp_dep = dependency('freerdp', required: enable_rdp)
x11_dep = dependency('x11', required: enable_rdp)
have_rdp = freerdp_dep.found() and x11_dep.found()
if have_rdp
  m_dep = cc.find_library('m')
endif
config_h.set10('HAVE_FREERDP_1_1', have_rdp and freerdp_dep.version().version_compare('>= 1.1'))

# Check for spice.
spice_client_gtk_dep = dependency('spice-client-gtk-3.0', version: '>= 0.5', required: get_option('spice'))
have_spice = spice_client_gtk_dep.found()
config_h.set('HAVE_SPICE_CLIENT_GTK_0_32', have_spice and spice_client_gtk_dep.version().version_compare('>= 0.32'))

# Check for Avahi.
enable_avahi = get_option('avahi')
avahi_gobject_dep = dependency('avahi-gobject', version: '>= 0.6.26', required: enable_avahi)
avahi_ui_gtk_dep = dependency('avahi-ui-gtk3', version: '>= 0.6.26', required: enable_avahi)
have_avahi = avahi_ui_gtk_dep.found() and avahi_gobject_dep.found()
config_h.set('VINAGRE_HAVE_AVAHI', have_avahi)

# Check for telepathy-glib.
enable_telepathy = get_option('telepathy')
dbus_glib_dep = dependency('dbus-glib-1', required: enable_telepathy)
telepathy_glib_dep = dependency('telepathy-glib', version: '>= 0.11.6', required: enable_telepathy)
have_telepathy = dbus_glib_dep.found() and telepathy_glib_dep.found()
config_h.set('VINAGRE_HAVE_TELEPATHY', have_telepathy)

# *** Checks for pty stuff ***
# Pull in the right libraries for various functions which might not be
# bundled into an exploded libc.
socket_deps = []
have_socket = cc.has_function('socketpair')
if not have_socket
  socket_deps += cc.find_library('socket', required: false)
  have_socket = cc.has_function('socketpair', dependencies: socket_deps)
endif
config_h.set('HAVE_SOCKETPAIR', have_socket)

# Check for bind in libsocket (needed on Solaris)
# FIXME: check seems unnecessary
cc.has_function('bind', dependencies: socket_deps)

util_dep = cc.find_library('util', required: false)
have_login_tty = util_dep.found() and cc.has_function('login_tty', dependencies: util_dep)
config_h.set('HAVE_LOGIN_TTY', have_login_tty)

# Check for getifaddrs
have_ifaddrs = cc.has_header('ifaddrs.h')
config_h.set('HAVE_IFADDRS_H', have_ifaddrs)

# RFC2553 introduce sockaddr_storage as ifa_addr member in ifaddrs structure
# Not all distros follow this.
if have_ifaddrs
  ifaddrs_src = '''
    #include <net/if.h>
    #include <ifaddrs.h>
    int
    main (int argc, char *argv[])
    {
      struct ifaddrs *myaddrs;
      getifaddrs (&myaddrs);
      if (myaddrs->ifa_addr->ss_family == AF_INET) {
      }
    }
  '''

  config_h.set('RFC2533', cc.links(ifaddrs_src, name: 'RFC2553 is followed'))
endif

subdir('po')
subdir('help')
subdir('data')
subdir('plugins')
subdir('vinagre')

configure_file(
  output: 'config.h',
  configuration: config_h,
)

meson.add_install_script(
  'meson_post_install.py',
  vinagre_datadir,
  gio_schemasdir,
)
