#!/bin/bash
# Starts hardthz from the folder this file is in. Run from Terminal (a double-click opens it there), hardthz is allowed to
# hear your instrument once Terminal is: macOS asks the first time.
cd "$(dirname "$0")" || exit 1
./hardthz
