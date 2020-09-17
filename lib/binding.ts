import {promisify} from "util";
const addon = require('bindings')('addon');

export interface CanMessage {
    data: Number[];
    messageID: Number;
    timeStamp: Number;
}

export interface CanDeviceInfo {
    descriptor: string;
    name: string;
    driverName: string;
}

export interface CanDeviceStatus {
    busOff:Number;
    txFull:Number;
    receiveErr:Number;
    transmitErr:Number;
    percentBusUtilization:Number;
}

export const getDevices: () => CanDeviceInfo[] = addon.getDevices;
export const registerDeviceToHAL:
    (descriptor:string, messageId:Number, messageMask:number) => Promise<Number> = promisify(addon.registerDeviceToHAL);
export const unregisterDeviceFromHAL: (descriptor:string) => Promise<Number> = promisify(addon.unregisterDeviceFromHAL);
export const receiveMessage: (descriptor:string, messageId:Number, messageMask:number) => CanMessage = addon.receiveMessage;
export const openStreamSession: (descriptor:string, messageId:Number, messageMask:number, maxSize:number) =>
    Number = addon.openStreamSession;
export const readStreamSession: (descriptor:string, sessionHandle:Number, messagesToRead:number) =>
    CanMessage[] = addon.readStreamSession;
export const closeStreamSession: (descriptor:string, sessionHandle:Number) => Number = addon.closeStreamSession;
export const getCANDetailStatus: (descriptor:string) => CanDeviceStatus = addon.getCANDetailStatus;