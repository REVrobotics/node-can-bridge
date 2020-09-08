import {promisify} from "util";
const addon = require('bindings')('addon.node');

export const getNumDevices: () => Promise<Number> = promisify(addon.getNumDevices);