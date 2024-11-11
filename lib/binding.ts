import {promisify} from "util";
import * as path from "path";

export interface DfuImageElement {
    startAddress: number;
    size: number;
}

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
    lastErrorTime: number;
}

export enum ThreadPriority {
    Low,
    BelowNormal,
    Normal,
    AboveNormal,
    High,
    PriorityMax,
    PriorityError
}

let bindingOptions = require("../binding-options.cjs");

export class CanBridgeInitializationError extends Error {
    cause: any;

    constructor(cause: any) {
        super("Failed to load the CANBridge native addon. This is likely a packaging problem, or perhaps the Visual Studio C++ redistributable package is not installed. See cause field for details.");
        this.cause = cause;
    }
}

export class CanBridge {
    getDevices: () => Promise<CanDeviceInfo[]>;
    registerDeviceToHAL: (descriptor:string, messageId:Number, messageMask:number) => number;
    unregisterDeviceFromHAL: (descriptor:string) => Promise<number>;
    receiveMessage: (descriptor:string, messageId:number, messageMask:number) => CanMessage;
    openStreamSession: (descriptor:string, messageId:number, messageMask:number, maxSize:number) => number;
    readStreamSession: (descriptor:string, sessionHandle:number, messagesToRead:number) => CanMessage[];
    closeStreamSession: (descriptor:string, sessionHandle:number) => number;
    getCANDetailStatus: (descriptor:string) => CanDeviceStatus;
    sendRtrMessage: (descriptor:string, messageId: number, messageData: number[], repeatPeriod: number) => number;
    sendCANMessage: (descriptor:string, messageId: number, messageData: number[], repeatPeriod: number) => number;
    sendHALMessage: (messageId: number, messageData: number[], repeatPeriod: number) => number;
    initializeNotifier: () => void;
    waitForNotifierAlarm: (time:number) => Promise<number>;
    stopNotifier: () => void;
    writeDfuToBin: (dfuFileName:string, binFileName:string, elementIndex?: number) => Promise<number>;
    getImageElements: (dfuFileName: string, imageIndex: number) => DfuImageElement[];
    openHALStreamSession: (messageId: number, messageMask:number, numMessages:number) => number;
    readHALStreamSession: (streamHandle:number, numMessages:number) => CanMessage[];
    closeHALStreamSession: (streamHandle:number) => void;
    setThreadPriority: (descriptor: string, priority: ThreadPriority) => void;
    setSparkMaxHeartbeatData: (descriptor: string, heartbeatData: number[]) => void;
    startRevCommonHeartbeat: (descriptor: string) => void;
    stopHeartbeats: (descriptor: string, sendDisabledHeartbeatsFirst: boolean) => void;
    ackHeartbeats: () => void;
    /**
     * @return Object that maps arbitration IDs to the last-received message with that ID
     */
    getLatestMessageOfEveryReceivedArbId: (descriptor: string, maxAgeMs: number) => Record<number, CanMessage>;

    constructor() {
        try {
            const addon = require("pkg-prebuilds")(path.join(__dirname, '..'), bindingOptions);

            this.getDevices = promisify(addon.getDevices);
            this.registerDeviceToHAL = addon.registerDeviceToHAL;
            this.unregisterDeviceFromHAL = promisify(addon.unregisterDeviceFromHAL);
            this.receiveMessage = addon.receiveMessage;
            this.openStreamSession = addon.openStreamSession;
            this.readStreamSession = addon.readStreamSession;
            this.closeStreamSession = addon.closeStreamSession;
            this.getCANDetailStatus = addon.getCANDetailStatus;
            this.sendRtrMessage = addon.sendRtrMessage;
            this.sendCANMessage = addon.sendCANMessage;
            this.sendHALMessage = addon.sendHALMessage;
            this.initializeNotifier = addon.initializeNotifier;
            this.waitForNotifierAlarm = promisify(addon.waitForNotifierAlarm);
            this.stopNotifier = addon.stopNotifier;
            this.writeDfuToBin = addon.writeDfuToBin;
            this.getImageElements = addon.getImageElements;
            this.openHALStreamSession = addon.openHALStreamSession;
            this.readHALStreamSession = addon.readHALStreamSession;
            this.closeHALStreamSession = addon.closeHALStreamSession;
            this.setThreadPriority = addon.setThreadPriority;
            this.setSparkMaxHeartbeatData = addon.setSparkMaxHeartbeatData;
            this.startRevCommonHeartbeat = addon.startRevCommonHeartbeat;
            this.ackHeartbeats = addon.ackHeartbeats;
            this.stopHeartbeats = addon.stopHeartbeats;
            this.getLatestMessageOfEveryReceivedArbId = addon.getLatestMessageOfEveryReceivedArbId;
        } catch (e: any) {
            throw new CanBridgeInitializationError(e);
        }
    }
}
