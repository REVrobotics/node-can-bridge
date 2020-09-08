"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.getNumDevices = void 0;
const util_1 = require("util");
const addon = require('bindings')('addon.node');
exports.getNumDevices = util_1.promisify(addon.getNumDevices);