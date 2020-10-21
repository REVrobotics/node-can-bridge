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
export declare const getDevices: () => Promise<CanDeviceInfo[]>;
export declare const registerDeviceToHAL: (descriptor: string, messageId: Number, messageMask: number) => number;
export declare const unregisterDeviceFromHAL: (descriptor: string) => Promise<number>;
export declare const receiveMessage: (descriptor: string, messageId: number, messageMask: number) => CanMessage;
export declare const openStreamSession: (descriptor: string, messageId: number, messageMask: number, maxSize: number) => number;
export declare const readStreamSession: (descriptor: string, sessionHandle: number, messagesToRead: number) => CanMessage[];
export declare const closeStreamSession: (descriptor: string, sessionHandle: number) => number;
export declare const getCANDetailStatus: (descriptor: string) => CanDeviceStatus;
export declare const sendCANMessage: (descriptor: string, messageId: number, messageData: number[], repeatPeriod: number) => number;
export declare const sendHALMessage: (messageId: number, messageData: number[], repeatPeriod: number) => number;
export declare const intializeNotifier: () => void;
export declare const waitForNotifierAlarm: (time: number) => Promise<number>;
export declare const stopNotifier: () => void;
export declare const writeDfuToBin: (dfuFileName: string, binFileName: string) => Promise<number>;
export declare const openHALStreamSession: (messageId: number, messageMask: number, numMessages: number) => number;
export declare const readHALStreamSession: (streamHandle: number, numMessages: number) => CanMessage[];
export declare const closeHALStreamSession: (streamHandle: number) => void;
