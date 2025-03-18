#!/bin/sh -e
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

##
## @fn build-deps.sh
##
## Builds the various core protocol library dependencies.
##

# Pre-populate build control variables such that the custom build prefix is
# used for C headers, locating libraries, etc.
CFLAGS_REL="-I${PREFIX_DIR}/include"
CFLAGS_DEBUG="-I${PREFIX_DIR}/include -O0 -g"

export LDFLAGS="-L${PREFIX_DIR}/lib"
export PKG_CONFIG_PATH="${PREFIX_DIR}/lib/pkgconfig"

# Ensure thread stack size will be 8 MB (glibc's default on Linux) rather than
# 128 KB (musl's default)
export LDFLAGS="$LDFLAGS -Wl,-z,stack-size=8388608"

##
## Builds and installs the source at the given git repository, automatically
## switching to the version of the source at the tag/commit that matches the
## given pattern.
##
## @param URL
##     The URL of the git repository that the source should be downloaded from.
##
## @param PATTERN
##     The Perl-compatible regular expression that the tag must match. If no
##     tag matches the regular expression, the pattern is assumed to be an
##     exact reference to a commit, branch, etc. acceptable by git checkout.
##
## @param ...
##     Any additional command-line options that should be provided to CMake or
##     the configure script.
##
install_from_git() {

    URL="$1"
    PATTERN="$2"
    KEEP_BUILD="$3"
    shift 3

    # Calculate top-level directory name of resulting repository from the
    # provided URL
    REPO_DIR="$(basename "$URL" .git)"

    # Allow dependencies to be manually omitted with the tag/commit pattern "NO"
    if [ "$PATTERN" = "NO" ]; then
        echo "NOT building $REPO_DIR (explicitly skipped)"
        return
    fi

    BUILD_TYPE=""

    # Clone repository and change to top-level directory of source
    if [ "$KEEP_BUILD" = "yes" ]; then
      echo "Keeping build directory of $REPO_DIR"
      cd "$PREFIX_DIR"
      export CFLAGS="$CFLAGS_DEBUG"
      BUILD_TYPE="Debug"
    else
      cd /tmp
      export CFLAGS="$CFLAGS_REL"
      BUILD_TYPE="Release"
    fi

    git clone "$URL"
    cd $REPO_DIR/

    # Locate tag/commit based on provided pattern
    VERSION="$(git tag -l --sort=-v:refname | grep -Px -m1 "$PATTERN" \
        || echo "$PATTERN")"

    # Switch to desired version of source
    echo "Building $REPO_DIR @ $VERSION ..."
    git -c advice.detachedHead=false checkout "$VERSION"

    # Configure build using CMake or GNU Autotools, whichever happens to be
    # used by the library being built
    if [ -e CMakeLists.txt ]; then
        cmake -DCMAKE_INSTALL_PREFIX="$PREFIX_DIR" -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -B cmake-build "$@" .
        cmake --build cmake-build -j4 || cmake --build cmake-build
        cmake --install cmake-build
    else
        [ -e configure ] || autoreconf -fi
        ./configure --prefix="$PREFIX_DIR" "$@"
        make V=1 -j4 || make V=1
        make install
    fi

}

#
# Build and install core protocol library dependencies
#
install_from_git "https://github.com/cezary-marczak/FreeRDP" "$WITH_FREERDP" yes $FREERDP_OPTS
install_from_git "https://github.com/libssh2/libssh2" "$WITH_LIBSSH2" no $LIBSSH2_OPTS
install_from_git "https://github.com/seanmiddleditch/libtelnet" "$WITH_LIBTELNET" no $LIBTELNET_OPTS
install_from_git "https://github.com/LibVNC/libvncserver" "$WITH_LIBVNCCLIENT" no $LIBVNCCLIENT_OPTS
install_from_git "https://github.com/warmcat/libwebsockets" "$WITH_LIBWEBSOCKETS" no $LIBWEBSOCKETS_OPTS
