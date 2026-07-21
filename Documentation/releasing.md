# Releasing Tactility

1. Set the CDN to development mode. Alternatively: manually clear cache after uploading things.
2. Set the new version in the Tactility repository:
    1. Update `version.txt` in the Tactility project and create a branch to start a build for it
    2. Merge the branch and wait for a build.
3. Push a tag e.g. `v1.2.3` - the `v` prefix is crucial for the pipelines.
4. Wait for the SDK and firmwares to be published on the CDN.
5. Test the firmwares on all devices with the web installer
6. Run `Buildscripts/gh-download-artifacts.py [actionId] --token [token]` and let it run in the background.
   Get a token from [here](https://github.com/settings/personal-access-tokens)
7. Update the [TactilityApps](https://github.com/TactilityProject/TactilityApps) with the new SDK version and bump the app versions.
8. After merging the apps, manually upload to CDN, then test them.
9. Make a new version of the docs available at [TactilityDocs](https://github.com/TactilityProject/TactilityDocs)
   1. Copy `docs/versions/main/` into new version folder
   2. Edit `index.html` to include the new version
   3. Push code directly to `main` branch
10. Make a new [GitHub release](https://github.com/TactilityProject/Tactility/releases/new)
    Run `Buildscripts/release-summary.py` to generate a release, then review it
11. Double-check that all CDN/TactilityApps/Tactility repository changes are merged.
12. Add the `-dev` postfix from `version.txt` and bump the version. Merge it to `main`.
13. Ensure that the CDN is not in development mode anymore.

### Post-release

1. Mention on Discord
2. Consider making a video with one of the devices, showcasing new features
3. Consider notifying vendors/stakeholders
 