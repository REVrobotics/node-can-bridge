# node-can-bridge NPM package

This package enables Node.js applications to use a CAN bus over USB.

## Compiling

### Prerequisites

- Supported version of NodeJS (preferably a LTS version)
- A C/C++ compiler that supports C++20
- Internet connection to pull from upstream [CANBridge](https://github.com/unofficial-rev-port/CANBridge)

### Steps

1. Clone repository
2. Run `npm i` to install dependencies and compile the Node APIs
3. To test, run `npm run pretest` to prepare the testing and `npm test` to actually perform the testing of the repository

## Making a release (for repository owners)

1. Check out the `main` branch
2. Update `version` field in `package.json`
3. Run `npm install`
4. Commit change to git
5. Run `git tag v<version>`
6. Run `git push`
7. Run `git push --tags`
8. Run `npm publish --access public`
9. Create a new release on GitHub with an explanation of the changes
