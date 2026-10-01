# Server fork status and PR guide

Verified against GitHub on **2026-10-01**. All 12 server packages below are submitted upstream and open for review. All reported upstream checks on these heads are successful; older failed runs remain visible. The [complete index](https://github.com/Akilleez-QA/client-tools/blob/review/client-x64-evidence/review/client-x64/REVIEW-INDEX.md) covers all 58 client/server submissions, their prerequisites, evidence and production-count boundaries.

## Source branches

- [`lp64-fixed-width-boundaries`](https://github.com/Akilleez-QA/src/tree/lp64-fixed-width-boundaries) at `6b998f6fc281a88a50a710d7ae6afd55814b1946` is server #35, based on upstream `64-bit-types`.
- [`integration/windows-shared-compat`](https://github.com/Akilleez-QA/src/tree/integration/windows-shared-compat) at `731d85cc10326f5f2d656b4b66f09c5feab30635` holds maintained shared compatibility work.
- Fork `master` retains upstream source baseline `7d2159a337281184d6a55db30d2bc9a4013c0e80` plus navigation. It does not include those integration changes.
- Each upstream PR uses its own reviewed fork branch/head. Preparation drafts retain their earlier incremental bases; [their upstream successors](https://github.com/Akilleez-QA/client-tools/blob/review/client-x64-evidence/review/client-x64/package-inventory/fork-preparation-map.md) are the current submission authority.

## Submitted server PRs

| PR | Scope | Head | Upstream base |
|---|---|---|---|
| [#35](https://github.com/SWG-Source/src/pull/35) | Complete Linux server LP64 compatibility, database boundaries and shutdown fixes | `6b998f6fc2` | `64-bit-types` |
| [#37](https://github.com/SWG-Source/src/pull/37) | Separate the checked int-length move helper from CRT memmove | `12c98077c0` | `master` |
| [#38](https://github.com/SWG-Source/src/pull/38) | Build the submitted server revision in legacy CI | `1c0152794e` | `master` |
| [#39](https://github.com/SWG-Source/src/pull/39) | foundation: support Windows x64 floating point controls | `eb75e4e007` | `master` |
| [#40](https://github.com/SWG-Source/src/pull/40) | math: add Windows x64 SSE kernels | `8931a807e1` | `master` |
| [#41](https://github.com/SWG-Source/src/pull/41) | Use native-width Windows diagnostics and API results | `72bddb3634` | `master` |
| [#42](https://github.com/SWG-Source/src/pull/42) | Scope the native crypto PCH packing diagnostic | `af7c4fa520` | `master` |
| [#43](https://github.com/SWG-Source/src/pull/43) | Preserve host-sized string lengths in shared helpers | `0c8df6db37` | `master` |
| [#44](https://github.com/SWG-Source/src/pull/44) | Preserve native Windows socket handles and IOCP keys | `acc7aa8bd1` | `master` |
| [#45](https://github.com/SWG-Source/src/pull/45) | Use native byte swaps for Windows x64 network conversions | `5353694a69` | `master` |
| [#46](https://github.com/SWG-Source/src/pull/46) | Bound archive payload reads and preserve storage ownership | `0b45f5e726` | `64-bit-types` |
| [#47](https://github.com/SWG-Source/src/pull/47) | Capture native Windows stacks with complete DbgHelp locking | `b17b06f87a` | `64-bit-types` |

Review independent master fixes separately. Server #38 repairs CI to build the submitted revision; its workflow changes are included separately in affected master-based PRs. The earlier infrastructure failures remain recorded. Server #46 and #47 include #35 as a real prerequisite on `64-bit-types`; their full diffs must not be counted or applied as independent copies of that work. Follow the incremental comparisons in each PR.

## Evidence and limits

Current check results are linked from the PRs and preserved in the [submission ledger](https://github.com/Akilleez-QA/client-tools/blob/review/client-x64-evidence/review/client-x64/package-inventory/submission-ledger.json). Successful Linux server CI does not establish native Windows runtime coverage for every shared Windows change. Those packages state their own compile/probe evidence.

[Recorded mixed-width sessions](https://github.com/Akilleez-QA/client-tools/blob/eca74ffa5741f608a1417944b2e2602868936517/tools/test-wire-compatibility/live-session.md) exercised client/server architecture combinations at the named historical revisions. They are bounded runtime evidence, not new full-cluster acceptance of every submitted head or a claim of complete gameplay/fidelity equivalence.

No upstream PR has been automatically merged. Repository source, test/build changes and evidence are separated in the packages; private SDKs, provider binaries and game assets are not included. Use the [current client/server status](https://github.com/Akilleez-QA/client-tools/blob/review/client-x64-evidence/review/client-x64/FORK-STATUS.md) for combined implementation, runtime and media limits.
