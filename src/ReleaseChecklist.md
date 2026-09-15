# Release checklist

Complete release work on a development branch until the final merge.

## Prepare and validate

- Update the version and display suffix (`Release`, `Beta`, or `Experimental`)
  in every required source file.
- Add the new entry to `docs/ReleaseNotes.md`.
- Run a complete RelWithDebInfo build and the native test suite.
- Build and sign the Debian package with `./buildPackage` and `./signPackage`.
- Push the development branch and wait for every required GitHub Actions check.

## Prepare the GitHub release

- Create a draft release with a title such as `PiPedal v2.0.110 Release`.
- Copy the release notes and the standard documentation and PGP links into the
  draft.
- Create the version tag without a channel suffix, for example `v2.0.110`.
- Upload the Debian package and its `.asc` signature.
- Enable a discussion when appropriate.
- Mark Release and Beta builds as the current release; do not do this for an
  Experimental build.
- Leave the release as a draft until the development branch is merged.

## Publish

- Confirm that the development-branch checks passed.
- Merge the development branch into `main`.
- Publish the draft release so its generated source archives use the merged
  revision.
- Wait for the `main` build and documentation workflows to complete.

## Verify

- Download and verify the published package and signature.
- Confirm that the README and online documentation link to the new version.
- Proofread the rendered release notes.

Experimental releases remain on the development branch and are not linked from
the README or documentation download page.
