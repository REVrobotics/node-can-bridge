
const addon = require("../dist/binding.js");
const assert = require("assert").strict;

let devices = [];

async function testGetDevices() {
    assert(addon.getDevices, "getDevices is undefined");
    try {
        devices = await addon.getDevices();
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
        const message = await addon.receiveMessage(devices[0].descriptor, 0, 0);
        console.log("Got message", message);
    } catch(error) {
        assert.fail(error);
    }
}

async function testOpenStreamSession() {
    assert(addon.openStreamSession, "openStreamSession is undefined");
    try {
        if (devices.length ===  0) return;
        const status = await addon.openStreamSession(devices[0].descriptor, 0, 0, 4);
        assert.equal(status, 0, "Opening stream failed");
    } catch(error) {
        assert.fail(error);
    }
}

async function testReadStreamSession() {
    assert(addon.readStreamSession, "readStreamSession is undefined");
    try {
        if (devices.length ===  0) return;
        await new Promise(resolve=>{setTimeout(resolve, 500)});
        const data = await addon.readStreamSession(devices[0].descriptor, 1);
        console.log("Got stream", data);
        await new Promise(resolve=>{setTimeout(resolve, 500)});
    } catch(error) {
        assert.fail(error);
    }
}

async function testCloseStreamSession() {
    assert(addon.closeStreamSession, "closeStreamSession is undefined");
    try {
        if (devices.length ===  0) return;
        const status = await addon.closeStreamSession(devices[0].descriptor);
        assert.equal(status, 0, "Closing stream failed");
    } catch(error) {
        assert.fail(error);
    }
}

async function testGetCANDetailStatus() {
    assert(addon.getCANDetailStatus, "getCANDetailStatus is undefined");
    try {
        if (devices.length ===  0) return;
        const status = await addon.getCANDetailStatus(devices[0].descriptor);
        console.log("CAN Status:", status);
    } catch(error) {
        assert.fail(error);
    }
}

testGetDevices()
    .then(testReceiveMessage)
    .then(testOpenStreamSession)
    .then(testReadStreamSession)
    .then(testCloseStreamSession)
    .then(testGetCANDetailStatus)
    .catch((error)  => {
        console.log(error);
    });

