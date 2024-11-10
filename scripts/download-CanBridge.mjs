import * as fs from "fs";
import * as path from "path";
import axios from 'axios';
import AdmZip from 'adm-zip';

const canBridgeTag = "v2.5.1";
const canBridgeReleaseAssetUrlPrefix = `https://github.com/REVrobotics/CANBridge/releases/download/${canBridgeTag}`;

const externalCompileTimeDepsPath = 'externalCompileTimeDeps';
const tempDir = 'temp';
const headersZipFilename = 'headers.zip';
const wpiHalWindowsZipFilename = 'wpihal-windows-x64.zip';
const wpiUtilWindowsZipFilename = 'wpiutil-windows-x64.zip';

try {
    const wpiHalVersion = (await axios.get(`${canBridgeReleaseAssetUrlPrefix}/wpiHalVersion.txt`, { responseType: "text"})).data;
    console.log(`WPILib HAL version: ${wpiHalVersion}`)
    await Promise.all(Array.of(
        downloadCanBridgeArtifact('CANBridge-static.lib', externalCompileTimeDepsPath, "CANBridge.lib"),
        downloadCanBridgeArtifact(headersZipFilename, tempDir),
        downloadFile(`https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first/hal/hal-cpp/${wpiHalVersion}/hal-cpp-${wpiHalVersion}-windowsx86-64static.zip`, tempDir, wpiHalWindowsZipFilename),
        downloadFile(`https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first/wpiutil/wpiutil-cpp/${wpiHalVersion}/wpiutil-cpp-${wpiHalVersion}-windowsx86-64static.zip`, tempDir, wpiUtilWindowsZipFilename),
    ));

    console.log("Extracting headers");
    new AdmZip(path.join(tempDir, headersZipFilename))
        .extractAllTo(path.join(externalCompileTimeDepsPath, 'include'), true);
    console.log("Extracting WPILib HAL");
    new AdmZip(path.join(tempDir, wpiHalWindowsZipFilename))
        .extractEntryTo("windows/x86-64/static/wpiHal.lib", externalCompileTimeDepsPath, false, true);;
    console.log("Extracting WPIUtil");
    new AdmZip(path.join(tempDir, wpiUtilWindowsZipFilename))
        .extractEntryTo("windows/x86-64/static/wpiutil.lib", externalCompileTimeDepsPath, false, true);

    console.log(`Successfully downloaded CANBridge ${canBridgeTag}`);
} catch (e) {
    if (axios.isAxiosError(e) && e.request) {
        console.error(`Failed to download CANBridge file ${e.request.protocol}//${e.request.host}/${e.request.path}`);
    } else {
        console.error(`Failed to download CANBridge`);
        // For non-axios errors, the stacktrace will likely be helpful
        throw e;
    }
    process.exit(1);
} finally {
    if (fs.existsSync(tempDir)) {
        fs.rmSync(tempDir, { recursive: true });
    }
}

/**
 *
 * @param {string} filename
 * @param {string} destDir
 * @param {string} [destFilename]
 * @returns {Promise<undefined>}
 */
async function downloadCanBridgeArtifact(filename, destDir, destFilename) {
    fs.mkdirSync(destDir, { recursive: true });
    const response = await axios.get(`${canBridgeReleaseAssetUrlPrefix}/${filename}`, { responseType: "stream" });
    const fileStream = fs.createWriteStream(`${destDir}/${destFilename || filename}`);
    response.data.pipe(fileStream);
    await new Promise(resolve => {
        fileStream.on('finish', resolve);
    });
}

/**
 *
 * @param {string} url
 * @param {string} destDir
 * @param {string} destFilename
 * @returns {Promise<undefined>}
 */
async function downloadFile(url, destDir, destFilename) {
    fs.mkdirSync(destDir, { recursive: true });
    const response = await axios.get(url, { responseType: "stream" });
    const fileStream = fs.createWriteStream(`${destDir}/${destFilename}`);
    response.data.pipe(fileStream);
    await new Promise(resolve => {
        fileStream.on('finish', resolve);
    });
}
