# Releasing Tactility

1. Set the CDN to development mode. Alternatively: manually clear cache after uploading things.
2. Set the new version in the Tactility repository:
    1. Update `version.txt` in the Tactility project and create a branch to start a build for it
    2. Merge the branch and wait for a build.
3. Upload the new Tactility SDK to the CDN
    1. Upload it to the [CDN](https://dash.cloudflare.com)
    2. Update `sdk.json` from [TactilityTool](https://github.com/TactilityProject/TactilityTool) and upload it to [CDN](https://dash.cloudflare.com)
4. Update the [TactilityApps](https://github.com/TactilityProject/TactilityApps) with the new SDK and also release these to the CDN:
    1. Download the `cdn-files.zip` from the pipelines
    2. Upload them to the CDN at `apps/x.y.z/`
    3. Also upload them to the CDN for the upcoming version, because the upcoming a.b.c version will also need some basic apps to download
5. Test the latest unstable version of Tactility on several devices using the [web installer](https://install.tactility.one). Pay special attention to:
    1. The version of the unstable channel (should be updated!)
    2. App Hub
    3. Wi-Fi
6. Test the firmwares on all devices with the local web installer
7. Push a tag e.g. `v1.2.3` - the `v` prefix is crucial for the pipelines
8. The pipelines should now kick off a build that releases the build to the stable channel of the Web Installer. Verify that.
9. Make a new version of the docs available at [TactilityDocs](https://github.com/TactilityProject/TactilityDocs)
10. Make a new [GitHub release](https://github.com/TactilityProject/Tactility/releases/new)
11. Double-check that all CDN/TactilityApps/Tactility repository changes are merged.
12. Ensure that the CDN is not in development mode anymore.

### Post-release

1. Mention on Discord
2. Consider making a video with one of the devices, showcasing new features
3. Consider notifying vendors/stakeholders
 