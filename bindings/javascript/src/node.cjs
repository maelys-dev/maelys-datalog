/* SPDX-License-Identifier: MPL-2.0 */
'use strict';
const { binding } = require('./core.cjs');
const supported = 'Linux x64/arm64 with glibc >= 2.34, or macOS arm64 >= 13.5';
let platformChecked = false;
module.exports = binding(async ({ profile = 'small' } = {}) => {
  if (profile !== 'small' && profile !== 'large') throw new TypeError('Unknown engine profile');
  const platform = `${process.platform}-${process.arch}`;
  if (!platformChecked) {
    let compatible = ['linux-x64', 'linux-arm64', 'darwin-arm64'].includes(platform);
    if (compatible && process.platform === 'linux') {
      const glibc = process.report?.getReport().header.glibcVersionRuntime;
      const [major, minor] = (glibc || '').split('.').map(Number);
      compatible = major > 2 || (major === 2 && minor >= 34);
    }
    if (!compatible) {
      throw Object.assign(new Error(`Native Maelys does not support ${platform} or its libc. Supported: ${supported}. Use @maelys-dev/datalog/wasm on other platforms.`), { code: 'MAELYS_UNSUPPORTED_PLATFORM' });
    }
    platformChecked = true;
  }
  let addon;
  try {
    addon = require(`../prebuilds/${platform}/${profile}.node`);
  } catch (cause) {
    throw Object.assign(new Error(`Cannot load Maelys native prebuild ${platform}/${profile}. Supported: ${supported}. Check that the complete package is installed, or use @maelys-dev/datalog/wasm.`, { cause }), { code: 'MAELYS_NATIVE_LOAD_FAILED' });
  }
  return addon.create();
});
