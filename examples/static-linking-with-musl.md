# How to build a statically linked Snac with musl

Prepare the environment
```
mkdir build
cd build
export BUILD_TARGET=$PWD
export CC="musl-gcc"
```

Download and build latest zlib
```
wget http://zlib.net/current/zlib.tar.gz
tar xvf zlib.tar.gz
cd zlib-1.3.1/
./configure --prefix=$BUILD_TARGET --static
make
make install
cd ..
```

Download and build latest openssl
```
wget https://github.com/openssl/openssl/releases/download/openssl-3.4.0/openssl-3.4.0.tar.gz
tar xvf openssl-3.4.0.tar.gz
cd openssl-3.4.0
CC="musl-gcc -fPIE -pie -static -idirafter /usr/include/ -idirafter /usr/include/x86_64-linux-gnu/" \
   ./Configure no-shared no-async --prefix=$BUILD_TARGET --openssldir=$BUILD_TARGET/ssl linux-x86_64
make depend
make
make install
cd ..
```

Download and build latest curl
```
wget https://curl.se/download/curl-7.88.1.tar.gz
tar xvf curl-7.88.1.tar.gz
cd curl-7.88.1
./configure --disable-shared --enable-static --disable-silent-rules \
            --disable-debug --disable-warnings --disable-werror \
            --disable-curldebug --disable-symbol-hiding --disable-ares \
            --disable-rt --disable-ech --disable-dependency-tracking \
            --disable-libtool-lock --enable-http --disable-ftp \
            --disable-file --disable-ldap --disable-ldaps \
            --disable-rtsp --disable-proxy --disable-dict \
            --disable-telnet --disable-tftp --disable-pop3 \
            --disable-imap --disable-smb --disable-smtp --disable-gopher \
            --disable-mqtt --disable-manual --disable-libcurl-option --disable-ipv6 \
            --disable-openssl-auto-load-config --disable-versioned-symbols 
            --disable-verbose --disable-sspi --disable-crypto-auth \
            --disable-ntlm --disable-ntlm-wb --disable-tls-srp \
            --disable-unix-sockets --disable-cookies --disable-socketpair \
            --disable-http-auth --disable-doh --disable-mime --disable-dateparse \
            --disable-netrc --disable-progress-meter --disable-dnsshuffle \
            --disable-get-easy-options --disable-alt-svc --disable-websockets \
            --without-brotli --without-zstd --without-libpsl --without-libgsasl \
            --without-librtmp --without-winidn --disable-threaded-resolver  \
            --with-openssl=$BUILD_TARGET/ --with-zlib=$BUILD_TARGET/ \
            --prefix=$BUILD_TARGET/
make
make install
cd ..
```

Download and build latest snac2
```
git clone https://codeberg.org/grunfink/snac2.git # or cd to your existing repo
cd snac2
make CFLAGS="-g -Wall -Wextra -pedantic -static -DWITHOUT_SHM" \
     LDFLAGS="-L$BUILD_TARGET/lib64 -lcurl -lssl -lcrypto -lz" \
     PREFIX=$BUILD_TARGET 
make install PREFIX=$BUILD_TARGET
cd ..
```

Finally a statically linked snac is ready at $BUILD_TARGET/bin
