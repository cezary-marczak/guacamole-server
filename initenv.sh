export BUILD_DIR=/opt/guacamole/guacamole-server
export PREFIX_DIR=/opt/guacamole
export WITH_FREERDP='f5f678fb9aa06769dea94d6ab1a940fbd509d534'
export WITH_LIBSSH2='libssh2-\d+(\.\d+)+'
export WITH_LIBTELNET='\d+(\.\d+)+'
export WITH_LIBVNCCLIENT='LibVNCServer-\d+(\.\d+)+'
export WITH_LIBWEBSOCKETS='v\d+(\.\d+)+'
export FREERDP_OPTS="\
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
    -DKRB5_TRACE=/dev/stdout \
    -DDEBUG_NLA=ON \
    -DGSS_ROOT_FLAVOUR=MIT"

export LIBSSH2_OPTS="\
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_SHARED_LIBS=ON"

export LIBTELNET_OPTS="\
    --disable-static \
    --disable-util"

export LIBVNCCLIENT_OPTS=""

export LIBWEBSOCKETS_OPTS="\
    -DDISABLE_WERROR=ON \
    -DLWS_WITHOUT_SERVER=ON \
    -DLWS_WITHOUT_TESTAPPS=ON \
    -DLWS_WITHOUT_TEST_CLIENT=ON \
    -DLWS_WITHOUT_TEST_PING=ON \
    -DLWS_WITHOUT_TEST_SERVER=ON \
    -DLWS_WITHOUT_TEST_SERVER_EXTPOLL=ON \
    -DLWS_WITH_STATIC=OFF"
export CFLAGS="-I${PREFIX_DIR}/include"
export LDFLAGS="-L${PREFIX_DIR}/lib"
export PKG_CONFIG_PATH="${PREFIX_DIR}/lib/pkgconfig"

# Ensure thread stack size will be 8 MB (glibc's default on Linux) rather than
# 128 KB (musl's default)
export LDFLAGS="$LDFLAGS -Wl,-z,stack-size=8388608"

export LC_ALL=C.UTF-8
export LD_LIBRARY_PATH=${PREFIX_DIR}/lib
export PKG_CONFIG_PATH=${PREFIX_DIR}/lib/pkgconfig
export GUACD_LOG_LEVEL=debug
export WLOG_LEVEL=DEBUG
export WLOG_PREFIX='%fl:%ln[%hr:%mi:%se:%ml] [%pid:%tid][%lv]: '
