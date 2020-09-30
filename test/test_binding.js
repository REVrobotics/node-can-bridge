
const addon = require("../dist/binding.js");
const assert = require("assert").strict;

let devices = [];

async function testGetDevices() {
    assert(addon.getDevices, "getDevices is undefined");
    try {
        devices = addon.getDevices();
        console.log(`Found ${devices.length} device(s)`);
        devices.forEach((device) => {
            console.log(device);
            assert(device.hasOwnProperty("descriptor"), "Device does not have a descriptor");
            assert(device.hasOwnProperty("name"), "Device does not have a name");
            assert(device.hasOwnProperty("driverName"), "Device does not have a driver name");
        });
    } catch(error) {
        assert.fail(error);
    }
}

async function testRegisterDeviceToHAL() {
    assert(addon.registerDeviceToHAL, "registerDeviceToHAL is undefined");
    try {
        if (devices.length > 0) {
            console.log(`Registering device ${devices[0].descriptor} to HAL`);
            const status = await addon.registerDeviceToHAL(devices[0].descriptor, 0, 0);
            console.log(`Device registered with status code ${status}`);
            assert.equal(status, 0, "Registering device failed");
        }

    } catch(error) {
        assert.fail(error);
    }
}

async function testUnregisterDeviceFromHAL() {
    assert(addon.unregisterDeviceFromHAL, "unregisterDeviceFromHAL is undefined");

    try {
        if (devices.length > 0) {
            console.log(`Unregistering device ${devices[0].descriptor} from HAL`);
            const status = await addon.unregisterDeviceFromHAL(devices[0].descriptor);
            console.log(`Device unregistered with status code ${status}`);
            assert.equal(status, 0, "unregisterDeviceFromHAL device failed");
        }
    } catch(error) {
        assert.fail(error);
    }
}

async function testReceiveMessage() {
    assert(addon.receiveMessage, "receiveMessage is undefined");
    try {
        if (devices.length ===  0) return;
        await new Promise(resolve=>{setTimeout(resolve, 200)});
        const message = addon.receiveMessage(devices[0].descriptor, 0, 0);
        console.log("Got message", message);
    } catch(error) {
        assert.fail(error);
    }
}

async function testOpenStreamSession() {
    assert(addon.openStreamSession, "openStreamSession is undefined");
    try {
        if (devices.length ===  0) return;
        const sessionHandle = addon.openStreamSession(devices[0].descriptor, 0, 0, 4);
        console.log("Started stream session with handle", sessionHandle);
        return sessionHandle;
    } catch(error) {
        assert.fail(error);
    }
}

async function testReadStreamSession(sessionHandle) {
    assert(addon.readStreamSession, "readStreamSession is undefined");
    if (devices.length === 0) return;
    try {
        await new Promise(resolve => {setTimeout(resolve, 200)});
        const data = addon.readStreamSession(devices[0].descriptor, sessionHandle, 4);
        console.log("Got stream:", data);
        return sessionHandle;
    } catch (error) {
        console.log(error);
    }
}

async function testCloseStreamSession(sessionHandle) {
    assert(addon.closeStreamSession, "closeStreamSession is undefined");
    try {
        if (devices.length ===  0) return;
        const status = addon.closeStreamSession(devices[0].descriptor, sessionHandle);
        assert.equal(status, 0, "Closing stream failed");
    } catch(error) {
        assert.fail(error);
    }
}

async function testGetCANDetailStatus() {
    assert(addon.getCANDetailStatus, "getCANDetailStatus is undefined");
    try {
        if (devices.length ===  0) return;
        const status = addon.getCANDetailStatus(devices[0].descriptor);
        console.log("CAN Status:", status);
    } catch(error) {
        assert.fail(error);
    }
}

async function testSendCANMessage() {
    assert(addon.sendCANMessage, "sendCANMessage is undefined");
    try {
        if (devices.length ===  0) return;
        // Send identify to SparkMax #1
        const status = addon.sendCANMessage(devices[0].descriptor, 0x2051D81, [], 0);
        console.log("CAN Status:", status);
    } catch(error) {
        assert.fail(error);
    }
}

process.on('uncaughtException', function (exception) {
    console.log(exception);
});

testGetDevices()
    .then(testReceiveMessage)
    .then(testOpenStreamSession)
    .then(testReadStreamSession)
    .then(testCloseStreamSession)
    .then(testGetCANDetailStatus)
    .then(testSendCANMessage)
    .catch((error)  => {
        console.log(error);
    });
