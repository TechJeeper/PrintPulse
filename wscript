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
    # config_page.js is committed so CloudPebble (which may skip this wscript)
    # and GitHub clones can webpack without a prior local build. When
    # config/index.html is present, refresh the embed so they stay in sync.
    src = ctx.path.find_node('config/index.html')
    dst = ctx.path.find_node('src/pkjs/config_page.js')
    if src is None:
        if dst is None:
            ctx.fatal('config/index.html and src/pkjs/config_page.js are both missing')
        return
    # Drop indentation and blank lines to keep the data: URL small. (waf reads
    # Latin-1 unless told otherwise.)
    text = src.read(encoding='utf-8').lstrip('﻿')
    lines = [line.strip() for line in text.splitlines()]
    html = '\n'.join(line for line in lines if line)
    js = ('// Generated from config/index.html by wscript. Do not edit.\n'
          'module.exports = ' + json.dumps(html) + ';\n')
    out = ctx.path.make_node('src/pkjs/config_page.js')
    if not os.path.exists(out.abspath()) or out.read(encoding='utf-8') != js:
        out.write(js, encoding='utf-8')


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
