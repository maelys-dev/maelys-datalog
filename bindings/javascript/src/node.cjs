/* SPDX-License-Identifier: MPL-2.0 */
'use strict';
const { binding } = require('./core.cjs');
module.exports = binding(async ({ profile = 'small' } = {}) => {
  if (profile !== 'small' && profile !== 'large') throw new TypeError('Unknown engine profile');
  const addon = require(`../prebuilds/${process.platform}-${process.arch}/${profile}.node`);
  return addon.create();
});
