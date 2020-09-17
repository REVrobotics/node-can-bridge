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
    busOff: Number;
    txFull: Number;
    receiveErr: Number;
    transmitErr: Number;
    percentBusUtilization: Number;
}
export declare const getDevices: () => CanDeviceInfo[];
export declare const registerDeviceToHAL: (descriptor: string, messageId: Number, messageMask: number) => Promise<Number>;
export declare const unregisterDeviceFromHAL: (descriptor: string) => Promise<Number>;
export declare const receiveMessage: () => CanMessage;
export declare const openStreamSession: (descriptor: string, messageId: Number, messageMask: number, maxSize: number) => Number;
export declare const readStreamSession: (descriptor: string, messagesToRead: number) => CanMessage[];
export declare const closeStreamSession: (descriptor: string) => Number;
export declare const getCANDetailStatus: (descriptor: string) => CanDeviceStatus;
