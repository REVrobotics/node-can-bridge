# node-can-bridge NPM package

This package enables Node.js applications to use a CAN bus over USB.

## Making a release

1. Check out the `main` branch
2. Update `version` field in `package.json`
3. Run `npm install`
4. Run `npm run prepublish`
5. Commit change to git
6. Run `git tag v<version>`
7. Run `git push`
8. Run `git push --tags` 
9. Run `npm publish --access public`
10. Create a new release on GitHub with an explanation of the changes
