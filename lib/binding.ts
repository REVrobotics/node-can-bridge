import {promisify} from "util";
const addon = require('bindings')('addon');

export const getDevices: () => Promise<any> = promisify(addon.getDevices);
export const registerDeviceToHAL:
    (descriptor:string, messageId:Number, messageMask:number) => Promise<Number> = promisify(addon.registerDeviceToHAL);
export const unregisterDeviceFromHAL: (descriptor:string) => Promise<Number> = promisify(addon.unregisterDeviceFromHAL);
export const receiveMessage: () => Promise<Array<Number>> = promisify(addon.receiveMessage);
export const openStreamSession: (descriptor:string, messageId:Number, messageMask:number, maxSize:number) =>
    Promise<Number> = promisify(addon.openStreamSession);
export const readStreamSession: (descriptor:string, messagesToRead:number) =>
    Promise<any> = promisify(addon.readStreamSession);
export const closeStreamSession: (descriptor:string) => Promise<Number> = promisify(addon.closeStreamSession);