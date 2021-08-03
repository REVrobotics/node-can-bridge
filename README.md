# node-can-bridge NPM package

This package enables Node.js applications to use a CAN bus over USB.

## Making a release

1. Check out the `main` branch
2. Update `version` field in `package.json`
3. Run `npm install`
4. Commit change to git
5. Run `git tag v<version>`
6. Run `git push --tags` 
7. Run `npm publish --access public`
8. Create a new release on GitHub with an explanation of the changes
