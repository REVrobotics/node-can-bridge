import {promisify} from "util";
const addon = require('bindings')('addon');

export const getDevices: () => Promise<any> = promisify(addon.getDevices);
export const registerDeviceToHAL:
    (descriptor:string, messageId:Number, messageMask:number) => Promise<Number> = promisify(addon.registerDeviceToHAL);
