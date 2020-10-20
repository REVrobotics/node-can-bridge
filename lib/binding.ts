import {promisify} from "util";
const addon = require('bindings')('addon');

export interface CanMessage {
    data: number[];
    messageID: number;
    timeStamp: number;
}

export interface CanDeviceInfo {
    descriptor: string;
    name: string;
    driverName: string;
    available: boolean;
}

export interface CanDeviceStatus {
    busOff: number;
    txFull: number;
    receiveErr: number;
    transmitErr: number;
    percentBusUtilization: number;
}

export const getDevices: () => CanDeviceInfo[] = addon.getDevices;
export const registerDeviceToHAL:
    (descriptor:string, messageId:Number, messageMask:number) => number = addon.registerDeviceToHAL;
export const unregisterDeviceFromHAL: (descriptor:string) => Promise<number> = promisify(addon.unregisterDeviceFromHAL);
export const receiveMessage: (descriptor:string, messageId:number, messageMask:number) => CanMessage = addon.receiveMessage;
export const openStreamSession: (descriptor:string, messageId:number, messageMask:number, maxSize:number) =>
    number = addon.openStreamSession;
export const readStreamSession: (descriptor:string, sessionHandle:number, messagesToRead:number) =>
    CanMessage[] = addon.readStreamSession;
export const closeStreamSession: (descriptor:string, sessionHandle:number) => number = addon.closeStreamSession;
export const getCANDetailStatus: (descriptor:string) => CanDeviceStatus = addon.getCANDetailStatus;
export const sendCANMessage: (descriptor:string, messageId: number, messageData: number[], repeatPeriod: number) => number = addon.sendCANMessage;
export const sendHALMessage: (messageId: number, messageData: number[], repeatPeriod: number) => number = addon.sendHALMessage;
export const intializeNotifier: () => void = addon.intializeNotifier;
export const waitForNotifierAlarm: (time:number) => Promise<number> = promisify(addon.waitForNotifierAlarm);
export const stopNotifier: () => void = addon.stopNotifier;
export const writeDfuToBin: (dfuFileName:string, binFileName:string) => Promise<number> = promisify(addon.writeDfuToBin);