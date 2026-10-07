# Change Proposals

This directory contains PKB-local change proposals following the OpenSpec
workflow. Repository-level proposals under `openspec/changes/` are a separate
source tree and are not included in this Sphinx site; link or summarize them
here when they need to be discoverable to PKB readers.

## What is a Change Proposal?

A change proposal documents a planned change to the system, including:

- **Why**: Problem statement and motivation
- **What**: Summary of changes
- **Impact**: Affected components and stakeholders
- **How**: Design and implementation plan

## Change Proposal Structure

Each change is in its own directory with:

```
changes/
└── change-id/
    ├── 02-proposal.md    # Why, what, impact
    ├── 01-design.md      # Technical design
    ├── 03-tasks.md       # Implementation tasks
    └── specs/         # Specification changes
```

## How to Create a Change Proposal

Use the `/PKB-change` command:

```bash
/PKB-change add-payment-gateway
```

## Active Changes

(Each change is a subdirectory with `02-proposal.md`, `01-design.md`, `03-tasks.md`.
Copy `_template/` to create a new PKB-local change.)

```{toctree}
:maxdepth: 1
:hidden:
:glob:

*/*
```

## Change Status

### Proposed

<!-- List proposed changes here -->

### In Review

<!-- List changes in review here -->

### Approved

<!-- List approved changes here -->

### Implementing

<!-- List changes being implemented here -->

### Completed

<!-- List completed changes here -->

## Process

1. **Propose**: Create a change proposal with `/PKB-change`
2. **Design**: Fill in design details
3. **Review**: Get feedback from stakeholders
4. **Approve**: Get approval from decision makers
5. **Implement**: Execute the tasks
6. **Close**: Mark as completed

## References

- [OpenSpec](https://github.com/jpoehnelt/openspec)
- Change management best practices

---
<!-- PKB-metadata
last_updated: 2026-08-30
commit: d94936cc10
updated_by: human+ai
review_status: pending
review_score: 0
reviewed_by:
confidentiality: L1
-->
