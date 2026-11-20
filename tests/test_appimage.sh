#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AppImages made here (an AppDir packed with mksquashfs - each compressor - or as an ISO
# with bsdtar, put after an ELF program) and read back with appimage-info: the kind, the
# payload's offset, the .desktop keys, the icon (through the .DirIcon link, and without
# one), files of several blocks, fragments, sparse files, links through directories, a
# big directory; a broken image must fail cleanly.
#
#     test_appimage.sh APPIMAGE-INFO WORKDIR

set -u
INFO=$1
WORK=$2/appimage-test
BSDTAR=${BSDTAR:-}
rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK" || exit 99

fail=0
bad() { echo "FAIL: $*" >&2; fail=1; }

# ---------------------------------------------------------------- the AppDir
D=AppDir
mkdir -p $D/usr/bin $D/usr/lib $D/usr/share/icons/hicolor/256x256/apps $D/many
cat > $D/test-app.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Test App
Name[pl]=Aplikacja testowa
GenericName=Tester
Comment=Made by the test
Exec=AppRun %F
Icon=testapp
Categories=Utility;Development;
X-AppImage-Version=1.2.3

[Desktop Action New]
Name=Not this one
EOF
printf '#!/bin/sh\necho hello\n' > $D/AppRun
chmod +x $D/AppRun

# a small, real PNG (made with Python's zlib), and a second, different one for hicolor
python3 - "$D/testapp.png" "$D/usr/share/icons/hicolor/256x256/apps/testapp.png" <<'EOF'
import struct, sys, zlib
def png(path, colour):
    w = h = 16
    raw = b''.join(b'\0' + bytes(colour) * w for _ in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)
png(sys.argv[1], (200, 40, 40))
png(sys.argv[2], (40, 40, 200))
EOF
ln -s testapp.png $D/.DirIcon

# several blocks: random (stored uncompressed) and text (compressed)
head -c 1100000 /dev/urandom > $D/usr/lib/random.bin
i=0
while [ $i -lt 9000 ]; do echo "line $i of a compressible file"; i=$((i + 1)); done > $D/usr/lib/text.txt
# a sparse file (blocks of zeros, which mksquashfs stores as size 0) with data at its end
truncate -s 700000 $D/usr/lib/sparse.bin
printf 'END' >> $D/usr/lib/sparse.bin
echo "the real file" > $D/usr/share/real.txt
ln -s ../share/real.txt $D/usr/lib/link1
ln -s usr/share $D/dirlink
ln -s /usr/lib/link1 $D/abslink
ln -s loop2 $D/loop1
ln -s loop1 $D/loop2
# a directory of more than 256 entries (more than one directory header)
i=0
while [ $i -lt 600 ]; do echo $i > $D/many/f$i; i=$((i + 1)); done

# the second AppDir: no .DirIcon and no icon at the top (the icon from hicolor)
cp -a $D AppDir2
rm AppDir2/.DirIcon AppDir2/testapp.png

RUNTIME=/bin/true
RSIZE=$(stat -c %s $RUNTIME)

# check IMAGE: everything read back
check() {
    img=$1
    label=$2
    kind=$3
    out=$("$INFO" "$img" 2>&1)
    rc=$?
    if [ $rc -ne 0 ]; then
        if [ $rc -eq 20 ]; then echo "skip $label: not supported by this build"; return; fi
        bad "$label: appimage-info failed ($rc): $out"; return
    fi
    echo "$out" | grep -qx "Kind: $kind" || bad "$label: kind: $out"
    echo "$out" | grep -qx "Offset: $RSIZE" || bad "$label: offset: $out"
    echo "$out" | grep -qx "Name: Test App" || bad "$label: name: $out"
    echo "$out" | grep -qx "Exec: AppRun %F" || bad "$label: exec"
    echo "$out" | grep -qx "Categories: Utility;Development;" || bad "$label: categories"
    echo "$out" | grep -qx "Version: 1.2.3" || bad "$label: version"
    echo "$out" | grep -qx "Desktop-File: test-app.desktop" || bad "$label: desktop file"

    "$INFO" "$img" --icon icon.out > icon.txt || bad "$label: icon"
    cmp -s icon.out $4 || bad "$label: the icon is not $4"
    grep -q '^png ' icon.txt || bad "$label: icon format: $(cat icon.txt)"

    for f in usr/lib/random.bin usr/lib/text.txt usr/lib/sparse.bin AppRun many/f0 many/f599 many/f300; do
        "$INFO" "$img" --extract $f x.out || { bad "$label: extract $f"; continue; }
        cmp -s x.out AppDir/$f || bad "$label: $f differs"
    done
    for f in usr/lib/link1 dirlink/real.txt abslink; do
        "$INFO" "$img" --extract $f x.out || { bad "$label: through the link $f"; continue; }
        cmp -s x.out AppDir/usr/share/real.txt || bad "$label: link $f"
    done
    "$INFO" "$img" --extract loop1 x.out 2>/dev/null && bad "$label: a link loop was read"
    "$INFO" "$img" --extract nothing x.out 2>/dev/null && bad "$label: a missing file was read"
    "$INFO" "$img" --extract usr x.out 2>/dev/null && bad "$label: a directory was read"
    "$INFO" "$img" --desktop | grep -q '^Name\[pl\]=Aplikacja testowa' || bad "$label: --desktop"
    echo "ok $label"
}

for comp in gzip xz lzma lz4 zstd lzo; do
    rm -f p.sqfs
    if ! mksquashfs AppDir p.sqfs -comp $comp -noappend -quiet -all-root > /dev/null 2>&1; then
        echo "skip $comp: mksquashfs cannot make it"
        continue
    fi
    cat $RUNTIME p.sqfs > test-$comp.AppImage
    if [ $comp = lzo ]; then
        "$INFO" test-$comp.AppImage > /dev/null 2>&1
        [ $? -eq 20 ] || bad "lzo: not reported as unsupported"
        echo "ok lzo (unsupported, as it should be)"
        continue
    fi
    check test-$comp.AppImage "squashfs-$comp" SquashFS AppDir/testapp.png
done

# metadata and data stored uncompressed, no fragments, small blocks
mksquashfs AppDir p.sqfs -comp gzip -noI -noD -noF -noX -noappend -quiet -all-root > /dev/null 2>&1 &&
    cat $RUNTIME p.sqfs > test-plain.AppImage && check test-plain.AppImage squashfs-uncompressed SquashFS AppDir/testapp.png
mksquashfs AppDir p.sqfs -comp zstd -no-fragments -b 4096 -noappend -quiet -all-root > /dev/null 2>&1 &&
    cat $RUNTIME p.sqfs > test-small.AppImage && check test-small.AppImage squashfs-4k-nofrag SquashFS AppDir/testapp.png

# the icon without .DirIcon: from usr/share/icons/hicolor
mksquashfs AppDir2 p.sqfs -comp xz -noappend -quiet -all-root > /dev/null 2>&1
cat $RUNTIME p.sqfs > test-hicolor.AppImage
"$INFO" test-hicolor.AppImage --icon icon.out > /dev/null || bad "hicolor: no icon"
cmp -s icon.out AppDir/usr/share/icons/hicolor/256x256/apps/testapp.png || bad "hicolor: wrong icon"
echo "ok icon from hicolor"

# ISO 9660 (with Rock Ridge links)
if [ -n "$BSDTAR" ]; then
    "$BSDTAR" --format iso9660 -cf p.iso -C AppDir . 2>/dev/null
    cat $RUNTIME p.iso > test-iso.AppImage
    check test-iso.AppImage iso9660 "ISO 9660" AppDir/testapp.png
fi

# broken images: cut short, scribbled over - an error, never a crash
SIZE=$(stat -c %s test-gzip.AppImage)
head -c $((RSIZE + 2000)) test-gzip.AppImage > cut.AppImage
"$INFO" cut.AppImage --icon icon.out > /dev/null 2>&1
rc=$?
[ $rc -ge 128 ] && bad "a cut image crashed ($rc)"
head -c $((SIZE / 2)) test-gzip.AppImage > half.AppImage
for what in "" "--icon icon.out" "--extract usr/lib/random.bin x.out"; do
    # shellcheck disable=SC2086
    "$INFO" half.AppImage $what > /dev/null 2>&1
    rc=$?
    [ $rc -ge 128 ] && bad "a half image crashed ($rc) with '$what'"
done
cp test-zstd.AppImage noise.AppImage
python3 - noise.AppImage "$RSIZE" <<'EOF'
import os, random, sys
path, start = sys.argv[1], int(sys.argv[2])
size = os.path.getsize(path)
random.seed(7)
with open(path, 'r+b') as f:
    for _ in range(400):
        pos = random.randrange(start + 96, size)
        f.seek(pos)
        f.write(bytes([random.randrange(256)]))
EOF
for what in "" "--icon icon.out" "--extract usr/lib/random.bin x.out" "--extract many/f599 x.out"; do
    # shellcheck disable=SC2086
    "$INFO" noise.AppImage $what > /dev/null 2>&1
    rc=$?
    [ $rc -ge 128 ] && bad "a scribbled image crashed ($rc) with '$what'"
done
"$INFO" /bin/true > /dev/null 2>&1 && bad "a plain program was taken for an AppImage"
echo "ok broken images"

exit $fail
