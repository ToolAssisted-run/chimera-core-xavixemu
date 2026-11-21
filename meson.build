# chimera-core-xavixemu: XaviXEmu (XaviX games) in miniBox's sandbox, as a
# Chimera core.
#
# Native reference build (run-native + run-wbx under the build dir):
#     sh waterbox/apply-patches.sh
#     meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
# Guest (core.wbx) build via the cross file waterbox/setup-guest.sh writes:
#     sh waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest core.wbx
#
# THE SOURCE LIST IS UPSTREAM'S OWN: the emulation files of XaviXEmu's
# CMakeLists.txt (src/core, sha1.c, miniz), with the two small patches in
# patches/. Its Windows front end (main.c and the files around it) and the
# experimental XaviX 2 machine (src/xavix2) are not built. Both flavors
# compile the SAME waterbox/cinterface.c; only emulibc.h differs
# (waterbox/native-shim for the reference build).
project('chimera-core-xavixemu', 'c',
  default_options: ['c_std=c99', 'optimization=2', 'debug=false', 'warning_level=0'])

xx = 'extern/XaviXEmu'

# The patches over XaviXEmu, applied once: a tree that already has them is left
# alone (waterbox/apply-patches.sh).
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  error('pass -Dminibox_dir=<miniBox checkout>, the extern/chimera-common-minibox of a Chimera checkout')
endif

# miniz reads the ZIP from memory only: no files, no clock. _POSIX_C_SOURCE:
# the guest kit's windows header uses openat under -std=c99.
add_project_arguments('-D_POSIX_C_SOURCE=200809L', '-DMINIZ_NO_STDIO', '-DMINIZ_NO_TIME', '-DMINIZ_NO_ARCHIVE_WRITING_APIS',
  '-fno-strict-aliasing', '-ffp-contract=off', language: 'c')

inc = include_directories('waterbox', xx / 'src/core', xx / 'src', xx / 'third_party/miniz')

core_files = [
  xx / 'src/core/drgqst_core.c',
  xx / 'src/core/drgqst_state.c',
  xx / 'src/core/xavix_audio.c',
  xx / 'src/core/xavix_cpu.c',
  xx / 'src/core/xavix_machine.c',
  xx / 'src/core/xavix_peripherals.c',
  xx / 'src/core/xavix_video.c',
]
core_sources = files(core_files) + files(
  xx / 'src/sha1.c',
  xx / 'third_party/miniz/miniz.c',
  'waterbox/cinterface.c',
)

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  # miniBox's musl toolchain arrives through the cross file (setup-guest.sh);
  # the link recipe is the guest kit's - --no-relax for the large code model,
  # emulibc from the miniBox build, plain C (no unwinder, no libstdc++).
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', core_sources,
    name_suffix: 'wbx',
    include_directories: inc,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax',
      '-T', mbdir / 'source/guest/linkscript.T',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lgcc', '-lc'])
else
  # ---- the native reference -------------------------------------------------
  # the same cinterface over shimmed emulibc; the guest kit headers
  # (waterbox_settings/slots, jsmn) are plain stdio C and run as-is.
  # -ffp-contract=off (above): the machine's video does floating point, and
  # the two builds must round alike.
  m = meson.get_compiler('c').find_library('m', required: false)
  executable('run-native', core_sources + files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc],
    c_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    dependencies: [m])

  mbhost = mb / 'build/meson-linux/source/host'
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host', '-D_DEFAULT_SOURCE'],
    link_args: [mbhost / 'libminiboxhost.so', '-Wl,-rpath,' + mbhost])

  # ---- upstream's own tests, which need no ROM ------------------------------
  # What XaviXEmu's CMakeLists.txt builds for the emulation (its tests of the
  # CPU, the machine, the video, the audio, the serial memory, SHA-1 and the
  # state image), against the patched sources this core ships. waterbox/
  # run-gate.sh runs them (test-* in the build directory).
  tdir = xx / 'tests'
  upstream_tests = {
    'test-sha1': ['sha1_test.c', xx / 'src/sha1.c'],
    'test-machine': ['xavix_machine_test.c', xx / 'src/core/xavix_machine.c', xx / 'src/core/xavix_peripherals.c'],
    'test-cpu': ['xavix_cpu_test.c', xx / 'src/core/xavix_cpu.c'],
    'test-video': ['xavix_video_test.c', xx / 'src/core/xavix_video.c'],
    'test-peripherals': ['xavix_peripherals_test.c', xx / 'src/core/xavix_peripherals.c'],
    'test-audio': ['xavix_audio_test.c', xx / 'src/core/xavix_audio.c'],
    'test-state': ['drgqst_state_test.c', 'waterbox/test-stubs.c'] + core_files,
  }
  foreach name, parts : upstream_tests
    srcs = []
    foreach p : parts
      if p.contains('/')
        srcs += files(p)
      else
        srcs += files(tdir / p)
      endif
    endforeach
    executable(name, srcs,
      include_directories: [inc, include_directories(tdir)],
      dependencies: [m])
  endforeach
endif
