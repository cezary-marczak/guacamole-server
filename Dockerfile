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

#
# Dockerfile for guacamole-server
#

# The Alpine Linux image that should be used as the basis for the guacd image
ARG ALPINE_BASE_IMAGE=3.18.4
FROM alpine:${ALPINE_BASE_IMAGE} AS builder

# Install build dependencies
RUN apk add --no-cache                \
        autoconf                      \
        automake                      \
        build-base                    \
        cairo-dev                     \
        cmake                         \
        git                           \
        grep                          \
        libjpeg-turbo-dev             \
        libpng-dev                    \
        libtool                       \
        libwebp-dev                   \
        make                          \
        pango-dev                     \
        pulseaudio-dev                \
        util-linux-dev                \
        ffmpeg-dev \
    openssl-dev \
    openssl \
        krb5-libs \
        krb5 \
        krb5-dev \
        libgss \
        krb5-conf \
        musl-dev

# Copy source to container for sake of build
# ARG BUILD_DIR=/tmp/guacamole-server
# COPY . ${BUILD_DIR}

#
# Base directory for installed build artifacts.
#
# NOTE: Due to limitations of the Docker image build process, this value is
# duplicated in an ARG in the second stage of the build.
#
ARG PREFIX_DIR=/opt/guacamole

#
# Automatically select the latest versions of each core protocol support
# library (these can be overridden at build time if a specific version is
# needed)
#
ARG WITH_FREERDP='2\.11\.7'
ARG WITH_LIBSSH2='libssh2-\d+(\.\d+)+'
ARG WITH_LIBTELNET='\d+(\.\d+)+'
ARG WITH_LIBVNCCLIENT='LibVNCServer-\d+(\.\d+)+'
ARG WITH_LIBWEBSOCKETS='v\d+(\.\d+)+'

#
# Default build options for each core protocol support library, as well as
# guacamole-server itself (these can be overridden at build time if different
# options are needed)
#

ARG FREERDP_OPTS="\
    -DBUILTIN_CHANNELS=OFF \
    -DCHANNEL_URBDRC=OFF \
    -DWITH_ALSA=OFF \
    -DWITH_CAIRO=ON \
    -DWITH_CHANNELS=ON \
    -DWITH_CLIENT=ON \
    -DWITH_CUPS=OFF \
    -DWITH_DIRECTFB=OFF \
    -DWITH_FFMPEG=OFF \
    -DWITH_GSM=OFF \
    -DWITH_GSSAPI=ON \
    -DWITH_IPP=OFF \
    -DWITH_JPEG=ON \
    -DWITH_LIBSYSTEMD=OFF \
    -DWITH_MANPAGES=OFF \
    -DWITH_OPENH264=OFF \
    -DWITH_OPENSSL=ON \
    -DWITH_OSS=OFF \
    -DWITH_PCSC=OFF \
    -DWITH_PULSE=OFF \
    -DWITH_SERVER=ON \
    -DWITH_SERVER_INTERFACE=OFF \
    -DWITH_SHADOW_MAC=OFF \
    -DWITH_SHADOW_X11=OFF \
    -DWITH_SSE2=ON \
    -DWITH_WAYLAND=OFF \
    -DWITH_X11=OFF \
    -DWITH_X264=OFF \
    -DWITH_XCURSOR=ON \
    -DWITH_XEXT=ON \
    -DWITH_XI=OFF \
    -DWITH_XINERAMA=OFF \
    -DWITH_XKBFILE=ON \
    -DWITH_XRENDER=OFF \
    -DWITH_XTEST=OFF \
    -DWITH_XV=OFF \
    -DWITH_ZLIB=ON \
    -DWITH_KRB5=ON \
    -DWLOG_LEVEL=1 \
    -DKRB5_TRACE=/dev/stdout \
    -DDEBUG_NLA=ON \
    -DGSS_ROOT_FLAVOUR=MIT"

ARG GUACAMOLE_SERVER_OPTS="\
    --disable-guaclog"

ARG LIBSSH2_OPTS="\
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_SHARED_LIBS=ON"

ARG LIBTELNET_OPTS="\
    --disable-static \
    --disable-util"

ARG LIBVNCCLIENT_OPTS=""

ARG LIBWEBSOCKETS_OPTS="\
    -DDISABLE_WERROR=ON \
    -DLWS_WITHOUT_SERVER=ON \
    -DLWS_WITHOUT_TESTAPPS=ON \
    -DLWS_WITHOUT_TEST_CLIENT=ON \
    -DLWS_WITHOUT_TEST_PING=ON \
    -DLWS_WITHOUT_TEST_SERVER=ON \
    -DLWS_WITHOUT_TEST_SERVER_EXTPOLL=ON \
    -DLWS_WITH_STATIC=OFF"

ARG BUILD_DIR=/tmp/guacamole-server

# Build the dependencies for guacamole-server
RUN mkdir -p ${BUILD_DIR}/src/guacd-docker/bin
COPY ./src/guacd-docker/bin/build-deps.sh ${BUILD_DIR}/src/guacd-docker/bin
#COPY ./src/guacd-docker/freerdp.patch ${BUILD_DIR}/freerdp.patch
RUN ${BUILD_DIR}/src/guacd-docker/bin/build-deps.sh
#RUN rm -f ${BUILD_DIR}/src/guacd-docker/bin/build-deps.sh

# Copy source to container for sake of build
#COPY . ${BUILD_DIR}

# Build guacamole-server and its core protocol library dependencies
#RUN ${BUILD_DIR}/src/guacd-docker/bin/build-all.sh
COPY ./src/guacd-docker/bin/list-dependencies.sh ${BUILD_DIR}/src/guacd-docker/bin

# Record the packages of all runtime library dependencies
RUN ${BUILD_DIR}/src/guacd-docker/bin/list-dependencies.sh \
        ${PREFIX_DIR}/sbin/guacd               \
        ${PREFIX_DIR}/lib/libguac-client-*.so  \
        ${PREFIX_DIR}/lib/freerdp2/*guac*.so   \
        > ${PREFIX_DIR}/DEPENDENCIES

# Use same Alpine version as the base for the runtime image
FROM alpine:${ALPINE_BASE_IMAGE}

#
# Base directory for installed build artifacts. See also the
# CMD directive at the end of this build stage.
#
# NOTE: Due to limitations of the Docker image build process, this value is
# duplicated in an ARG in the first stage of the build.
#
ARG PREFIX_DIR=/opt/guacamole

# Runtime environment
ENV LC_ALL=C.UTF-8
ENV LD_LIBRARY_PATH=${PREFIX_DIR}/lib
ENV PKG_CONFIG_PATH=${PREFIX_DIR}/lib/pkgconfig
ENV GUACD_LOG_LEVEL=info

# Copy build artifacts into this stage
COPY --from=builder ${PREFIX_DIR} ${PREFIX_DIR}

# Install dependencies
RUN apk add --no-cache \
    build-base \
    cmake \
    ninja \
    git \
    libx11-dev \
    libxkbfile-dev \
    libxi-dev \
    libxcursor-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxrender-dev \
    alsa-lib-dev \
    ffmpeg-dev \
    jpeg-dev \
    openssl-dev \
    zlib-dev \
    musl-dev \
    libc-dev \
    wayland-dev \
    libxkbcommon-dev \
    libxdamage-dev \
    libxcomposite-dev \
    dbus-dev \
    cups-dev \
    pulseaudio-dev \
    linux-headers \
    openssl \
    krb5 \
    krb5-dev  \
             util-linux-dev \
             openssh \
             rsync \
             gdb \
             cunit-dev \
            autoconf                      \
            automake                      \
            build-base                    \
            cairo-dev                     \
            cmake                         \
            git                           \
            grep                          \
            libjpeg-turbo-dev             \
            libpng-dev                    \
            libtool                       \
            libwebp-dev                   \
            make                          \
            pango-dev                     \
            pulseaudio-dev                \
            util-linux-dev                \
            ffmpeg-dev \
            krb5-libs \
            krb5 \
            krb5-dev \
            libgss \
            krb5-conf \
            musl-dev

RUN apk add --no-cache \
    icu \
    icu-dev \
    fuse3 \
    fuse3-dev \
    libusb \
    libusb-dev

# Set up directory structure
RUN mkdir -p $PREFIX_DIR && \
    echo "" > /opt/guacamole/toolchain.cmake

# Generate SSL certificates
RUN openssl genpkey -algorithm RSA -out private_key.pem -pkeyopt rsa_keygen_bits:2048 && \
    openssl req -new -x509 -key private_key.pem -out certificate.pem -days 365 -subj "/C=US/ST=State/L=City/O=Organization/CN=localhost"

# Clone and build zlib following exact steps
RUN git clone --depth 1 -b v1.3 https://github.com/madler/zlib.git && \
    cmake -GNinja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/guacamole/toolchain.cmake \
    -B zlib-build \
    -S zlib \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SKIP_INSTALL_ALL_DEPENDENCY=ON \
    -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR \
    -DLIBRESSL_APPS=OFF \
    -DLIBRESSL_TESTS=OFF && \
    cmake --build zlib-build && \
    cmake --install zlib-build && \
    rm -rf /src/zlib /src/zlib-build

# Clone and build uriparser following exact steps
RUN git clone --depth 1 -b uriparser-0.9.7 https://github.com/uriparser/uriparser.git && \
    cmake -GNinja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/guacamole/toolchain.cmake \
    -B uriparser-build \
    -S uriparser \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SKIP_INSTALL_ALL_DEPENDENCY=ON \
    -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR \
    -DURIPARSER_BUILD_DOCS=OFF \
    -DURIPARSER_BUILD_TESTS=OFF && \
    cmake --build uriparser-build && \
    cmake --install uriparser-build && \
    rm -rf /src/uriparser /src/uriparser-build

# Clone and build cJSON following exact steps
RUN git clone --depth 1 -b v1.7.16 https://github.com/DaveGamble/cJSON.git && \
    cmake -GNinja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/guacamole/toolchain.cmake \
    -B cJSON-build \
    -S cJSON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SKIP_INSTALL_ALL_DEPENDENCY=ON \
    -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR \
    -DENABLE_CJSON_TEST=OFF \
    -DBUILD_SHARED_AND_STATIC_LIBS=ON && \
    cmake --build cJSON-build && \
    cmake --install cJSON-build && \
    rm -rf /src/cJSON /src/cJSON-build

# Clone and build SDL2
RUN git clone --depth 1 -b release-2.28.1 https://github.com/libsdl-org/SDL.git && \
    cmake -GNinja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/guacamole/toolchain.cmake \
        -B SDL-build \
        -S SDL \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SKIP_INSTALL_ALL_DEPENDENCY=ON \
        -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR \
        -DSDL_TEST=OFF \
        -DSDL_TESTS=OFF \
        -DSDL_STATIC_PIC=ON && \
    cmake --build SDL-build && \
    cmake --install SDL-build && \
    rm -rf /src/SDL /src/SDL-build

# Clone and build SDL2_ttf
RUN git clone --depth 1 --recurse-submodules -b release-2.20.2 https://github.com/libsdl-org/SDL_ttf.git && \
    cmake -GNinja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/guacamole/toolchain.cmake \
        -B SDL_ttf-build \
        -S SDL_ttf \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SKIP_INSTALL_ALL_DEPENDENCY=ON \
        -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR \
        -DSDL2TTF_HARFBUZZ=ON \
        -DSDL2TTF_FREETYPE=ON \
        -DSDL2TTF_VENDORED=ON \
        -DFT_DISABLE_ZLIB=OFF \
        -DSDL2TTF_SAMPLES=OFF && \
    cmake --build SDL_ttf-build && \
    cmake --install SDL_ttf-build && \
    rm -rf /src/SDL_ttf /src/SDL_ttf-build

# Clone and build SDL2_image
RUN git clone --depth 1 --recurse-submodules -b release-2.8.1 https://github.com/libsdl-org/SDL_image.git && \
    cmake -GNinja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/guacamole/toolchain.cmake \
        -B SDL_image-build \
        -S SDL_image \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SKIP_INSTALL_ALL_DEPENDENCY=ON \
        -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR \
        -DSDL2IMAGE_SAMPLES=OFF \
        -DSDL2IMAGE_DEPS_SHARED=OFF && \
    cmake --build SDL_image-build && \
    cmake --install SDL_image-build && \
    rm -rf /src/SDL_image /src/SDL_image-build

# Copy pre-downloaded FreeRDP source files instead of cloning
#COPY . /src/freerdp

# Build FreeRDP using the copied source files
#RUN cmake -GNinja \
#        -DCMAKE_TOOLCHAIN_FILE=/opt/guacamole/toolchain.cmake \
#        -B freerdp-build \
#        -S freerdp \
#        -DCMAKE_BUILD_TYPE=Release \
#        -DCMAKE_SKIP_INSTALL_ALL_DEPENDENCY=ON \
#        -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR \
#        -DWITH_SERVER=ON \
#        -DWITH_KRB5=ON \
#        -DWITH_SAMPLE=ON \
#        -DWITH_PLATFORM_SERVER=OFF \
#        -DUSE_UNWIND=OFF \
#        -DWITH_SWSCALE=OFF \
#        -DWITH_FFMPEG=OFF \
#        -DWITH_WEBVIEW=OFF \
#        -DWITH_PROXY=ON \
#        -DWITH_MANPAGES=OFF \
#        -DWITH_OPUS=OFF \
#        -DWITH_CLIENT_SDL=OFF \
#        -DWITH_SHADOW=OFF \
#        -DWITH_X11=ON \
#        -DWITH_CUPS=OFF
#
# Build step with logging
#RUN cmake --build freerdp-build 2>&1 | tee build.log

# Install step
#RUN cmake --install freerdp-build

# Add configuration file with SSL settings
RUN printf "[Server]\nHost=0.0.0.0\nPort=3389\n\n[Target]\nHost=example.hostname.com\nPort=3389\n\n[Channels]\nClipboard=TRUE\nPassthroughIsBlacklist=TRUE\n\n[Clipboard]\nTextOnly=FALSE\nMaxTextLength=0\n\n[Certificates]\nCertificateFile=\"/src/certificate.pem\"\nPrivateKeyFile=\"/src/private_key.pem\"\n" > /opt/guacamole/config.ini

RUN echo 'PasswordAuthentication yes' >> /etc/ssh/sshd_config
RUN echo 'PermitRootLogin yes' >> /etc/ssh/sshd_config
RUN echo 'PubkeyAuthentication yes' >> /etc/ssh/sshd_config
RUN echo -n 'root:FNjkgnsdjk4trgrsd#@fs' | chpasswd

# Expose the proxy port
EXPOSE 3389
EXPOSE 22

# Bring runtime environment up to date and install runtime dependencies
RUN apk add --no-cache                \
        ca-certificates               \
        ghostscript                   \
        netcat-openbsd                \
        shadow                        \
        terminus-font                 \
        ttf-dejavu                    \
        ttf-liberation                \
        ffmpeg-dev                    \
        krb5-conf \
        krb5-libs \
        krb5-dev \
        krb5 \
        libgss \
        musl-dev \
        util-linux-login && \
    xargs apk add --no-cache < ${PREFIX_DIR}/DEPENDENCIES

# Checks the operating status every 5 minutes with a timeout of 5 seconds
HEALTHCHECK --interval=5m --timeout=5s CMD nc -z 127.0.0.1 4822 || exit 1

# Create a new user guacd
ARG UID=1000
ARG GID=10001
RUN groupadd --gid $GID guacd
RUN useradd --system --create-home --shell /bin/sh --uid $UID --gid $GID guacd

# Create symlinks to procyon krb5.conf and hosts
RUN mkdir -p /etc/procyon-tmp
COPY ./src/guacd-docker/krb5.conf /etc/procyon-tmp/krb5.conf
COPY ./entrypoint-dev.sh /etc/procyon-tmp/entrypoint.sh
COPY ./src/guacd-docker/bin/copy_hosts.sh /etc/procyon-tmp/copy_hosts.sh
RUN chmod +x /etc/procyon-tmp/entrypoint.sh
RUN chmod +x /etc/procyon-tmp/copy_hosts.sh

# Expose the default listener port
EXPOSE 4822

#USER guacd

# Start guacd, listening on port 0.0.0.0:4822
#
# Note the path here MUST correspond to the value specified in the 
# PREFIX_DIR build argument.
#
ENTRYPOINT [ "/etc/procyon-tmp/entrypoint.sh" ]
#CMD "sleep 100d"
