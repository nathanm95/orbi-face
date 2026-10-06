#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    ctx.load('pebble_sdk')

    # Verification affordances, never ship a bundle built with these set.
    # The emulator follows the host clock and ignores attempts to set it,
    # SELECT/BACK can't be sent reliably and touch can't be injected, so store
    # shots are pinned at build time instead:
    #   ORBI_SHOW_TIME=1008        always show 10:08
    #   ORBI_FREEZE=<act>          play act <act> (ActId in anim.h) and hold it
    #   ORBI_FREEZE_MS=<ms>        ...at this moment (default 0)
    #   ORBI_LOOP=<act>            replay act <act> forever
    #   ORBI_DEMO=1                the store GIF's run of acts, forever
    #   ORBI_THEME=<n>             force a colour theme
    #   ORBI_WEATHER=<kind>        fake the weather (WeatherKind in anim.h),
    #   ORBI_TEMP=<c> ORBI_WIND=<kmh>   with this temperature and wind
    #   ORBI_BATT=<pct>            fake the battery level
    #   ORBI_DEBUG=1               draw render time and frame-loop state
    defaults = {'ORBI_FREEZE_MS': '0', 'ORBI_TEMP': '14', 'ORBI_WIND': '8'}
    flags = ('ORBI_SHOW_TIME', 'ORBI_FREEZE', 'ORBI_LOOP', 'ORBI_DEMO', 'ORBI_THEME',
             'ORBI_WEATHER', 'ORBI_BATT', 'ORBI_DEBUG')
    for flag in flags:
        value = os.environ.get(flag)
        if not value:
            continue
        for env in ctx.all_envs.values():
            env.append_value('DEFINES', '%s=%d' % (flag, int(value)))
            if flag == 'ORBI_FREEZE':
                env.append_value('DEFINES', 'ORBI_FREEZE_MS=%d' % int(
                    os.environ.get('ORBI_FREEZE_MS', defaults['ORBI_FREEZE_MS'])))
            if flag == 'ORBI_WEATHER':
                for extra in ('ORBI_TEMP', 'ORBI_WIND'):
                    env.append_value('DEFINES', '%s=%d' % (extra, int(
                        os.environ.get(extra, defaults[extra]))))


def build(ctx):
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')
