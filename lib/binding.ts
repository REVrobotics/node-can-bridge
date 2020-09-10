import {promisify} from "util";
const addon = require('bindings')('addon');

export const getDevices: () => Promise<any> = promisify(addon.getDevices);
