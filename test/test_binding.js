
const addon = require("../dist/binding.js");
const assert = require("assert").strict;

async function testGetDevices() {
    assert(addon.getDevices, "The expected function is undefined");
    const getDevices = await addon.getDevices;
    getDevices()
        .then((devices) => {
            console.log(`Found ${devices.length} device(s)`);
            devices.forEach((device) => {
                console.log(device);
                assert(device.hasOwnProperty("descriptor"), "Device does not have a desctiptor");
                assert(device.hasOwnProperty("name"), "Device does not have a name");
                assert(device.hasOwnProperty("driverName"), "Device does not have a driver name");
            });
        })
        .catch((error) => {
            assert(false, error);
        });
}

testGetDevices();
