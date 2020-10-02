#!/bin/sh

url="https://github.com/facebook/zstd"
release="$(curl -s "${url}"/releases/latest | grep -oP '(?<=v)[\d\.]+')"
zstd="/tmp/zstd-${release}/"

curl "${url}/releases/download/v${release}/zstd-${release}.tar.gz" \
    --output /tmp/zstd.tar.gz
    
tar -C /tmp -xzf /tmp/zstd.tar.gz

cp ${zstd}/lib/zstd.h module/zstd/lib/
cp ${zstd}/lib/common/zstd_errors.h module/zstd/lib/
${zstd}/contrib/single_file_libs/combine.sh \
    -r ${zstd}/lib -o module/zstd/lib/zstd.c module/zstd/zstd-in.c
