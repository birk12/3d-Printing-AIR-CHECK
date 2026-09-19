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
| core-tests | the 537 host checks over the measurement core and the power module (`ac_power`), and the Power-Standard's `pwr_std` test, with `-Werror` |
| electrical | the 748 rule checks - including the cell chain (a fuse per cell, the fused cells meeting only at the BMS, P− as the system ground, B− nowhere else, the NTC between TH and GND), that the FireBeetle's own Li-ion charger can never reach the LiFePO4 cells, that the SEN62's rail stays at or above 3.15 V down to the cells' 3.0 V, that no GPIO sees more than 3.6 V and the PWR-K ladder clears `pwr_std`'s thresholds, and that the firmware's pin table matches the wiring - and that the generated files (`NETLIST.md`, `docs/BOM.md`) are in step with `design.py` |
| energy-model | re-runs the model and **fails if ECO on the 1S4P LiFePO4 pack stops clearing 2.5 months with margin** (it is at 2.9) |
| enclosure | renders all four parts (front, back, door, stand), which runs the OpenSCAD asserts over every module, post and zone (including the charger's 5 mm to every wall and 30 mm to the gas bay's sensors), and checks each STL is manifold and fits a 250 × 210 bed (more than 8 % overhang is a warning, not a failure) |
| firmware | placeholder - building esp-matter in CI needs a prepared image |

The energy-model job is the interesting one: it makes the headline battery
claim a build-breaking assertion rather than a sentence in a README that
quietly goes stale. The ERC's one standing warning (no soft start on the
SEN62's switch, bench test T-P3) is a warning, not an error, so it does not
fail the job.
