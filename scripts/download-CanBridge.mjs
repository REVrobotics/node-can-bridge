import * as fs from "fs";
import * as path from "path";
import axios from 'axios';
import AdmZip from 'adm-zip';
import { platform, arch } from 'os';

const canBridgeTag = "v2.3.2";
const canBridgeReleaseAssetUrlPrefix = `https://github.com/unofficial-rev-port/CANBridge/releases/download/${canBridgeTag}`;

const externalCompileTimeDepsPath = 'externalCompileTimeDeps';
const runtimeArtifactsPath = {
    win: 'prebuilds/win32-x64',
    osx: 'prebuilds/darwin-osxuniversal',
    linux: 'prebuilds/linux-x64',
    linuxArm: 'prebuilds/linux-arm64',
    linuxArm32: 'prebuilds/linux-arm32'
};
const tempDir = 'temp';

try {
    // TODO: Do not hardcode the filenames, instead get them from the GitHub API -> Look at Octokit: https://github.com/octokit/octokit.js
    await Promise.all([
        'CANBridge-linuxarm32.zip',
        'CANBridge-linuxarm64.zip',
        'CANBridge-linuxx86-64-Linux64.zip',
        'CANBridge-osxuniversal-macOS.zip',
        'CANBridge-windowsx86-64-Win64.zip',
        'headers.zip'
    ].map(filename => downloadCanBridgeArtifact(filename)));
    console.log("CANBridge download completed");

    
    console.log("Extracting headers");
    const zipFiles = fs.readdirSync(tempDir).filter(filename => filename.endsWith('.zip') && filename !== 'headers.zip');
    for (const filename of zipFiles) { 
        await unzipCanBridgeArtifact(filename, tempDir);
    }
    const headersZip = new AdmZip(path.join(tempDir, "headers.zip"));
    headersZip.extractAllTo(path.join(externalCompileTimeDepsPath, 'include'));
    console.log("Headers extracted");

    moveRuntimeDeps();

    moveCompileTimeDeps();
} catch (e) {
    if (axios.isAxiosError(e) && e.request) {
        console.error(`Failed to download CANBridge file ${e.request.protocol}//${e.request.host}${e.request.path}`);
    } else {
        console.error(`Other error occurred: ${e.message}`);
        // For non-axios errors, the stacktrace will likely be helpful
        throw e;
    }
    process.exit(1);
} finally {
    //if (fs.existsSync(tempDir)) {
    //    fs.rmSync(tempDir, { recursive: true, force: true});
    //}
}

/**
 * Move external compile time dependencies to the correct directory
 * 
 * This function is used to move the external compile time dependencies to the correct directory based on the platform and architecture from downloaded artifacts
 */
function moveCompileTimeDeps() {
    console.log("Moving external compile time dependencies to correct directories");
    if (!fs.existsSync(externalCompileTimeDepsPath)) {
        fs.mkdirSync(externalCompileTimeDepsPath, { recursive: true });
    }
    if (platform() === 'win32') {
        const deps = ['CANBridge.lib', 'wpiHal.lib', 'wpiutil.lib'];
        deps.forEach(dep => moveExternalCompileTimeDeps(path.join('win32-x64', dep)));
    } else if (platform() === 'darwin') {
        const deps = ['libCANBridge.a'];
        deps.forEach(dep => moveExternalCompileTimeDeps(path.join('darwin-osxuniversal', dep)));
    } else if (platform() === 'linux') {
        const deps = ['libCANBridge.a'];
        const archDepMap = {
            x64: 'linux-x64',
            arm64: 'linux-arm64',
            arm: 'linux-arm32'
        };
        deps.forEach(dep => moveExternalCompileTimeDeps(path.join(archDepMap[arch()], dep)));
    }
    console.log("External compile time dependencies moved to correct directories");
}

/**
 * Move runtime dependencies to the correct directory
 * 
 * This function is used to move the runtime dependencies to the correct directory based on the platform and architecture from downloaded artifacts
 */
function moveRuntimeDeps() {
    console.log("Moving artifacts to correct directories");
    if (!fs.existsSync('prebuilds')) {
        fs.mkdirSync('prebuilds', { recursive: true });
    }
    if (platform() === 'win32') {
        const deps = ['CANBridge.dll', 'wpiHal.dll', 'wpiutil.dll'];
        deps.forEach(dep => moveRuntimeArtifactsDeps(path.join('win32-x64', dep), runtimeArtifactsPath.win));
    } else if (platform() === 'darwin') {
        const deps = ['libCANBridge.dylib', 'libwpiHal.dylib', 'libwpiutil.dylib'];
        deps.forEach(dep => moveRuntimeArtifactsDeps(path.join('darwin-osxuniversal', dep), runtimeArtifactsPath.osx));
    } else if (platform() === 'linux') {
        const deps = ['libCANBridge.so', 'libwpiHal.so', 'libwpiutil.so'];
        if (arch() === 'x64') {
            deps.forEach(dep => moveRuntimeArtifactsDeps(path.join('linux-x64', dep), runtimeArtifactsPath.linux));
        }
        if (arch() === 'arm64') {
            deps.forEach(dep => moveRuntimeArtifactsDeps(path.join('linux-arm64', dep), runtimeArtifactsPath.linuxArm));
        }
        if (arch() === 'arm') {
            deps.forEach(dep => moveRuntimeArtifactsDeps(path.join('linux-arm32', dep), runtimeArtifactsPath.linuxArm32));
        }
    }
    console.log("CANBridge artifacts moved to correct directories");
}

/**
 * Download artifacts from the CANBridge GitHub release page
 * 
 * @param {*} filename filename of the artifact to download
 * @param {*} destDir destination directory to save the artifact, defaults to tempDir
 */
async function downloadCanBridgeArtifact(filename, destDir = tempDir) {
    fs.mkdirSync(destDir, { recursive: true });
    const response = await axios.get(`${canBridgeReleaseAssetUrlPrefix}/${filename}`, { responseType: "stream" });
    const fileStream = fs.createWriteStream(`${destDir}/${filename}`);
    response.data.pipe(fileStream);
    await new Promise(resolve => {
        fileStream.on('finish', resolve);
    });
}

/**
 * Unzip the CANBridge artifacts
 * 
 * @param {string} filename - filename of the artifact to unzip
 * @param {string} destDir - destination directory to unzip the artifact
 */
async function unzipCanBridgeArtifact(filename, destDir) {
    const zip = new AdmZip(`${destDir}/${filename}`);
    let filepath;
    if (filename.includes('linuxarm32')) filepath = "linux-arm32";
    else if (filename.includes('linuxarm64')) filepath = "linux-arm64";
    else if (filename.includes('linuxx86-64')) filepath = "linux-x64";
    else if (filename.includes('osxuniversal')) filepath = "darwin-osxuniversal";
    else if (filename.includes('windowsx86-64')) filepath = "win32-x64";
    zip.extractAllTo(`${destDir}/${filepath}`);
}

/**
 * Move runtime artifacts to the correct directory
 * 
 * @param {*} filename filename of the artifact to move
 * @param {*} destDir destination directory to save the artifact
 */
function moveRuntimeArtifactsDeps(filename, destDir) {
    fs.mkdirSync(destDir, { recursive: true });
    fs.renameSync(path.join(tempDir, filename), path.join(destDir, path.basename(filename)));
}

/**
 * Move External Compile Time Dependencies to the correct directory
 * 
 * @param {*} filename filename of the artifact to move
 */
function moveExternalCompileTimeDeps(filename) {
    fs.renameSync(path.join(tempDir, filename), path.join(externalCompileTimeDepsPath, path.basename(filename)));
}