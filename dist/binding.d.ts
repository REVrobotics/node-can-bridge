export interface CanMessage {
    data: Number[];
    messageID: Number;
    timeStamp: Number;
}
export declare const getDevices: () => Promise<any>;
export declare const registerDeviceToHAL: (descriptor: string, messageId: Number, messageMask: number) => Promise<Number>;
export declare const unregisterDeviceFromHAL: (descriptor: string) => Promise<Number>;
export declare const receiveMessage: () => Promise<CanMessage>;
export declare const openStreamSession: (descriptor: string, messageId: Number, messageMask: number, maxSize: number) => Promise<Number>;
export declare const readStreamSession: (descriptor: string, messagesToRead: number) => Promise<CanMessage[]>;
export declare const closeStreamSession: (descriptor: string) => Promise<Number>;
export declare const getCANDetailStatus: (descriptor: string) => Promise<Object>;
