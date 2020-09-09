
const addon = require("../dist/binding.js");
const assert = require("assert").strict;

assert(addon.getNumDevices, "The expected function is undefined");

console.log(addon);
async function testBasic() {
    await new Promise(async (resolve) =>
    {
        const getNumDevices = await addon.getNumDevices;
        console.log(getNumDevices);
        getNumDevices().then((result) => {
            console.log("result", result);
            resolve();
        }).catch((error) => console.log("error", error));
    });
}

testBasic().then(() => console.log("Done"));