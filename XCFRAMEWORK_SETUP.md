# XCFramework Distribution Setup Guide

This guide explains how to set up XCFramework distribution for kuzu-swift.

## Distribution Strategy

This project uses a hybrid approach:
- **Development**: Environment variables for testing binary distribution
- **Production**: Dedicated `binary-distribution` branch for releases

## Repository Settings (Manual Setup Required)

### 1. Enable GitHub Actions
1. Go to **Settings → Actions → General**
2. Select **"Allow all actions and reusable workflows"**
3. Under **Workflow permissions**, select **"Read and write permissions"**
4. Check **"Allow GitHub Actions to create and approve pull requests"**

### 2. Create Personal Access Token for Documentation
1. Go to GitHub **Settings → Developer settings → Personal access tokens (classic)**
2. Generate new token with `repo` scope (for pushing to kuzudb/api-docs)
3. Add the token to repository secrets:
   - Go to **Settings → Secrets and variables → Actions**
   - Add new repository secret named `DOC_PUSH_TOKEN`
   - Paste your personal access token

### 3. Branch Protection (Optional but Recommended)
1. Go to **Settings → Branches**
2. Add rule for `main` branch:
   - Enable **"Require pull request reviews before merging"**
   - Enable **"Require status checks to pass before merging"**
   - Select required status checks: `build-and-test`
   - Enable **"Allow GitHub Actions to bypass pull request requirements"**

## Testing the Setup

### 1. Test XCFramework Build
```bash
# Test locally
./Scripts/build-xcframework.sh

# Test via GitHub Actions (manual trigger)
# Go to Actions → Release XCFramework → Run workflow
```

### 2. Test Documentation Generation
```bash
# Go to Actions → Generate Documentation → Run workflow
```

### 3. Create a Release
```bash
# Create and push a tag
git tag v0.1.0
git push origin v0.1.0

# Create release on GitHub
# 1. Go to Releases → Create a new release
# 2. Choose the tag you just created
# 3. Publish release
# 4. The workflow will automatically build and attach XCFramework
```

## Using the Binary Distribution

After a successful release, users can add kuzu-swift as a binary dependency:

```swift
// Package.swift
dependencies: [
    .package(url: "https://github.com/kuzudb/kuzu-swift", exact: "0.1.0"),
],
targets: [
    .target(
        name: "YourApp",
        dependencies: [
            .product(name: "Kuzu", package: "kuzu-swift"),
        ]
    )
]
```

## Workflow Files Overview

- **`swift.yml`**: Builds and tests on push/PR to main
- **`release-xcframework.yml`**: Builds XCFramework on release creation
- **`generate-docs.yml`**: Generates and pushes documentation (manual trigger)
- **`update-kuzu.yml`**: Daily check for upstream Kuzu updates

## Troubleshooting

### XCFramework build fails
- Ensure Xcode Command Line Tools are installed
- Check that all build scripts have execution permissions
- Verify Package.swift is correctly configured

### Documentation push fails
- Verify `DOC_PUSH_TOKEN` has correct permissions
- Ensure kuzudb/api-docs repository exists and is accessible

### Release assets not uploading
- Check GitHub Actions permissions are set to "Read and write"
- Verify the release event triggered the workflow
- Check workflow logs for specific errors

## Support

For issues or questions, please open an issue on the kuzu-swift repository.