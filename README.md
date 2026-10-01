# Akilleez-QA server x64 working fork

**[Server PRs and branch guide](FORK-STATUS.md) · [Complete client/server review index](https://github.com/Akilleez-QA/client-tools/blob/review/client-x64-evidence/review/client-x64/REVIEW-INDEX.md) · [Fork draft → upstream map](https://github.com/Akilleez-QA/client-tools/blob/review/client-x64-evidence/review/client-x64/package-inventory/fork-preparation-map.md)**

As verified on 2026-10-01, all 12 submitted server PRs are open and all reported checks on their current heads pass. The client/server delivery contains 58 upstream PRs in total; maintainer review and merging remain.

This fork's `master` retains upstream source baseline `7d2159a3` plus navigation. The LP64 and shared compatibility work lives on the branches listed in [FORK-STATUS.md](FORK-STATUS.md). Source, CI and runtime evidence remain tied to their named commits; the original README below describes the legacy baseline.

---

## Original upstream README (legacy baseline)

# Star Wars Galaxies Source Code (C++) Repository

This is the main server code for SWGSource 1.2 as originally forked from the https://bitbucket.org/stellabellumswg/ repository.  Please see that repository for original publication and alteration credit.

# Works in progress
* 64-bit-types - fully 64 bit version that builds and runs completely.

# Building

For local testing, and non-live builds set MODE=Release or MODE=Debug in the build.properties file in swg-main.

For production, user facing builds, set MODE=MINSIZEREL for profile built, heavily optimized versions of the binaries.

## Profiling and Using Profiles (IN-WORK)

To generate new profiles, build SWG with MODE=RELWITHDEBINFO. 

Add export LLVM_PROFILE_FILE="output-%p.profraw" to your startServer.sh file. 

WHILE THE SERVER IS RUNNING do a ps -a to get the pid's of each SWG executable. And take note of which ones are which.

After you cleanly exit (shutdown) the server, and ctrl+c the LoginServer, move each output-pid.profraw to a folder named for it's process.

Then, proceed to combine them into usable profiles for the compiler:

llvm-profdata merge -output=code.profdata output-*.profraw

Finally, then replace the profdata files with the updated versions, within the src/ tree.

See http://clang.llvm.org/docs/UsersManual.html#profiling-with-instrumentation for more information.

# More Information

See https://swg-source.github.io/ for more information on the SWG Source project.

Join the SWGSource Discord if you would like to contribute:  https://discord.gg/j53cMj9
