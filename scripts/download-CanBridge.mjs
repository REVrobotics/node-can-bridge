import * as fs from "fs";
import * as path from "path";
import axios from 'axios';
import AdmZip from 'adm-zip';

const canBridgeTag = "v2.3.0";
const canBridgeReleaseAssetUrlPrefix = `https://github.com/unofficial-rev-port/CANBridge/releases/download/${canBridgeTag}`;

const externalCompileTimeDepsPath = 'externalCompileTimeDeps';
const runtimeArtifactsPath = path.join('prebuilds', 'win32-x64');
const tempDir = 'temp';

try {
    await Promise.all(Array.of(
        downloadCanBridgeArtifact('CANBridge.lib', externalCompileTimeDepsPath),
        downloadCanBridgeArtifact('CANBridge.dll', runtimeArtifactsPath),
        downloadCanBridgeArtifact('wpiHal.lib', externalCompileTimeDepsPath),
        downloadCanBridgeArtifact('wpiHal.dll', runtimeArtifactsPath),
        downloadCanBridgeArtifact('wpiutil.lib', externalCompileTimeDepsPath),
        downloadCanBridgeArtifact('wpiutil.dll', runtimeArtifactsPath),
        downloadCanBridgeArtifact('headers.zip', tempDir),
    ));

    console.log("CANBridge download completed");
    console.log("Extracting headers");

    const headersZip = new AdmZip(path.join(tempDir, "headers.zip"));

    await headersZip.extractAllTo(path.join(externalCompileTimeDepsPath, 'include'));
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

async function downloadCanBridgeArtifact(filename, destDir) {
    fs.mkdirSync(destDir, { recursive: true });
    const response = await axios.get(`${canBridgeReleaseAssetUrlPrefix}/${filename}`, { responseType: "stream" });
    const fileStream = fs.createWriteStream(`${destDir}/${filename}`);
    response.data.pipe(fileStream);
    await new Promise(resolve => {
        fileStream.on('finish', resolve);
    });
}
