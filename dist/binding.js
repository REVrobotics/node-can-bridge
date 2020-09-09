"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.getNumDevices = void 0;
var util_1 = require("util");
var addon = require('bindings')('addon');
exports.getNumDevices = util_1.promisify(addon.getNumDevices);
