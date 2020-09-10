"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.getDevices = void 0;
var util_1 = require("util");
var addon = require('bindings')('addon');
exports.getDevices = util_1.promisify(addon.getDevices);
