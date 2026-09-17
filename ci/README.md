# Continuous integration

`github-actions-ci.yml` belongs at `.github/workflows/ci.yml`. It is parked
here because the token this repository was first pushed with did not carry the
`workflow` scope, and GitHub refuses a push that creates a workflow file
without it.

To enable it:

```bash
mkdir -p .github/workflows
git mv ci/github-actions-ci.yml .github/workflows/ci.yml
git commit -m "ci: enable GitHub Actions"
git push
```

You need a token with the `workflow` scope:

```bash
gh auth refresh -h github.com -s workflow
```

## What it runs

| job | what it checks |
|---|---|
| core-tests | the 455 host checks over the measurement core, with `-Werror` |
| electrical | the 371 rule checks, and that the generated files are in step with `design.py` |
| energy-model | re-runs the model and **fails if ECO stops clearing six months with margin** |
| enclosure | renders every part (which runs the OpenSCAD asserts) and checks each STL is manifold, fits the bed and stays under the overhang limit |
| firmware | placeholder - building esp-matter in CI needs a prepared image |

The energy-model job is the interesting one: it makes the headline battery
claim a build-breaking assertion rather than a sentence in a README that
quietly goes stale.
