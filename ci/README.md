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
| core-tests | the 529 host checks over the measurement core, with `-Werror` |
| electrical | the 510 rule checks - including that nothing can charge the AA cells and that the firmware's pin table matches the wiring - and that the generated files (`NETLIST.md`, `docs/BOM.md`) are in step with `design.py` |
| energy-model | re-runs the model and **fails if ECO stops clearing three months with margin** |
| enclosure | renders all four parts (front, back, door, stand), which runs the OpenSCAD asserts over every module, post and zone, and checks each STL is manifold and fits a 250 × 210 bed (more than 8 % overhang is a warning, not a failure) |
| firmware | placeholder - building esp-matter in CI needs a prepared image |

The energy-model job is the interesting one: it makes the headline battery
claim a build-breaking assertion rather than a sentence in a README that
quietly goes stale. The ERC's one standing warning (no soft start on the
SEN62's switch, bench test T-P3) is a warning, not an error, so it does not
fail the job.
