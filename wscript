#
# PrintPulse build script (waf). Same as the SDK template, plus one step that
# embeds config/index.html into the phone-side JS bundle so the settings page
# works without any web hosting.
#
import json
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    ctx.load('pebble_sdk')


def embed_config_page(ctx):
    src = ctx.path.find_node('config/index.html')
    if src is None:
        # e.g. the CloudPebble zip, which ships the page already embedded
        if ctx.path.find_node('src/pkjs/config_page.js') is None:
            ctx.fatal('config/index.html is missing')
        return
    # Drop indentation and blank lines to keep the data: URL small. (waf reads
    # Latin-1 unless told otherwise.)
    text = src.read(encoding='utf-8').lstrip('﻿')
    lines = [line.strip() for line in text.splitlines()]
    html = '\n'.join(line for line in lines if line)
    js = ('// Generated from config/index.html by wscript. Do not edit.\n'
          'module.exports = ' + json.dumps(html) + ';\n')
    dst = ctx.path.make_node('src/pkjs/config_page.js')
    if not os.path.exists(dst.abspath()) or dst.read(encoding='utf-8') != js:
        dst.write(js, encoding='utf-8')


def build(ctx):
    ctx.load('pebble_sdk')
    embed_config_page(ctx)

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
