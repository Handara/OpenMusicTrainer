# Contributing to hardthz

Thanks for helping. Bug reports, charts, lessons, exercises and code are all welcome.

## Before your first contribution is merged: the CLA

hardthz is free software under the GNU GPL v3, and it will stay that way: anyone may read, build, change and share
it, and every version shared must stay open.

To keep hardthz sustainable, its maintainer also needs to be able to ship builds that the GPL alone doesn't allow,
such as a Steam version, which has to link Steam's own closed SDK. So contributions come with a Contributor
License Agreement (CLA): you keep the copyright to your work, and you give the maintainer the right to also
publish it under other licences. Whatever happens, your contribution stays available to everyone under the GPL.

You'll be asked to agree to the CLA once, on your first pull request.

## The name and the logo

The code is yours to fork, but the name **hardthz**, its wordmark and its logo are not part of the
licence. A fork is welcome to use the code; it needs a name and a logo of its own, so nobody takes it for the
official game.

## Code

- C++17, built with CMake (see the README). `hardthz_core` is plain C++ with no graphics or audio, and has unit
  tests: `ctest --test-dir build --output-on-failure` must pass.
- Match the code around your change: its naming, its comments, its size of steps.
- One change per pull request, with a message saying what it changes for the player.
