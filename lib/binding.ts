import {promisify} from "util";
const addon = require('bindings')('addon');

export const getNumDevices: () => Promise<Number> = promisify(addon.getNumDevices);